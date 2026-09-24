/*
 * atwtest - show the ATW800/2's 16 and 32 bpp pixel byte order on the
 * monitor. Run it from an xterm on the TT (it reads Return from there):
 *
 *   screen 1: 1024x768 at 16 bpp, 2 columns
 *   screen 2: 1024x768 at 32 bpp, 4 columns (needs the 4 MB card)
 *
 * Each column is the same picture written in a different byte order,
 * numbered by 1-4 white squares at the top: bands of red, green, blue,
 * white, then a grey ramp black -> white. The right column shows exactly
 * that. Afterwards the card goes back to X's 1024x768 8 bpp mode and the
 * 2 MB layout; run xrefresh to repaint the screen (atwtest.sh does).
 *
 * Writes the whole video memory, the VTG and register 15; never the LUT.
 */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

#define W 1024
#define H 768
#define TOP 64			/* the marker strip */
#define GAP 8			/* black between columns */

static volatile unsigned char *m;
static unsigned long vtg = 0x1FF800UL;	/* current layout's VTG */
#define R(n)	(*(volatile unsigned short *)(m + vtg + 2 * (n)))
#define REG15(o) (*(volatile unsigned short *)(m + (o)))
#define ID(o)	(*(volatile unsigned long *)(m + (o)))
#define CPM	0x2063706DUL

/* VESA 1024x768 60 Hz, all depths share the timing */
static void setmode(int ctrl)
{
	R(0) = 0;
	R(9) = 12 - 1; R(10) = 1 - 1; R(11) = 2 - 1;	/* 27 MHz * 12 = 5 x 64.8 MHz */
	R(1) = 24; R(2) = 136; R(3) = 160; R(4) = W;
	R(5) = 3;  R(6) = 6;   R(7) = 29;  R(8) = H;
	R(12) = 0; R(13) = 0;
	R(0) = ctrl;
}

/* back to what X runs: 1024x768 8 bpp through the LUT, 2 MB layout */
static void restore(void)
{
	setmode(0x19);
	REG15(0x3FF81E) = 1;		/* 4 MB layout's register first */
	REG15(0x1FF81E) = 1;
	vtg = 0x1FF800UL;
	setmode(0x19);
}

static void onsig(int s)
{
	restore();
	_exit(1);
}

/* colour of pixel (x within the column, y) */
static void colour(int y, int x, int cw, int *r, int *g, int *b)
{
	int band = (y - TOP) * 5 / (H - TOP);
	static const int c[4][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255}};

	if (band < 4) { *r = c[band][0]; *g = c[band][1]; *b = c[band][2]; }
	else *r = *g = *b = x * 255 / (cw - 1);
}

/* the marker strip: n white squares, else black */
static int marker(int y, int x, int n)
{
	return y >= 16 && y < 48 && x >= 16 && x < 16 + n * 48 && (x - 16) % 48 < 32;
}

/* build one row in RAM, then copy it down: every row of a band is the
 * same, so the card sees long word copies, not per-pixel byte writes */
static void makerow(unsigned char *row, int y, int bpp, int ncol)
{
	int cw = (W - (ncol - 1) * GAP) / ncol, x;
	unsigned char *p = row;

	for (x = 0; x < W; x++) {
		int col = x / (cw + GAP), cx = x % (cw + GAP), r, g, b;
		if (col >= ncol || cx >= cw) r = g = b = 0;
		else if (y < TOP) r = g = b = marker(y, cx, col + 1) ? 255 : 0;
		else colour(y, cx, cw, &r, &g, &b);
		if (bpp == 16) {
			unsigned short v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
			if (col == 0) { p[0] = v & 0xFF; p[1] = v >> 8; }	/* manual: little-endian */
			else          { p[0] = v >> 8;   p[1] = v & 0xFF; }	/* big-endian */
			p += 2;
		} else {
			switch (col) {
			case 0:  p[0] = b; p[1] = g; p[2] = r; p[3] = 0; break;	/* B G R x */
			case 1:  p[0] = 0; p[1] = r; p[2] = g; p[3] = b; break;	/* x R G B */
			case 2:  p[0] = r; p[1] = g; p[2] = b; p[3] = 0; break;	/* R G B x */
			default: p[0] = 0; p[1] = b; p[2] = g; p[3] = r; break;	/* x B G R */
			}
			p += 4;
		}
	}
}

static void draw(int bpp, int ncol)
{
	static unsigned long row[W];		/* 4 KB: a 32 bpp row */
	unsigned long line = (unsigned long)W * (bpp / 8);
	int y, lastkey = -1;

	for (y = 0; y < H; y++) {
		/* rows only differ between marker rows and bands */
		int key = y < TOP ? (y >= 16 && y < 48) : 2 + (y - TOP) * 5 / (H - TOP);
		volatile unsigned long *d = (volatile unsigned long *)(m + y * line);
		unsigned long i;
		if (key != lastkey) {
			makerow((unsigned char *)row, y, bpp, ncol);
			lastkey = key;
		}
		for (i = 0; i < line / 4; i++)
			d[i] = row[i];
	}
}

static void waitret(const char *what)
{
	char buf[80];
	printf("%s\n  -> press Return for the next screen\n", what);
	fflush(stdout);
	fgets(buf, sizeof buf, stdin);
}

int main(void)
{
	int fd = open("/dev/mem", O_RDWR), four;

	if (fd < 0) { perror("/dev/mem"); return 1; }
	m = (volatile unsigned char *)mmap(0, 0x400000, PROT_READ | PROT_WRITE,
					   MAP_SHARED, fd, (off_t)0xFEA00000UL);
	if ((long)m == -1) { perror("mmap 4 MB at FEA00000 (jumpers open?)"); return 1; }
	signal(SIGINT, onsig); signal(SIGTERM, onsig); signal(SIGHUP, onsig);

	REG15(0x1FF81E) = 1; REG15(0x3FF81E) = 1;	/* the 2 MB layout */
	REG15(0x1FF81E) = 3;
	four = ID(0x3FF218) == CPM && ID(0x1FF218) != CPM;
	if (four)
		vtg = 0x3FF800UL;
	else
		REG15(0x1FF81E) = 1;

	setmode(0x29);
	draw(16, 2);
	waitret("16 bpp: which column (1 or 2 squares) shows red, green, blue, white, grey ramp?");
	if (four) {
		setmode(0x39);
		draw(32, 4);
		waitret("32 bpp: which column (1-4 squares) shows red, green, blue, white, grey ramp?");
	} else
		printf("(not a 4 MB layout: no 32 bpp screen)\n");
	restore();
	printf("back to 8 bpp\n");
	return 0;
}
