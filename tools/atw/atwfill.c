/* atwfill - does the fill command (5) repeat a 64-byte source across a
 * wider row? Off-screen above 3.25 MB, 4 MB layout, reg 15 untouched. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
static volatile unsigned char *m;
#define BLT	0x3FF900UL
#define BL(o)	(*(volatile unsigned long *)(m + BLT + (o)))
#define BW(o)	(*(volatile unsigned short *)(m + BLT + (o)))
#define SRC	0x340000UL
#define DST	0x380000UL
static void test(int srcw, int w, int rows)
{
	int r, x, tiled = 0, fromsrc = 0, untouched = 0;
	for (x = 0; x < 4096; x++) m[SRC + x] = (unsigned char)(x < srcw ? x + 1 : 0xAA);
	for (x = 0; x < rows * 4096; x++) m[DST + x] = 0xEE;
	BL(0) = SRC; BL(4) = DST; BW(8) = 0; BW(10) = 4096;
	BW(12) = w; BW(14) = rows; BW(16) = 5;
	while (BW(16) & 1) ;
	for (r = 0; r < rows; r++)
		for (x = 0; x < w; x++) {
			unsigned char v = m[DST + r * 4096 + x];
			tiled += v == (unsigned char)(x % srcw + 1);
			fromsrc += v == m[SRC + x];
			untouched += v == 0xEE;
		}
	if (srcw == 64 && w == 1024)
		for (r = 0; r < rows; r++) {
			printf("row %d:", r);
			for (x = 0; x < w; x += 64)
				printf(" %02x%02x", m[DST + r * 4096 + x], m[DST + r * 4096 + x + 63]);
			printf("\n");
		}
	printf("src %2d bytes, fill %4d x %d: %5d match a %d-byte tiling, %5d match the source row itself, %5d untouched (of %d)\n",
	       srcw, w, rows, tiled, srcw, fromsrc, untouched, w * rows);
}
int main(void)
{
	int fd = open("/dev/mem", O_RDWR);
	m = (volatile unsigned char *)mmap(0, 0x400000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0xFEA00000UL);
	if ((long)m == -1 || *(volatile unsigned long *)(m + 0x3FF218) != 0x2063706DUL) { printf("no 4 MB layout\n"); return 1; }
	test(64, 1024, 4);
	return 0;
}
