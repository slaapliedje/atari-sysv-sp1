/*
 * atwtorture - random rectangles, odd widths and unaligned addresses:
 * forward copies, backward copies, and solid fills from a 64-byte source,
 * each checked byte for byte against a CPU model of the whole test area
 * (so a write outside the rectangle also shows). Off-screen above
 * 3.25 MB, 4 MB layout, register 15 untouched.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

static volatile unsigned char *m;
#define BLT	0x3FF900UL
#define BL(o)	(*(volatile unsigned long *)(m + BLT + (o)))
#define BW(o)	(*(volatile unsigned short *)(m + BLT + (o)))
#define AREA	0x340000UL		/* test area: 128 rows of 2048 bytes */
#define PITCH	2048
#define ROWS	128
#define PAT	0x3E0000UL		/* 64-byte fill source */
static unsigned char model[PITCH * ROWS];
static unsigned long seed = 12345;
static unsigned rnd(unsigned n) { seed = seed * 1103515245UL + 12345; return (seed >> 8) % n; }

static void run(unsigned long s, unsigned long d, int ss, int ds, int w, int h, int cmd)
{
	BL(0) = s; BL(4) = d; BW(8) = ss; BW(10) = ds;
	BW(12) = w; BW(14) = h; BW(16) = cmd;
	while (BW(16) & 1) ;
}

int main(void)
{
	int fd = open("/dev/mem", O_RDWR), t, i, r, x, bad[3] = {0,0,0}, n[3] = {0,0,0};
	m = (volatile unsigned char *)mmap(0, 0x400000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0xFEA00000UL);
	if ((long)m == -1 || *(volatile unsigned long *)(m + 0x3FF218) != 0x2063706DUL) { printf("no 4 MB layout\n"); return 1; }
	for (i = 0; i < PITCH * ROWS; i++) m[AREA + i] = model[i] = (unsigned char)rnd(256);

	for (t = 0; t < 300; t++) {
		int kind = t % 3, w = 1 + rnd(700), h = 1 + rnd(40);
		int sx = rnd(PITCH - w), sy = rnd(ROWS - h), dx = rnd(PITCH - w), dy = rnd(ROWS - h);
		unsigned char v = (unsigned char)rnd(256);
		if (kind == 0) {		/* forward copy: only safe when not moving down/right over itself */
			if (dy > sy || (dy == sy && dx > sx)) { int tx = sx, ty = sy; sx = dx; sy = dy; dx = tx; dy = ty; }
			run(AREA + sy * PITCH + sx, AREA + dy * PITCH + dx, PITCH, PITCH, w, h, 1);
			for (r = 0; r < h; r++) memmove(model + (dy + r) * PITCH + dx, model + (sy + r) * PITCH + sx, w);
		} else if (kind == 1) {		/* backward copy: dst below/right of src */
			unsigned long ls = (sy + h - 1) * PITCH + sx + w - 1, ld = (dy + h - 1) * PITCH + dx + w - 1;
			if (dy < sy || (dy == sy && dx < sx)) {
				int tx = sx, ty = sy; sx = dx; sy = dy; dx = tx; dy = ty;
				ls = (sy + h - 1) * PITCH + sx + w - 1; ld = (dy + h - 1) * PITCH + dx + w - 1;
			}
			run(AREA + ls, AREA + ld, -PITCH, -PITCH, w, h, 3);
			for (r = h - 1; r >= 0; r--) memmove(model + (dy + r) * PITCH + dx, model + (sy + r) * PITCH + sx, w);
		} else {			/* solid fill */
			for (i = 0; i < 64; i++) m[PAT + i] = v;
			run(PAT, AREA + dy * PITCH + dx, 0, PITCH, w, h, 5);
			for (r = 0; r < h; r++) memset(model + (dy + r) * PITCH + dx, v, w);
		}
		n[kind]++;
		for (i = 0; i < PITCH * ROWS; i++)
			if (m[AREA + i] != model[i]) {
				if (bad[kind]++ < 3)
					printf("test %d (%s w=%d h=%d src %d,%d dst %d,%d): first wrong byte at row %d col %d: %02x, want %02x\n",
					       t, kind == 0 ? "fwd copy" : kind == 1 ? "bwd copy" : "fill", w, h, sx, sy, dx, dy,
					       i / PITCH, i % PITCH, m[AREA + i], model[i]);
				for (; i < PITCH * ROWS; i++) m[AREA + i] = model[i];	/* resync */
				break;
			}
	}
	printf("forward copy %d/%d exact, backward copy %d/%d exact, fill %d/%d exact\n",
	       n[0] - bad[0], n[0], n[1] - bad[1], n[1], n[2] - bad[2], n[2]);
	return 0;
}
