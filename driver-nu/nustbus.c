/*
 * nustbus - does an ST-bus cycle right after a NetUSBee register write
 * spoil it?  The kernel (code and data in ST-RAM) fails to set the chip up
 * in every way tried, user programs (TT-RAM) do not.  The chip stopped:
 * write RCR then TCR (0 or 2 in turn), with or without a read of ST-RAM
 * (physical page 0, the vector table) straight after each write; then
 * read TCR back slowly on page 2.  Also DCR (0x48 / 0x40).
 *
 *   cc -O -o nustbus nustbus.c && ./nustbus [N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

static __volatile__ unsigned char *win;	/* 0xFA0000 - 0xFBFFFF */
static __volatile__ unsigned char *st;	/* ST-RAM, physical 0 - 0xFFF */
__volatile__ unsigned char nu_sink, st_sink;
__volatile__ int spin;

#define RD(r)		(win[(r) << 9])
#define WR(r, v)	(nu_sink = win[0x10000 + ((r) << 9) + ((v) << 1)])
#define SPIN(n)		{ register int g_; for (g_ = (n); g_ > 0; g_--) spin++; }
#define STREAD()	(st_sink = st[0x100])

static int
p2(int r, int mask)
{
	int t;

	SPIN(30);
	WR(0, 0xa1);			/* page 2, stopped */
	SPIN(30);
	t = RD(r) & mask;
	SPIN(30);
	WR(0, 0x21);
	SPIN(30);
	return t;
}

int
main(int argc, char **argv)
{
	long n, i, bad[2][2];
	int fd, s, want, dwant;
	caddr_t p, q;

	n = argc > 1 ? atol(argv[1]) : 20000L;
	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		perror("/dev/mem");
		return 1;
	}
	p = mmap((caddr_t)0, 0x20000, PROT_READ, MAP_SHARED, fd, (off_t)0xfa0000);
	q = mmap((caddr_t)0, 0x1000, PROT_READ, MAP_SHARED, fd, (off_t)0);
	if (p == (caddr_t)-1 || q == (caddr_t)-1) {
		perror("mmap");
		return 1;
	}
	win = (__volatile__ unsigned char *)p;
	st = (__volatile__ unsigned char *)q;
	STREAD();
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
	for (s = 0; s < 2; s++) {
		bad[s][0] = bad[s][1] = 0;
		for (i = 0; i < n; i++) {
			want = i & 1 ? 0x02 : 0x00;
			dwant = i & 1 ? 0x48 : 0x40;
			if (s) {
				WR(0x0c, 0x20);
				STREAD();
				WR(0x0d, want);
				STREAD();
				WR(0x0e, dwant);
				STREAD();
			} else {
				WR(0x0c, 0x20);
				WR(0x0d, want);
				WR(0x0e, dwant);
			}
			if (p2(0x0d, 0x1f) != want)
				bad[s][0]++;
			if (p2(0x0e, 0x7f) != dwant)
				bad[s][1]++;
		}
		printf("%s: TCR wrong %ld, DCR wrong %ld, of %ld\n", s ?
		    "with an ST-RAM read after each write" : "writes alone       ",
		    bad[s][0], bad[s][1], n);
		fflush(stdout);
	}
	WR(0x0e, 0x48);
	SPIN(30);
	WR(0x0d, 0);
	return 0;
}
