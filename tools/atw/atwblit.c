/*
 * atwblit - check the ATW800/2 2D engine's register model on the card,
 * by reading the result back (no monitor needed). Uses only video memory
 * above 3.5 MB, which no mode up to 1024x768x32 shows, and never writes
 * register 15: safe with X running, but needs the 4 MB layout (X at
 * -depth 32 sets it). The model being checked:
 *   +0x100 src (long, card offset)   +0x104 dst (long)
 *   +0x108 src stride  +0x10A dst stride (words, signed)
 *   +0x10C width in bytes  +0x10E rows  +0x110 command:
 *   1 = copy, addresses ascending; 3 = copy descending (src/dst = the
 *   LAST byte, strides negative); 5 = fill: repeat the source row
 *   (stride 0) - bit 0 go, bit 1 backwards, bit 2 fill.
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
#define CPM	0x2063706DUL
#define ID(o)	(*(volatile unsigned long *)(m + (o)))

#define W	64
#define ROWS	4
#define STRIDE	256
#define SRC	0x380000UL
#define DST	0x390000UL

static void clear(unsigned long base)
{
	int i;
	for (i = 0; i < 0x2000; i++)
		m[base - 0x1000 + i] = 0xEE;	/* a margin either side */
}

static void pattern(unsigned long base, int rows)
{
	int r, x;
	for (r = 0; r < rows; r++)
		for (x = 0; x < W; x++)
			m[base + r * STRIDE + x] = (unsigned char)(r * 64 + x + 1);
}

static void go(unsigned long src, unsigned long dst, int ss, int ds, int cmd)
{
	volatile long spin;
	BL(0x00) = src; BL(0x04) = dst;
	BW(0x08) = (unsigned short)ss; BW(0x0A) = (unsigned short)ds;
	BW(0x0C) = W; BW(0x0E) = ROWS;
	BW(0x10) = cmd;
	for (spin = 0; spin < 200000; spin++)	/* no readable busy bit on V0205 */
		;
}

/* compare the destination with what the copy should give; report the
 * number of right bytes and whether anything outside it changed */
static void check(const char *what, int fill)
{
	int r, x, ok = 0, stray = 0;
	for (r = 0; r < ROWS; r++)
		for (x = 0; x < W; x++) {
			unsigned char want = fill ? (unsigned char)(x + 1)
						  : (unsigned char)(r * 64 + x + 1);
			ok += m[DST + r * STRIDE + x] == want;
		}
	for (x = -0x1000; x < 0x1000; x++) {
		unsigned long a = DST + x;
		int inside = x >= 0 && x < ROWS * STRIDE && x % STRIDE < W;
		if (!inside && m[a] != 0xEE) stray++;
	}
	printf("%-34s %3d/%d bytes right, %d stray bytes\n", what, ok, W * ROWS, stray);
}

int main(void)
{
	int fd = open("/dev/mem", O_RDWR);
	unsigned long last = (ROWS - 1) * STRIDE + W - 1;

	if (fd < 0) { perror("/dev/mem"); return 1; }
	m = (volatile unsigned char *)mmap(0, 0x400000, PROT_READ | PROT_WRITE,
					   MAP_SHARED, fd, (off_t)0xFEA00000UL);
	if ((long)m == -1) { perror("mmap"); return 1; }
	if (!(ID(0x3FF218) == CPM && ID(0x1FF218) != CPM)) {
		printf("the card is not in its 4 MB layout (run X at -depth 32)\n");
		return 1;
	}
	clear(SRC); pattern(SRC, ROWS); clear(DST);
	go(SRC, DST, STRIDE, STRIDE, 1);
	check("cmd 1, ascending, +stride", 0);

	clear(DST);
	go(SRC + last, DST + last, -STRIDE, -STRIDE, 3);
	check("cmd 3, descending from last byte", 0);

	clear(DST);
	go(SRC, DST, STRIDE, STRIDE, 3);
	check("cmd 3 with ascending setup (old model)", 0);

	clear(DST);
	go(SRC, DST, 0, STRIDE, 5);
	check("cmd 5, fill from one row", 1);
	return 0;
}
