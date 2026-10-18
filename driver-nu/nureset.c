/*
 * nureset - nu_reset reads the reset port and writes it straight after:
 * two strokes close together.  Does the chip come out of that whole?
 * Four ways, each followed by 25 ms, DCR/RCR/TCR set, a remote DMA write
 * of 8 bytes and DCR read back on page 2:
 *
 *   0  read, write at once (nu_reset, inline)
 *   1  read, 20 spin turns, write (the function build's spacing, roughly)
 *   2  read only
 *   3  write only
 *
 *   cc -O -o nureset nureset.c && ./nureset [N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/mman.h>

static __volatile__ unsigned char *vw;
__volatile__ unsigned char nu_sink;
__volatile__ int spin;

#define RD(r)		(vw[(r) << 9])
#define WR(r, v)	(nu_sink = vw[0x10000 + ((r) << 9) + ((v) << 1)])
#define SPIN(n)		{ register int g_; for (g_ = (n); g_ > 0; g_--) spin++; }

static unsigned char data[8] = { 0xe5, 0xc0, 0x2f, 0x0a, 0x71, 0x5c, 0xbb, 0xe6 };

/* after the reset: the probe's setup, a DMA write, DCR read back */
static int
check(int *dcr)
{
	int i, k, ok;

	WR(0, 0x21);
	SPIN(5);
	WR(0x0e, 0x48);
	WR(0x0c, 0x20);
	WR(0x0d, 0x02);
	WR(0x0f, 0);
	WR(0x07, 0xff);
	WR(0, 0x22);
	nu_sink = RD(0);
	WR(7, 0x40);
	WR(0x0a, 8);
	WR(0x0b, 0);
	WR(0x08, 0);
	WR(0x09, 0x40);
	WR(0, 0x12);
	for (k = 0; k < 8; k++)
		nu_sink = vw[0x12000 + (data[k] << 1)];
	for (i = 0; i < 1000 && !(RD(7) & 0x40); i++)
		;
	ok = i < 1000;
	WR(7, 0x40);
	SPIN(5);
	WR(0, 0xa1);			/* page 2, stopped */
	SPIN(5);
	*dcr = RD(0x0e) & 0x7f;
	SPIN(5);
	WR(0, 0x21);
	SPIN(5);
	return ok;
}

int
main(int argc, char **argv)
{
	long n, i;
	int fd, how, dcr, x, dmabad, dcrbad, seen;
	caddr_t p;

	n = argc > 1 ? atol(argv[1]) : 40L;
	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		perror("/dev/mem");
		return 1;
	}
	p = mmap((caddr_t)0, 0x20000, PROT_READ, MAP_SHARED, fd, (off_t)0xfa0000);
	if (p == (caddr_t)-1) {
		perror("mmap");
		return 1;
	}
	vw = (__volatile__ unsigned char *)p;
	/* first touches: each page's fault lands on its base */
	nu_sink = RD(0);
	nu_sink = RD(8);
	nu_sink = RD(0x10);
	nu_sink = RD(0x18);		/* reset */
	WR(0, 0x21);
	WR(8, 0);
	nu_sink = vw[0x12000];
	nu_sink = vw[0x13000];		/* reset */
	(void)poll((struct pollfd *)0, 0, 30);
	for (how = 0; how < 4; how++) {
		dmabad = dcrbad = 0;
		seen = -1;
		for (i = 0; i < n; i++) {
			switch (how) {
			case 0:
				x = RD(0x1f);
				WR(0x1f, x);
				break;
			case 1:
				x = RD(0x1f);
				SPIN(20);
				WR(0x1f, x);
				break;
			case 2:
				nu_sink = RD(0x1f);
				break;
			case 3:
				WR(0x1f, 0xff);
				break;
			}
			(void)poll((struct pollfd *)0, 0, 25);
			if (!check(&dcr))
				dmabad++;
			if (dcr != 0x48) {
				dcrbad++;
				seen = dcr;
			}
		}
		printf("reset way %d: %ld rounds, DMA incomplete %d, DCR wrong %d",
		    how, n, dmabad, dcrbad);
		if (seen >= 0)
			printf(" (read %02x)", seen);
		printf("\n");
		fflush(stdout);
	}
	return 0;
}
