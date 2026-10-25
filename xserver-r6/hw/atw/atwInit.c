/*
 * atwInit.c - screen side of the ATW800/2 ddx (X11R6.3): map the card, set the
 * mode, hand the framebuffer to cfb, keep the hardware LUT in step with
 * the installed colormap - while this server is the one on show (see
 * atwVt.c: in the background it draws, but never touches the hardware).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/resource.h>

#include "atw.h"
#include "servermd.h"
#include "screenint.h"
#include "colormapst.h"
#include "resource.h"
#include "mi.h"
#include "Xatom.h"
#include "windowstr.h"
#include "cursorstr.h"
#include "propertyst.h"

extern int TellLostMap(), TellGainedMap();
extern Bool cfbScreenInit(), cfb32ScreenInit(), cfbSetVisualTypes(), cfbCreateDefColormap();

atwScreenRec	atwScreen;

/*
 * Modes: the VESA DMT 60 Hz timings. The PLL settings and the control
 * word are computed from them.
 */
#define ATW_MAXMODES	64

static atwModeRec atwModes[ATW_MAXMODES];
static int	atwNModes;

static struct atwVesa {
    int		w, h, hz, khz;		/* pixel clock in kHz */
    int		hfp, hsy, hbp, vfp, vsy, vbp;
    int		hpos, vpos;		/* sync polarity: 1 = positive */
} atwVesaModes[] = {
    {  640,  480, 60,  25175, 16,  96,  48, 10, 2, 33, 0, 0 },
    {  800,  600, 60,  40000, 40, 128,  88,  1, 4, 23, 1, 1 },
    { 1024,  768, 60,  65000, 24, 136, 160,  3, 6, 29, 0, 0 },
    { 1280, 1024, 60, 108000, 48, 112, 248,  1, 3, 38, 1, 1 },
};

/*
 * The card's PLL (Programmer's Manual, "How to set a specific timing")
 * takes a 27 MHz reference and must run at five times the pixel clock:
 * out = 27 MHz * fb / id. The output divider keeps the VCO (out * od) in
 * the FPGA PLL's 400-1200 MHz range; the largest that fits is taken.
 * id stops at 9, keeping the reference over the divider above 3 MHz.
 * Returns 0 if no setting is in range.
 */
static int
atwPll(khz, fb, id, od)
    int khz, *fb, *id, *od;
{
    long want = 5L * khz, err, best = -1;
    int f, i, o;

    for (i = 1; i <= 9; i++)
	for (f = 1; f <= 64; f++) {
	    err = 27000L * f / i - want;
	    if (err < 0)
		err = -err;
	    if (best < 0 || err < best) {
		best = err;
		*fb = f;
		*id = i;
	    }
	}
    want = 27000L * *fb / *id;		/* the clock we actually get */
    *od = 0;
    for (o = 2; o <= 16; o *= 2)
	if (want * o >= 400000L && want * o <= 1200000L)
	    *od = o;
    return *od != 0;
}

/* control word (manual): a = VTG on, b/c = positive h/v sync, d = latch
 * the PLL, fe = depth: 01 = 8 bpp through the LUT, 11 = 32 bpp (R,G,B,x,
 * measured). Every mode at 8 bpp; at 32 bpp those that fit 4 MB. */
static void
atwBuiltinModes()
{
    int i, bpp;
    atwModeRec *m;
    struct atwVesa *v;

    atwNModes = 0;
    for (bpp = 8; bpp <= 32; bpp += 24)
	for (i = 0; i < sizeof atwVesaModes / sizeof atwVesaModes[0]; i++) {
	    v = &atwVesaModes[i];
	    m = &atwModes[atwNModes];
	    if ((long)v->w * v->h * (bpp / 8) > ATW_4MB - 0x2000)
		continue;
	    if (!atwPll(v->khz, &m->pllfb, &m->pllid, &m->pllod))
		continue;
	    m->width = v->w;	m->height = v->h;
	    m->bpp = bpp;	m->hz = v->hz;
	    m->hfp = v->hfp;	m->hsy = v->hsy;	m->hbp = v->hbp;
	    m->vfp = v->vfp;	m->vsy = v->vsy;	m->vbp = v->vbp;
	    m->ctrl = 0x01 | (v->hpos ? 0x02 : 0) | (v->vpos ? 0x04 : 0) | 0x08 |
		(bpp == 32 ? 0x30 : 0x10);
	    sprintf(m->name, "%dx%d", m->width, m->height);
	    atwNModes++;
	}
}
static int	atwWantW = 640, atwWantH = 480, atwWantBpp = 8;

/* depth 1, and the screen's: 8 bpp, or 32 (set by InitOutput) */
static PixmapFormatRec formats[] = {
    { 1, 1, BITMAP_SCANLINE_PAD },
    { 8, 8, BITMAP_SCANLINE_PAD },
};
#define NUMFORMATS (sizeof formats / sizeof formats[0])

#define REGW(o)	(*(volatile unsigned short *)(atwScreen.fb + ATW_OFF_VTG + (o)))
#define LUTW(i)	(*(volatile unsigned short *)(atwScreen.fb + ATW_OFF_LUT + 2 * (i)))
#define CARDL(o) (*(volatile unsigned long *)(atwScreen.fb + (o)))
#define CARDW(o) (*(volatile unsigned short *)(atwScreen.fb + (o)))

static void
atwLoadModes()
{
    atwBuiltinModes();
}

static atwModeRec *
atwFindMode(w, h, bpp)
    int w, h, bpp;
{
    int i;

    for (i = 0; i < atwNModes; i++)
	if (atwModes[i].width == w && atwModes[i].height == h &&
	    atwModes[i].bpp == bpp)
	    return &atwModes[i];	/* the first: the file lists 60 Hz first */
    return NULL;
}

static Bool
atwMapAt(fd, phys, size)
    int fd;
    unsigned long phys, size;
{
    char *p = (char *)mmap((caddr_t)0, size, PROT_READ | PROT_WRITE,
			   MAP_SHARED, fd, (off_t)phys);

    if (p == (char *)-1)
	return FALSE;
    atwScreen.fb = (volatile unsigned char *)p;
    atwScreen.phys = phys;
    atwScreen.size = size;
    return TRUE;
}

/*
 * Map the card through /dev/mem (which refuses, ENXIO, any page that
 * would bus-error) and find its size (measured on a real card): with
 * the 4 MB window decoded, register 15 = 1 through both aliases gives the
 * mirrored 2 MB layout; 3 through the lower alias the 4 MB one, whose id
 * block is at the top of the 4 MB only.
 */
static Bool
atwMapCard()
{
    int fd, i;
    char id[33];

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) {
	ErrorF("atw: cannot open /dev/mem (errno %d)\n", errno);
	return FALSE;
    }
    if (!atwScreen.active && atwMapAt(fd, ATW_PHYS_A, ATW_4MB)) {
	/* Another server is on show: finding the size by switching the
	 * layout would pull its picture from under it. Read which layout
	 * it runs instead: the id block sits at the top of the 2 MB only
	 * in the 2 MB layout (and is mirrored at the 4 MB top). */
	if (CARDL(ATW_2MB - 0xE00 + 24) == ATW_ID) {
	    atwScreen.size = ATW_2MB;
	    atwScreen.memreg = 1;
	} else {
	    atwScreen.size = ATW_4MB;
	    atwScreen.memreg = 3;
	}
    } else if (atwMapAt(fd, ATW_PHYS_A, ATW_4MB)) {
	atwScreen.size = ATW_2MB;
	CARDW(ATW_2MB - 0x800 + ATW_R_MEM) = 1;
	CARDW(ATW_4MB - 0x800 + ATW_R_MEM) = 1;
	atwScreen.memreg = 1;
	if (CARDL(ATW_2MB - 0xE00 + 24) == ATW_ID) {
	    CARDW(ATW_2MB - 0x800 + ATW_R_MEM) = 3;
	    if (CARDL(ATW_4MB - 0xE00 + 24) == ATW_ID &&
		CARDL(ATW_2MB - 0xE00 + 24) != ATW_ID) {
		atwScreen.size = ATW_4MB;
		atwScreen.memreg = 3;
	    } else {
		CARDW(ATW_4MB - 0x800 + ATW_R_MEM) = 1;
		CARDW(ATW_2MB - 0x800 + ATW_R_MEM) = 1;
	    }
	}
    } else if (!atwMapAt(fd, ATW_PHYS_C, ATW_2MB) &&
	       !atwMapAt(fd, ATW_PHYS_A, ATW_2MB)) {
	ErrorF("atw: no card at %lx or %lx (errno %d)\n", ATW_PHYS_C, ATW_PHYS_A, errno);
	return FALSE;
    } else {
	atwScreen.memreg = 1;
	REGW(ATW_R_MEM) = 1;
    }
    for (i = 0; i < 32; i++) {
	id[i] = (char)atwScreen.fb[ATW_OFF_INFO + i];
	if (id[i] != 0 && (id[i] < 32 || id[i] > 126))
	    id[i] = '.';
    }
    id[32] = 0;
    if (CARDL(ATW_OFF_INFO + 24) != ATW_ID) {
	ErrorF("atw: no FPGA id string, is the card there?\n");
	return FALSE;
    }
    ErrorF("atw: %s, %d MB\n", id, (int)(atwScreen.size >> 20));
    return TRUE;
}

/* The VTG, the way the manual's set_fpga() programs it. */
static void
atwSetMode(m)
    atwModeRec *m;
{
    REGW(ATW_R_CTRL) = 0;		/* stop the VTG */
    REGW(ATW_R_MEM) = atwScreen.memreg;
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
    /* the display start, in bytes - taken only as the VTG goes from off
     * to on, as here (measured on a V0205 card) */
    REGW(ATW_R_VMLO) = (unsigned short)(atwScreen.fboff & 0xFFFF);
    REGW(ATW_R_VMHI) = (unsigned short)(atwScreen.fboff >> 16);
    REGW(ATW_R_CTRL) = m->ctrl;
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

    if (cmap->pVisual->class != PseudoColor)
	return;			/* 32 bpp: the pixels are the colours */
    if (!atwScreen.active)
	return;			/* the LUT is the server on show's */

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

/*
 * Take the card (atwVt.c): our mode, our display start, our colours, the
 * 2D engine and the keyboard and mouse. Our screen needs no redraw - it
 * was drawn into all along.
 */
void
atwActivate()
{
    int i;

    atwScreen.active = 1;
    atwSetMode(atwScreen.mode);
    if (atwScreen.installedMap)
	atwUpdateColormap(atwScreen.installedMap, 0,
			  atwScreen.installedMap->pVisual->ColormapEntries);
    else
	for (i = 0; i < 256; i++)
	    LUTW(i) = 0;
    atwAccelOn();
    atwInputOn();
}

/* Let go of it: keys and buttons held are released to the clients (the
 * releases themselves will go to the next server), then the input and
 * the 2D engine. The registers are left for the next server to set. */
void
atwDeactivate()
{
    atwKeysReleaseAll();
    atwInputOff();
    atwScreen.active = 0;
    atwAccelOn();		/* with active clear: the CPU draws */
}

/*
 * _ATW_FRAMEBUFFER on the root window: where this screen is, for a client
 * that draws into the card itself (OpenUA's full-screen window). CARD32s:
 * physical address and size of the card window, offset of our screen in
 * it, bytes per line, width, height, bits per pixel, -vt number. Set
 * again whenever there is a new root (a server reset).
 */
extern WindowPtr *WindowTable;		/* dix: each screen's root */

static void
atwPublish()
{
    static WindowPtr done;
    CARD32 v[8];
    atwModeRec *m = atwScreen.mode;

    if (WindowTable[0] == NullWindow || WindowTable[0] == done)
	return;
    done = WindowTable[0];
    v[0] = atwScreen.phys;
    v[1] = atwScreen.size;
    v[2] = atwScreen.fboff;
    v[3] = m->width * (m->bpp / 8);
    v[4] = m->width;
    v[5] = m->height;
    v[6] = m->bpp;
    v[7] = atwScreen.vt;
    ChangeWindowProperty(WindowTable[0], MakeAtom("_ATW_FRAMEBUFFER", 16, TRUE),
			 XA_INTEGER, 32, PropModeReplace, 8L, (pointer)v, FALSE);
}

/*
 * A cursor with an empty mask shows nothing, but mi's software sprite
 * would still save and restore the pixels under it at every move - over
 * what a client drawing into the card itself has put there since. So an
 * invisible cursor is no cursor at all.
 */
static Bool (*atwMiDisplayCursor)();

static Bool
atwDisplayCursor(pScreen, pCursor)
    ScreenPtr	pScreen;
    CursorPtr	pCursor;
{
    if (pCursor && pCursor->bits->mask) {
	long n = (long)BitmapBytePad(pCursor->bits->width) * pCursor->bits->height;
	unsigned char *p = pCursor->bits->mask;

	while (n > 0 && *p == 0)
	    n--, p++;
	if (n == 0)
	    pCursor = NullCursor;
    }
    return (*atwMiDisplayCursor)(pScreen, pCursor);
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
    atwPublish();
    atwVtPoll(pTimeout);
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
    atwVtPoll((pointer)0);
}

static Bool
atwScreenInit(index, pScreen, argc, argv)
    int		index;
    ScreenPtr	pScreen;
    int		argc;
    char	**argv;
{
    atwModeRec *m = atwScreen.mode;
    pointer fb = (pointer)(atwScreen.fb + atwScreen.fboff);

    if (atwScreen.active) {
	int i;

	atwSetMode(m);
	/* the LUT is whatever the last program left: black until a
	 * colormap is installed avoids a splash of the wrong colours */
	for (i = 0; i < 256; i++)
	    LUTW(i) = 0;
    }

    pScreen->SaveScreen = atwSaveScreen;
    pScreen->InstallColormap = atwInstallColormap;
    pScreen->UninstallColormap = atwUninstallColormap;
    pScreen->ListInstalledColormaps = atwListInstalledColormaps;
    pScreen->StoreColors = atwStoreColors;

    if (m->bpp == 32) {
	VisualPtr v;
	int i;

	/* one TrueColor visual at depth 32 (cfb would also offer, and
	 * default to, DirectColor), with depth 1 for bitmaps */
	if (!cfbSetVisualTypes(1, 0, 8) ||
	    !cfbSetVisualTypes(32, 1 << TrueColor, 8))
	    return FALSE;
	if (!cfb32ScreenInit(pScreen, fb, m->width,
			     m->height, 75, 75, m->width))
	    return FALSE;
	/* The card's 32 bpp pixel is the bytes R, G, B, x (measured on a
	 * V0205 card): the 68030 reads it as 0xRRGGBBxx. That is legal for
	 * a depth 32 visual, not a depth 24 one, whose colour bits would
	 * have to be the low 24 - so the screen is depth 32. */
	for (i = 0, v = pScreen->visuals; i < pScreen->numVisuals; i++, v++)
	    if (v->class == TrueColor) {
		v->redMask   = 0xFF000000;	v->offsetRed   = 24;
		v->greenMask = 0x00FF0000;	v->offsetGreen = 16;
		v->blueMask  = 0x0000FF00;	v->offsetBlue  = 8;
		v->bitsPerRGBValue = 8;
		v->ColormapEntries = 256;
	    }
    } else if (!cfbScreenInit(pScreen, fb, m->width,
			      m->height, 75, 75, m->width))
	return FALSE;

    pScreen->BlockHandler = atwBlockHandler;
    pScreen->WakeupHandler = atwWakeupHandler;

    if (!miDCInitialize(pScreen, &atwPointerScreenFuncs))
	return FALSE;
    atwMiDisplayCursor = pScreen->DisplayCursor;
    pScreen->DisplayCursor = atwDisplayCursor;

    atwScreen.installedMap = NULL;
    atwAccelInit(m);
    return cfbCreateDefColormap(pScreen);
}

/*
 * The memory limits. Atari System V gives a process 16 MB of data
 * (RLIMIT_DATA, what brk may grow to) and 16 MB of address space in
 * all (RLIMIT_VMEM, what as_map lets it map), soft and hard limits
 * alike, and xdm's server inherits them. A 1024x768 screen at 32 bpp
 * fills that with a browser's pixmaps, and from then on R6.3's os/io.c
 * drops a client whose reply buffer cannot grow (xwd -root, a full
 * xlsfonts: "X connection to :0 broken"). Both are raised here, before
 * anything is allocated: root may raise the hard limits.
 */
static unsigned long atwDataMB = ATW_DATA_MB;

static void
atwDataLimit()
{
    static int res[] = { RLIMIT_DATA, RLIMIT_VMEM };
    struct rlimit rl;
    rlim_t want = atwDataMB << 20;
    int i;

    for (i = 0; i < 2; i++) {
	if (getrlimit(res[i], &rl) != 0 || rl.rlim_cur >= want)
	    continue;
	rl.rlim_cur = want;
	if (rl.rlim_max < want)
	    rl.rlim_max = want;
	if (setrlimit(res[i], &rl) != 0)
	    ErrorF("atw: the %s limit stays below -datalimit %lu: %s\n",
		   i ? "address space" : "data", atwDataMB, strerror(errno));
    }
}

void
InitOutput(pScreenInfo, argc, argv)
    ScreenInfo	*pScreenInfo;
    int		argc;
    char	**argv;
{
    int i;

    atwDataLimit();		/* OsVendorInit runs only with DDXOSINIT */
    atwVtInit();		/* before the card: are we on show? */
    if (!atwScreen.fb && !atwMapCard())
	FatalError("atw: no ATW800/2 found\n");
    if (!atwScreen.mode) {
	atwModeRec *m;

	atwLoadModes();
	m = atwFindMode(atwWantW, atwWantH, atwWantBpp);
	if (!m)
	    FatalError("atw: no mode %dx%d at %d bpp (-listmodes shows them)\n",
		       atwWantW, atwWantH, atwWantBpp);
	if (atwScreen.fboff + (unsigned long)m->width * m->height * m->bpp / 8
	    > ATW_FB_MAX)
	    FatalError("atw: %s at %d bpp%s needs more than the card's %d MB\n",
		       m->name, m->bpp, atwScreen.fboff ? " at that -fboffset" : "",
		       (int)(atwScreen.size >> 20));
	atwScreen.mode = m;
    }
    formats[1].depth = formats[1].bitsPerPixel = atwScreen.mode->bpp;

    pScreenInfo->imageByteOrder = IMAGE_BYTE_ORDER;
    pScreenInfo->bitmapScanlineUnit = BITMAP_SCANLINE_UNIT;
    pScreenInfo->bitmapScanlinePad = BITMAP_SCANLINE_PAD;
    pScreenInfo->bitmapBitOrder = BITMAP_BIT_ORDER;
    pScreenInfo->numPixmapFormats = NUMFORMATS;
    for (i = 0; i < NUMFORMATS; i++)
	pScreenInfo->formats[i] = formats[i];

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
    atwDataLimit();
}

void
AbortDDX()
{
    atwVtExit();
}

void
ddxGiveUp()
{
    atwVtExit();
}

/*
 * -listmodes: while the arguments are read, before dix opens its sockets,
 * so it also works while another server runs. It must not touch the
 * card: finding the 4 MB layout switches it, which blanks a running
 * 2 MB server's picture.
 */
static void
atwListModesExit()
{
    int i;
    atwModeRec *m;

    atwLoadModes();
    for (i = 0; i < atwNModes; i++) {
	m = &atwModes[i];
	ErrorF("  -mode %s -depth %d  (%d Hz)%s\n", m->name, m->bpp, m->hz,
	       (unsigned long)m->width * m->height * m->bpp / 8 > ATW_2MB - 0x1000 ?
	       "  needs 4 MB (jumpers A0+A1 closed)" : "");
    }
    exit(0);
}

/*
 * -mode WxH       one of the VESA modes
 * -depth N        its bits per pixel: 8 (the default) or 32
 * -noaccel        draw everything with the CPU (no 2D engine)
 * -listmodes      print the modes and exit
 * -vt N           switchable with other servers: this is Ctrl+Alt+FN
 * -fboffset B     our screen starts B bytes into video memory (so that
 *                 switchable servers each have their own)
 * -datalimit MB   the data limit the server is raised to (128)
 */
int
ddxProcessArgument(argc, argv, i)
    int		argc;
    char	**argv;
    int		i;
{
    if (strcmp(argv[i], "-mode") == 0 && i + 1 < argc) {
	if (sscanf(argv[i + 1], "%dx%d", &atwWantW, &atwWantH) != 2) {
	    ErrorF("atw: -mode wants WxH, not %s\n", argv[i + 1]);
	    return 0;
	}
	return 2;
    }
    if (strcmp(argv[i], "-depth") == 0 && i + 1 < argc) {
	atwWantBpp = atoi(argv[i + 1]);
	return 2;
    }
    if (strcmp(argv[i], "-noaccel") == 0) {
	atwNoAccel = 1;
	return 1;
    }
    if (strcmp(argv[i], "-vt") == 0 && i + 1 < argc) {
	atwScreen.vt = atoi(argv[i + 1]);
	if (atwScreen.vt < 1 || atwScreen.vt > 9) {
	    ErrorF("atw: -vt wants 1 to 9\n");
	    return 0;
	}
	return 2;
    }
    if (strcmp(argv[i], "-fboffset") == 0 && i + 1 < argc) {
	/* a multiple of 64: the 2D engine's fill source follows the screen */
	atwScreen.fboff = strtoul(argv[i + 1], (char **)0, 0) & ~63UL;
	return 2;
    }
    if (strcmp(argv[i], "-datalimit") == 0 && i + 1 < argc) {
	atwDataMB = strtoul(argv[i + 1], (char **)0, 0);
	return 2;
    }
    if (strcmp(argv[i], "-listmodes") == 0) {
	atwListModesExit();
	return 1;
    }
    return 0;
}

void
ddxUseMsg()
{
    ErrorF("-mode WxH              ATW800/2 video mode (default 640x480)\n");
    ErrorF("-depth N               bits per pixel, 8 or 32 (8)\n");
    ErrorF("-noaccel               no 2D engine: the CPU draws everything\n");
    ErrorF("-listmodes             list the modes and exit\n");
    ErrorF("-vt N                  switchable with Ctrl+Alt+F1..F9: this is FN\n");
    ErrorF("-fboffset B            the screen starts B bytes into video memory\n");
    ErrorF("-datalimit MB          raise the data limit to MB (%d)\n", ATW_DATA_MB);
}
