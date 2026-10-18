/*
 * nuww - two NetUSBee register writes in a row: does the second one
 * land?  nu_init's DCR, RBCR, RCR, TCR writes went wrong when inline (as
 * in the kernel) and not through function calls.  The chip stopped: write
 * RCR, then TCR (0 or 2 in turn) after a gap of GAP spin turns (0: none),
 * then read TCR back slowly on page 2.  Also a run of three: RBCR1, RCR,
 * TCR.
 *
 *   cc -O -o nuww nuww.c && ./nuww [N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

static __volatile__ unsigned char *win;	/* 0xFA0000 - 0xFBFFFF */
__volatile__ unsigned char nu_sink;
__volatile__ int spin;

#define RD(r)		(win[(r) << 9])
#define WR(r, v)	(nu_sink = win[0x10000 + ((r) << 9) + ((v) << 1)])
#define SPIN(n)		{ register int g_; for (g_ = (n); g_ > 0; g_--) spin++; }

static int
tcr(void)
{
	int t;

	SPIN(30);
	WR(0, 0xa1);			/* page 2, stopped */
	SPIN(30);
	t = RD(0x0d) & 0x1f;
	SPIN(30);
	WR(0, 0x21);
	SPIN(30);
	return t;
}

int
main(int argc, char **argv)
{
	static int gaps[] = { 0, 1, 2, 4, 8, -1 };
	long n, i, bad2, bad3;
	int fd, g, gap, want;
	caddr_t p;

	n = argc > 1 ? atol(argv[1]) : 20000L;
	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		perror("/dev/mem");
		return 1;
	}
	p = mmap((caddr_t)0, 0x20000, PROT_READ, MAP_SHARED, fd, (off_t)0xfa0000);
	if (p == (caddr_t)-1) {
		perror("mmap");
		return 1;
	}
	win = (__volatile__ unsigned char *)p;
	nu_sink = RD(0);
	nu_sink = RD(8);
	WR(0, 0x21);
	WR(8, 0);
	SPIN(100);
	WR(0, 0x21);
	SPIN(100);
	if ((RD(0) & 0x03) == 0x02) {
		printf("the chip is running: stop it first\n");
		return 1;
	}
	for (g = 0; (gap = gaps[g]) >= 0; g++) {
		bad2 = bad3 = 0;
		for (i = 0; i < n; i++) {
			want = i & 1 ? 0x02 : 0x00;
			if (gap == 0) {
				WR(0x0c, 0x20);
				WR(0x0d, want);
			} else {
				WR(0x0c, 0x20);
				SPIN(gap);
				WR(0x0d, want);
			}
			if (tcr() != want)
				bad2++;
			want ^= 0x02;
			if (gap == 0) {
				WR(0x0b, 0);
				WR(0x0c, 0x20);
				WR(0x0d, want);
			} else {
				WR(0x0b, 0);
				SPIN(gap);
				WR(0x0c, 0x20);
				SPIN(gap);
				WR(0x0d, want);
			}
			if (tcr() != want)
				bad3++;
		}
		printf("gap %d: second of two writes wrong %ld of %ld, third of three "
		    "wrong %ld of %ld\n", gap, bad2, n, bad3, n);
		fflush(stdout);
	}
	WR(0x0d, 0);
	return 0;
}
