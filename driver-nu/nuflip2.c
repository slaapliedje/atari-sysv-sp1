/*
 * nuflip2 - the pattern that failed in nuflip's first form, all back to
 * back: select page 1, read CR, select page 0, read CR.  A miss is
 * classified by reading CR again after a pause: still page 1 means the
 * select was lost; page 0 means the read right after it came too soon.
 * The same for the driver's nu_curr: select page 1, read CURR, select
 * page 0, read CR.
 *
 *   cc -O -o nuflip2 nuflip2.c && ./nuflip2 [N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

static volatile unsigned char *win;	/* 0xFA0000 - 0xFBFFFF */
volatile unsigned char nu_sink;
volatile int spin;

#define RD(r)		(win[(r) << 9])
#define WR(r, v)	(nu_sink = win[0x10000 + ((r) << 9) + ((v) << 1)])
#define SPIN(n)		{ register int g_; for (g_ = (n); g_ > 0; g_--) spin++; }

int
main(int argc, char **argv)
{
	long n, i, lost[2], soon[2], other[2];
	int fd, c, c2, sta, t;
	caddr_t p;

	n = argc > 1 ? atol(argv[1]) : 200000L;
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
	for (t = 0; t < 2; t++) {
		lost[t] = soon[t] = other[t] = 0;
		for (i = 0; i < n; i++) {
			WR(0, 0x61);
			nu_sink = RD(t ? 7 : 0);
			WR(0, 0x21);
			if ((c = RD(0)) != (0x21 | sta)) {
				SPIN(50);
				c2 = RD(0);
				if (c2 == (0x61 | sta))
					lost[t]++;
				else if (c2 == (0x21 | sta))
					soon[t]++;
				else
					other[t]++;
				SPIN(50);
				WR(0, 0x21);
				SPIN(50);
			}
		}
		printf("%s, %ld rounds: select lost %ld, read too soon %ld, other %ld\n",
		    t ? "read CURR" : "read CR", n, lost[t], soon[t], other[t]);
		fflush(stdout);
	}
	WR(0, 0x21);
	return 0;
}
