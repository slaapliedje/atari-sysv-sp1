/*
 * atwInit.c - screen side of the ATW800/2 ddx (X11R6.3): map the card, set the
 * mode, hand the framebuffer to cfb, keep the hardware LUT in step with
 * the installed colormap.
 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "atw.h"
#include "servermd.h"
#include "screenint.h"
#include "colormapst.h"
#include "resource.h"
#include "mi.h"

extern int TellLostMap(), TellGainedMap();
extern Bool cfbScreenInit(), cfbCreateDefColormap();

atwScreenRec	atwScreen;

/* VESA DMT 60 Hz timings. The PLL (27 MHz reference, Programmer's
 * Manual) runs at five times the pixel clock, 27 MHz * fb / id, with the
 * output divider keeping the VCO in 400-1200 MHz: computed the way
 * xserver-r6's atwPll() does. */
static atwModeRec atwModes[] = {
    { "640x480",   640,  480,  16,  96,  48, 10, 2, 33, 14, 3, 8 },	/* 25.2 MHz */
    { "800x600",   800,  600,  40, 128,  88,  1, 4, 23, 37, 5, 4 },	/* 39.96 */
    { "1024x768", 1024,  768,  24, 136, 160,  3, 6, 29, 12, 1, 2 },	/* 64.8 */
    { "1280x1024", 1280, 1024, 48, 112, 248,  1, 3, 38, 20, 1, 2 },	/* 108 */
    { 0 }
};
static atwModeRec *atwWantedMode = &atwModes[0];

static PixmapFormatRec formats[] = {
    { 1, 1, BITMAP_SCANLINE_PAD },
    { 8, 8, BITMAP_SCANLINE_PAD },
};
#define NUMFORMATS (sizeof formats / sizeof formats[0])

#define REGW(o)	(*(volatile unsigned short *)(atwScreen.fb + ATW_OFF_VTG + (o)))
#define LUTW(i)	(*(volatile unsigned short *)(atwScreen.fb + ATW_OFF_LUT + 2 * (i)))

/*
 * Map the card through /dev/mem (the stock mm driver maps anything that
 * does not bus-error) and check the FPGA answers.
 */
static Bool
atwMapCard()
{
    int fd;
    char *p;
    char id[33];
    int i;

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) {
	ErrorF("atw: cannot open /dev/mem (errno %d)\n", errno);
	return FALSE;
    }
    p = (char *)mmap((caddr_t)0, ATW_SIZE, PROT_READ | PROT_WRITE,
		     MAP_SHARED, fd, (off_t)ATW_PHYS);
    if (p == (char *)-1) {
	ErrorF("atw: cannot map the card at %lx (errno %d)\n", ATW_PHYS, errno);
	return FALSE;
    }
    atwScreen.fb = (volatile unsigned char *)p;
    for (i = 0; i < 32; i++) {
	id[i] = (char)atwScreen.fb[ATW_OFF_INFO + i];
	if (id[i] != 0 && (id[i] < 32 || id[i] > 126))
	    id[i] = '.';
    }
    id[32] = 0;
    if (id[0] == 0) {
	ErrorF("atw: no FPGA id string, is the card there?\n");
	return FALSE;
    }
    ErrorF("atw: %s\n", id);
    return TRUE;
}

static void
atwSetMode(m)
    atwModeRec *m;
{
    REGW(ATW_R_CTRL) = 0;		/* stop the VTG */
    REGW(ATW_R_MEM) = 1;
    REGW(ATW_R_PLLFB) = m->pllfb - 1;
    REGW(ATW_R_PLLID) = m->pllid - 1;
    REGW(ATW_R_PLLOD) = m->pllod - 1;
    REGW(ATW_R_HFP) = m->hfp;
    REGW(ATW_R_HSY) = m->hsy;
    REGW(ATW_R_HBP) = m->hbp;
    REGW(ATW_R_HDI) = m->width;
    REGW(ATW_R_VFP) = m->vfp;
    REGW(ATW_R_VSY) = m->vsy;
    REGW(ATW_R_VBP) = m->vbp;
    REGW(ATW_R_VDI) = m->height;
    REGW(ATW_R_VMLO) = 0;
    REGW(ATW_R_VMHI) = 0;
    REGW(ATW_R_CTRL) = ATW_CTRL_8BPP;
}

static void
atwVideoOff()
{
    if (atwScreen.fb)
	REGW(ATW_R_CTRL) = 0;
}

/*
 * Colormap: PseudoColor, 256 entries, written straight into the LUT.
 */
static void
atwUpdateColormap(cmap, first, n)
    ColormapPtr	cmap;
    int		first, n;
{
    register int i;
    register Entry *pent;
    unsigned short r, g, b;

    for (i = first, pent = &cmap->red[first]; i < first + n; i++, pent++) {
	if (pent->fShared) {
	    r = pent->co.shco.red->color;
	    g = pent->co.shco.green->color;
	    b = pent->co.shco.blue->color;
	} else {
	    r = pent->co.local.red;
	    g = pent->co.local.green;
	    b = pent->co.local.blue;
	}
	LUTW(i) = (unsigned short)(((r >> 11) << 11) | ((g >> 10) << 5) | (b >> 11));
    }
}

static void
atwInstallColormap(cmap)
    ColormapPtr	cmap;
{
    ColormapPtr old = atwScreen.installedMap;

    if (cmap == old)
	return;
    if (old)
	WalkTree(old->pScreen, TellLostMap, (char *)&old->mid);
    atwScreen.installedMap = cmap;
    atwUpdateColormap(cmap, 0, cmap->pVisual->ColormapEntries);
    WalkTree(cmap->pScreen, TellGainedMap, (char *)&cmap->mid);
}

static void
atwUninstallColormap(cmap)
    ColormapPtr	cmap;
{
    if (cmap == atwScreen.installedMap) {
	Colormap defMapID = cmap->pScreen->defColormap;

	if (cmap->mid != defMapID) {
	    ColormapPtr defMap = (ColormapPtr)LookupIDByType(defMapID, RT_COLORMAP);

	    if (defMap)
		(*cmap->pScreen->InstallColormap)(defMap);
	}
    }
}

static int
atwListInstalledColormaps(pScreen, pCmapList)
    ScreenPtr	pScreen;
    Colormap	*pCmapList;
{
    *pCmapList = atwScreen.installedMap->mid;
    return 1;
}

static void
atwStoreColors(pmap, ndef, pdefs)
    ColormapPtr	pmap;
    int		ndef;
    xColorItem	*pdefs;
{
    int i;

    if (pmap != atwScreen.installedMap)
	return;
    for (i = 0; i < ndef; i++)
	atwUpdateColormap(pmap, (int)pdefs[i].pixel, 1);
}

static Bool
atwSaveScreen(pScreen, on)
    ScreenPtr	pScreen;
    Bool	on;
{
    return FALSE;
}

/*
 * Software cursor through mi; the pointer never leaves this one screen.
 */
static Bool
atwCursorOffScreen(pScreen, x, y)
    ScreenPtr	*pScreen;
    int		*x, *y;
{
    return FALSE;
}

static void
atwCrossScreen(pScreen, entering)
    ScreenPtr	pScreen;
    Bool	entering;
{
}

/*
 * The os layer selects on the ikbd fd (AddEnabledDevice); when it is
 * readable the wakeup handler drains it into mi's event queue, which
 * makes dix call ProcessInputEvents.
 */
static void
atwBlockHandler(index, blockData, pTimeout, pReadmask)
    int		index;
    pointer	blockData, pTimeout, pReadmask;
{
}

static void
atwWakeupHandler(index, blockData, result, pReadmask)
    int		index;
    pointer	blockData;
    unsigned long result;
    pointer	pReadmask;
{
    long *mask = (long *)pReadmask;

    if ((long)result > 0 && atwIkbdFd >= 0 &&
	(mask[atwIkbdFd / 32] & (1L << (atwIkbdFd % 32))))
	atwReadInput();
}

static Bool
atwScreenInit(index, pScreen, argc, argv)
    int		index;
    ScreenPtr	pScreen;
    int		argc;
    char	**argv;
{
    atwModeRec *m = atwWantedMode;

    atwScreen.mode = m;
    atwSetMode(m);
    /* the LUT is whatever the last program left: black until a colormap
     * is installed avoids a splash of the wrong colours */
    {
	int i;
	for (i = 0; i < 256; i++)
	    LUTW(i) = 0;
    }

    pScreen->SaveScreen = atwSaveScreen;
    pScreen->InstallColormap = atwInstallColormap;
    pScreen->UninstallColormap = atwUninstallColormap;
    pScreen->ListInstalledColormaps = atwListInstalledColormaps;
    pScreen->StoreColors = atwStoreColors;

    if (!cfbScreenInit(pScreen, (pointer)atwScreen.fb, m->width, m->height,
		       75, 75, m->width))
	return FALSE;

    pScreen->BlockHandler = atwBlockHandler;
    pScreen->WakeupHandler = atwWakeupHandler;

    if (!miDCInitialize(pScreen, &atwPointerScreenFuncs))
	return FALSE;

    atwScreen.installedMap = NULL;
    return cfbCreateDefColormap(pScreen);
}

void
InitOutput(pScreenInfo, argc, argv)
    ScreenInfo	*pScreenInfo;
    int		argc;
    char	**argv;
{
    int i;

    pScreenInfo->imageByteOrder = IMAGE_BYTE_ORDER;
    pScreenInfo->bitmapScanlineUnit = BITMAP_SCANLINE_UNIT;
    pScreenInfo->bitmapScanlinePad = BITMAP_SCANLINE_PAD;
    pScreenInfo->bitmapBitOrder = BITMAP_BIT_ORDER;
    pScreenInfo->numPixmapFormats = NUMFORMATS;
    for (i = 0; i < NUMFORMATS; i++)
	pScreenInfo->formats[i] = formats[i];

    if (!atwScreen.fb && !atwMapCard())
	FatalError("atw: no ATW800/2 found\n");

    if (AddScreen(atwScreenInit, argc, argv) < 0)
	FatalError("atw: screen initialisation failed\n");
    pScreenInfo->numScreens = 1;
}

void
InitInput(argc, argv)
    int		argc;
    char	**argv;
{
    DevicePtr p, k;

    p = AddInputDevice(atwMouseProc, TRUE);
    k = AddInputDevice(atwKbdProc, TRUE);
    if (!p || !k)
	FatalError("atw: cannot create the input devices\n");
    RegisterPointerDevice(p);
    RegisterKeyboardDevice(k);
    miRegisterPointerDevice(screenInfo.screens[0], p);
    if (!mieqInit(k, p))
	FatalError("atw: cannot initialise the input queue\n");
}

void
OsVendorInit()
{
}

void
AbortDDX()
{
    atwVideoOff();
}

void
ddxGiveUp()
{
    atwVideoOff();
}

/*
 * -mode <WxH>: one of the VESA timings above
 */
int
ddxProcessArgument(argc, argv, i)
    int		argc;
    char	**argv;
    int		i;
{
    atwModeRec *m;

    if (strcmp(argv[i], "-mode") == 0 && i + 1 < argc) {
	for (m = atwModes; m->name; m++)
	    if (strcmp(m->name, argv[i + 1]) == 0) {
		atwWantedMode = m;
		return 2;
	    }
	ErrorF("atw: unknown mode %s\n", argv[i + 1]);
	return 0;
    }
    return 0;
}

void
ddxUseMsg()
{
    ErrorF("-mode 640x480|800x600|1024x768|1280x1024  ATW800/2 video mode (default 640x480)\n");
}
