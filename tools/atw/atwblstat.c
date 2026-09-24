/* atwblstat - sample the 2D engine's register window while a 256 KB copy
 * runs, next to the moment its last long lands. Off-screen, 4 MB layout. */
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
#define WB	1024
#define ROWS	256
#define NS	4000

static unsigned short s10[NS], s0e[NS], s12[NS];
static unsigned char done[NS];

int main(void)
{
	int fd = open("/dev/mem", O_RDWR), i, first = -1;
	unsigned long k, last = WB * ROWS - 4, want;

	m = (volatile unsigned char *)mmap(0, 0x400000, PROT_READ | PROT_WRITE,
					   MAP_SHARED, fd, (off_t)0xFEA00000UL);
	if ((long)m == -1 || *(volatile unsigned long *)(m + 0x3FF218) != 0x2063706DUL) {
		printf("no 4 MB layout\n"); return 1;
	}
	for (k = 0; k < WB * ROWS; k += 4)
		*(volatile unsigned long *)(m + SRC + k) = k * 2654435761UL;
	want = last * 2654435761UL;
	*(volatile unsigned long *)(m + DST + last) = 0;
	printf("before: +10=%04x +0e=%04x +12=%04x\n", BW(0x10), BW(0x0E), BW(0x12));
	BL(0) = SRC; BL(4) = DST; BW(8) = WB; BW(10) = WB;
	BW(12) = WB; BW(14) = ROWS; BW(16) = 1;
	for (i = 0; i < NS; i++) {
		s10[i] = BW(0x10); s0e[i] = BW(0x0E); s12[i] = BW(0x12);
		done[i] = *(volatile unsigned long *)(m + DST + last) == want;
		if (done[i] && first < 0) first = i;
	}
	/* print only where something changes */
	for (i = 0; i < NS; i++)
		if (i == 0 || s10[i] != s10[i-1] || s0e[i] != s0e[i-1] || s12[i] != s12[i-1] || done[i] != done[i-1])
			printf("sample %4d: +10=%04x +0e=%04x +12=%04x  last long %s\n",
			       i, s10[i], s0e[i], s12[i], done[i] ? "DONE" : "not yet");
	printf("destination complete at sample %d of %d\n", first, NS);
	return 0;
}
