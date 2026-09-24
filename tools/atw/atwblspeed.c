/* atwblspeed - 2D engine copy/fill throughput, waiting on the busy bit
 * (bit 0 of the status word the register window reads back), against the
 * CPU. Off-screen above 3.25 MB, 4 MB layout, register 15 untouched. */
#include <stdio.h>
#include <time.h>
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
#define KB	(WB * ROWS / 1024)

static long spins;
static void blit(unsigned long s, unsigned long d, int ss, int ds, int cmd)
{
	BL(0) = s; BL(4) = d; BW(8) = ss; BW(10) = ds;
	BW(12) = WB; BW(14) = ROWS; BW(16) = cmd;
	while (BW(16) & 1)
		spins++;
}

int main(void)
{
	int fd = open("/dev/mem", O_RDWR), i, n, r;
	time_t t0, t1;
	unsigned long k, bad = 0;

	m = (volatile unsigned char *)mmap(0, 0x400000, PROT_READ | PROT_WRITE,
					   MAP_SHARED, fd, (off_t)0xFEA00000UL);
	if ((long)m == -1 || *(volatile unsigned long *)(m + 0x3FF218) != 0x2063706DUL) {
		printf("no 4 MB layout\n"); return 1;
	}
	for (k = 0; k < WB * ROWS; k += 4)
		*(volatile unsigned long *)(m + SRC + k) = k * 2654435761UL;

	blit(SRC, DST, WB, WB, 1);
	for (k = 0; k < WB * ROWS; k += 4)
		bad += *(volatile unsigned long *)(m + DST + k) != k * 2654435761UL;
	printf("copy after busy-wait: %lu wrong longs of %d\n", bad, WB * ROWS / 4);

	for (t0 = time(0); time(0) == t0; ) ;		/* align to a second */
	t0 = time(0); n = 0; spins = 0;
	while (time(0) - t0 < 5) { blit(SRC, DST, WB, WB, 1); n++; }
	printf("blitter copy: %d KB/s (%ld busy polls per blit)\n", n * KB / 5, spins / (n ? n : 1));

	for (t0 = time(0); time(0) == t0; ) ;
	t0 = time(0); n = 0;
	while (time(0) - t0 < 5) { blit(SRC, DST, 0, WB, 5); n++; }
	printf("blitter fill: %d KB/s\n", n * KB / 5);

	for (t0 = time(0); time(0) == t0; ) ;
	t0 = time(0); n = 0;
	while (time(0) - t0 < 5) {
		for (r = 0; r < ROWS; r++) {
			volatile unsigned long *s = (volatile unsigned long *)(m + SRC + r * WB);
			volatile unsigned long *d = (volatile unsigned long *)(m + DST + r * WB);
			for (i = 0; i < WB / 4; i++) d[i] = s[i];
		}
		n++;
	}
	printf("CPU copy:     %d KB/s\n", n * KB / 5);

	for (t0 = time(0); time(0) == t0; ) ;
	t0 = time(0); n = 0;
	while (time(0) - t0 < 5) {
		for (r = 0; r < ROWS; r++) {
			volatile unsigned long *d = (volatile unsigned long *)(m + DST + r * WB);
			for (i = 0; i < WB / 4; i++) d[i] = 0x12345678;
		}
		n++;
	}
	printf("CPU fill:     %d KB/s\n", n * KB / 5);
	return 0;
}
