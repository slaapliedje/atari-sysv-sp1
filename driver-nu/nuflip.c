/*
 * nuflip - does a NetUSBee access right after another one come through?
 * The chip stopped, three tests, each N rounds, with GAP spin turns
 * (about 0.7 us each; 0: none at all) between the two accesses:
 *
 *   A  read CR (page 1, 0x61), then select page 0: does CR say page 0?
 *   B  read CURR (0x4c, bit 6 set, as the driver's nu_curr does), then
 *      select page 0: does CR say page 0?
 *   C  write 0xff to RSAR0, then read ISR: does it read 0x80?
 *
 * A miss in A or B: the read's 1 bits came along into the write.  In C:
 * the write's into the read.
 *
 *   cc -O -o nuflip nuflip.c && ./nuflip [N [GAP]]
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>

static volatile unsigned char *win;	/* 0xFA0000 - 0xFBFFFF */
volatile unsigned char nu_sink;
volatile int spin;

#define RD(r)		(win[(r) << 9])
#define WR(r, v)	(nu_sink = win[0x10000 + ((r) << 9) + ((v) << 1)])
#define SPIN(n)		{ register int g_; for (g_ = (n); g_ > 0; g_--) spin++; }

static long seen[3][256];

static void
show(const char *what, long bad, int t)
{
	int c;

	printf("  %s: %ld wrong", what, bad);
	for (c = 0; c < 256; c++)
		if (seen[t][c])
			printf(", %02x x%ld", c, seen[t][c]);
	printf("\n");
}

int
main(int argc, char **argv)
{
	long n, i, a = 0, b = 0, c3 = 0;
	int fd, c, gap, sta, isr;
	time_t t0;
	caddr_t p;

	n = argc > 1 ? atol(argv[1]) : 200000L;
	gap = argc > 2 ? atoi(argv[2]) : 0;
	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		perror("/dev/mem");
		return 1;
	}
	p = mmap((caddr_t)0, 0x20000, PROT_READ, MAP_SHARED, fd, (off_t)0xfa0000);
	if (p == (caddr_t)-1) {
		perror("mmap");
		return 1;
	}
	win = (volatile unsigned char *)p;
	/* first touches fault and land on the page's base: harmless ones */
	nu_sink = RD(0);
	nu_sink = RD(8);
	WR(0, 0x21);
	WR(8, 0);
	WR(0, 0x21);
	SPIN(100);
	c = RD(0);
	if ((c & 0x03) == 0x02) {
		printf("the chip is running (CR %02x): stop it first\n", c);
		return 1;
	}
	sta = c & 0x02;
	SPIN(100);
	WR(0, 0x61);
	SPIN(100);
	WR(7, 0x4c);			/* CURR */
	SPIN(100);
	WR(0, 0x21);
	SPIN(100);
	isr = RD(7);
	t0 = time((time_t *)0);
	if (gap == 0) {
		for (i = 0; i < n; i++) {
			WR(0, 0x61);
			SPIN(20);
			c = RD(0);
			WR(0, 0x21);
			SPIN(20);
			if ((c = RD(0)) != (0x21 | sta)) {
				a++, seen[0][c]++;
				SPIN(20);
				WR(0, 0x21);
				SPIN(20);
			}
			WR(0, 0x61);
			SPIN(20);
			c = RD(7);
			WR(0, 0x21);
			SPIN(20);
			if ((c = RD(0)) != (0x21 | sta)) {
				b++, seen[1][c]++;
				SPIN(20);
				WR(0, 0x21);
				SPIN(20);
			}
			WR(8, 0xff);
			c = RD(7);
			if (c != isr)
				c3++, seen[2][c]++;
			SPIN(20);
		}
	} else {
		for (i = 0; i < n; i++) {
			WR(0, 0x61);
			SPIN(20);
			c = RD(0);
			SPIN(gap);
			WR(0, 0x21);
			SPIN(20);
			if ((c = RD(0)) != (0x21 | sta)) {
				a++, seen[0][c]++;
				SPIN(20);
				WR(0, 0x21);
				SPIN(20);
			}
			WR(0, 0x61);
			SPIN(20);
			c = RD(7);
			SPIN(gap);
			WR(0, 0x21);
			SPIN(20);
			if ((c = RD(0)) != (0x21 | sta)) {
				b++, seen[1][c]++;
				SPIN(20);
				WR(0, 0x21);
				SPIN(20);
			}
			WR(8, 0xff);
			SPIN(gap);
			c = RD(7);
			if (c != isr)
				c3++, seen[2][c]++;
			SPIN(20);
		}
	}
	printf("gap %d, %ld rounds in %ld s (ISR reads %02x):\n", gap, n,
	    (long)(time((time_t *)0) - t0), isr);
	show("A read CR 61, select page 0", a, 0);
	show("B read CURR 4c, select page 0", b, 1);
	show("C write ff, read ISR", c3, 2);
	WR(0, 0x21);
	return 0;
}
