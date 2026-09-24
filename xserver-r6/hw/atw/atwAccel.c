/*
 * atwAccel.c - the ATW800/2's 2D engine under cfb: on-screen copies
 * (window moves, scrolling) and solid fills (backgrounds, clears).
 *
 * The register model, verified on a real V0205 card by reading video
 * memory back (tools/atw: atwblit, atwtorture - 300 random unaligned
 * copies both ways and fills, all byte-exact):
 *
 *   VTG + 0x100  src (long, card offset)   + 0x104  dst (long)
 *   VTG + 0x108  src stride  + 0x10A  dst stride (signed words)
 *   VTG + 0x10C  width in bytes  + 0x10E  rows  + 0x110  command:
 *     1 = copy, addresses ascending
 *     3 = copy descending: src/dst name the LAST byte, strides negative
 *     5 = fill: each row gets the first 64 source bytes, then the second
 *         32-byte block repeated (src stride 0) - so a 64-byte source of
 *         one colour fills any width
 *   Every offset of the window reads back a status word; bit 0 = busy.
 *   The engine is asynchronous, so each operation here waits for it:
 *   cfb draws with the CPU in between.
 *
 * Measured: 38 MB/s copy and 58 MB/s fill, against 0.9 and 1.8 MB/s for
 * the CPU over the VME bus.
 *
 * cfb calls in through cfbScreenBlitHook / cfbScreenFillHook (cfb.h):
 * window drawables only, GXcopy, all planes.
 */
#include "atw.h"
#include "regionstr.h"

extern int (*cfbScreenBlitHook)(), (*cfbScreenFillHook)();

#define BLTL(o)	(*(volatile unsigned long *)(atwScreen.fb + ATW_OFF_VTG + 0x100 + (o)))
#define BLTW(o)	(*(volatile unsigned short *)(atwScreen.fb + ATW_OFF_VTG + 0x100 + (o)))

/* below this many bytes the register writes cost more than the CPU */
#define ATW_ACCEL_MIN	256

int		atwNoAccel;		/* -noaccel */
static int	atwBpp;			/* bytes per pixel */
static long	atwPitch;		/* bytes per line */
static unsigned long atwPatOff;		/* 64-byte fill source, 0 = none */
static unsigned long atwPatWord;
static int	atwPatValid;
static int	*atwOrder;		/* box order for a copy */
static int	atwOrderSize;

static void
atwBlit(src, dst, sstride, dstride, w, h, cmd)
    unsigned long src, dst;
    int sstride, dstride, w, h, cmd;
{
    BLTL(0x00) = src;
    BLTL(0x04) = dst;
    BLTW(0x08) = (unsigned short)sstride;
    BLTW(0x0A) = (unsigned short)dstride;
    BLTW(0x0C) = (unsigned short)w;
    BLTW(0x0E) = (unsigned short)h;
    BLTW(0x10) = (unsigned short)cmd;
    while (BLTW(0x10) & 1)
	;
}

/*
 * A copy between on-screen boxes. All the boxes move by the same offset.
 * Moving down, or right within the same rows, the engine copies each box
 * backwards (from its last byte) so a box that overlaps its own source
 * reads it before overwriting it; and the boxes are taken bottom band
 * first, and right to left within a band when moving right - cfb's own
 * ordering - so no box overwrites another's source before it is read.
 */
static int
atwScreenBlit(pSrc, pDst, prgnDst, pptSrc)
    DrawablePtr	pSrc, pDst;
    RegionPtr	prgnDst;
    DDXPointPtr	pptSrc;
{
    BoxPtr	pbox = REGION_RECTS(prgnDst);
    int		nbox = REGION_NUM_RECTS(prgnDst);
    long	bytes = 0;
    int		dx, dy, back, i, n, b0, b1, j;

    if (nbox <= 0)
	return TRUE;
    for (i = 0; i < nbox; i++)
	bytes += (long)(pbox[i].x2 - pbox[i].x1) * (pbox[i].y2 - pbox[i].y1);
    if (bytes * atwBpp < ATW_ACCEL_MIN)
	return FALSE;

    if (nbox > atwOrderSize) {
	int *o = (int *)xrealloc(atwOrder, 2 * nbox * sizeof(int));
	if (!o)
	    return FALSE;
	atwOrder = o;
	atwOrderSize = nbox;
    }
    dx = pbox[0].x1 - pptSrc[0].x;
    dy = pbox[0].y1 - pptSrc[0].y;
    back = dy > 0 || (dy == 0 && dx > 0);

    /* the bands (runs of boxes with the same y1, which a region keeps in
     * ascending y, then x): their starts go in the upper half of the
     * array. Taken bottom first when moving down; within a band, right
     * to left when moving right. */
    {
	int *band = atwOrder + nbox, nband = 0, k;

	for (b0 = 0; b0 < nbox; b0 = b1) {
	    band[nband++] = b0;
	    for (b1 = b0 + 1; b1 < nbox && pbox[b1].y1 == pbox[b0].y1; b1++)
		;
	}
	n = 0;
	for (k = 0; k < nband; k++) {
	    int bi = dy > 0 ? nband - 1 - k : k;
	    b0 = band[bi];
	    b1 = bi + 1 < nband ? band[bi + 1] : nbox;
	    for (j = 0; j < b1 - b0; j++)
		atwOrder[n++] = dx > 0 ? b1 - 1 - j : b0 + j;
	}
    }

    for (j = 0; j < nbox; j++) {
	BoxPtr	b = &pbox[atwOrder[j]];
	DDXPointPtr p = &pptSrc[atwOrder[j]];
	int	w = (b->x2 - b->x1) * atwBpp;
	int	h = b->y2 - b->y1;
	unsigned long s = p->y * atwPitch + p->x * atwBpp;
	unsigned long d = b->y1 * atwPitch + b->x1 * atwBpp;

	if (w <= 0 || h <= 0)
	    continue;
	if (back)
	    atwBlit(s + (h - 1) * atwPitch + w - 1, d + (h - 1) * atwPitch + w - 1,
		    (int)-atwPitch, (int)-atwPitch, w, h, 3);
	else
	    atwBlit(s, d, (int)atwPitch, (int)atwPitch, w, h, 1);
    }
    return TRUE;
}

/* Solid fill of on-screen boxes; fill = the pixel replicated to a long. */
static int
atwScreenFill(pDrawable, nBox, pBox, fill)
    DrawablePtr	pDrawable;
    int		nBox;
    BoxPtr	pBox;
    unsigned long fill;
{
    long	bytes = 0;
    int		i;

    if (!atwPatOff)
	return FALSE;
    for (i = 0; i < nBox; i++)
	bytes += (long)(pBox[i].x2 - pBox[i].x1) * (pBox[i].y2 - pBox[i].y1);
    if (bytes * atwBpp < ATW_ACCEL_MIN)
	return FALSE;

    if (!atwPatValid || fill != atwPatWord) {
	volatile unsigned long *pat = (volatile unsigned long *)(atwScreen.fb + atwPatOff);
	for (i = 0; i < 16; i++)
	    pat[i] = fill;
	atwPatWord = fill;
	atwPatValid = TRUE;
    }
    for (i = 0; i < nBox; i++) {
	int w = (pBox[i].x2 - pBox[i].x1) * atwBpp;
	int h = pBox[i].y2 - pBox[i].y1;

	if (w > 0 && h > 0)
	    atwBlit(atwPatOff, pBox[i].y1 * atwPitch + pBox[i].x1 * atwBpp,
		    0, (int)atwPitch, w, h, 5);
    }
    return TRUE;
}

/*
 * After the screen is set up. The fill source goes in the 8 KB below the
 * LUT, clear of the frame buffer in every mode that leaves room; a mode
 * whose frame buffer reaches it gets copies only.
 */
void
atwAccelInit(m)
    atwModeRec *m;
{
    unsigned long fbend;

    cfbScreenBlitHook = NULL;
    cfbScreenFillHook = NULL;
    if (atwNoAccel)
	return;
    atwBpp = m->bpp / 8;
    atwPitch = (long)m->width * atwBpp;
    fbend = (unsigned long)atwPitch * m->height;
    atwPatOff = atwScreen.size - 0x2000;
    if (fbend > atwPatOff)
	atwPatOff = 0;
    atwPatValid = FALSE;
    cfbScreenBlitHook = atwScreenBlit;
    cfbScreenFillHook = atwScreenFill;
    ErrorF("atw: 2D engine: copies%s\n", atwPatOff ? " and fills" : "");
}
