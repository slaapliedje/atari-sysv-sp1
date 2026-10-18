/*
 * nudma - a remote DMA write of 8 bytes, as nu_probe's memory test does,
 * with the data-port writes in two instruction forms: does the chip count
 * all 8 (ISR RDC)?
 *
 *   A  move.b (a0),nu_sink   - memory to memory (a plain pointer)
 *   B  move.b (a0),d0; move.b d0,nu_sink   - through a register (what
 *      GCC 1.40 makes of a __volatile__ pointer)
 *
 * The register setup and the ISR polling are the same for both.
 *
 *   cc -O -o nudma nudma.c && ./nudma [N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

static unsigned char *pw;			/* plain: form A */
static __volatile__ unsigned char *vw;		/* volatile: form B */
unsigned char sinkp;
__volatile__ unsigned char nu_sink;
__volatile__ int spin;

#define RD(r)		(vw[(r) << 9])
#define WR(r, v)	(nu_sink = vw[0x10000 + ((r) << 9) + ((v) << 1)])
#define SPIN(n)		{ register int g_; for (g_ = (n); g_ > 0; g_--) spin++; }

static unsigned char data[8] = { 0xe5, 0xc0, 0x2f, 0x0a, 0x71, 0x5c, 0xbb, 0xe6 };

/* gaps (spin turns): between setup writes, after the start, between data */
static int gset, gstart, gdata;

/* nu_dma's sequence: nu_setcr (write, read back), then the rest */
static void
setup(void)
{
	WR(0, 0x22);			/* start, no DMA */
	SPIN(gset);
	nu_sink = RD(0);
	SPIN(gset);
	WR(7, 0x40);			/* clear RDC */
	SPIN(gset);
	WR(0x0a, 8);
	SPIN(gset);
	WR(0x0b, 0);
	SPIN(gset);
	WR(0x08, 0);
	SPIN(gset);
	WR(0x09, 0x40);			/* page 0x40 */
	SPIN(gset);
	WR(0, 0x12);			/* remote write */
	SPIN(gstart);
}

static int
done(void)
{
	int i;

	for (i = 0; i < 100; i++) {
		SPIN(2);
		if (RD(7) & 0x40)
			break;
	}
	SPIN(5);
	WR(7, 0x40);
	SPIN(5);
	WR(0, 0x22);			/* abort what is left */
	SPIN(5);
	return i < 100;
}

int
main(int argc, char **argv)
{
	long n, i, failA = 0, failB = 0;
	int fd, k;
	caddr_t p;

	n = argc > 1 ? atol(argv[1]) : 2000L;
	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		perror("/dev/mem");
		return 1;
	}
	p = mmap((caddr_t)0, 0x20000, PROT_READ, MAP_SHARED, fd, (off_t)0xfa0000);
	if (p == (caddr_t)-1) {
		perror("mmap");
		return 1;
	}
	pw = (unsigned char *)p;
	vw = (__volatile__ unsigned char *)p;
	/* first touches (faults land on the page base): harmless */
	nu_sink = RD(0);
	nu_sink = RD(8);
	nu_sink = RD(0x10);
	WR(0, 0x21);
	WR(8, 0);
	SPIN(100);
	WR(0, 0x21);
	SPIN(100);
	nu_sink = vw[0x12000];		/* the data page, a write of 0, no DMA */
	SPIN(100);
	WR(0x0e, 0x48);			/* DCR: bytes, FIFO 8 */
	SPIN(5);
	WR(0x0c, 0x20);			/* RCR: monitor */
	SPIN(5);
	WR(0x0d, 0x02);			/* TCR: loopback */
	SPIN(5);
	{
		static int v[][3] = { { 0, 0, 0 }, { 0, 5, 0 }, { 5, 0, 0 },
		    { 0, 0, 5 }, { 5, 5, 5 }, { -1 } };
		int j;

		for (j = 0; v[j][0] >= 0; j++) {
			gset = v[j][0], gstart = v[j][1], gdata = v[j][2];
			failA = failB = 0;
			for (i = 0; i < n; i++) {
				setup();
				for (k = 0; k < 8; k++) {
					nu_sink = vw[0x12000 + (data[k] << 1)];
					SPIN(gdata);
				}
				if (!done())
					failA++;
				setup();
				for (k = 0; k < 8; k++) {
					register int t;

					/* load into a register, then store */
					t = vw[0x12000 + (data[k] << 1)];
					nu_sink = t;
					SPIN(gdata);
				}
				if (!done())
					failB++;
			}
			printf("gaps: setup %d, after start %d, data %d: of %ld, A "
			    "incomplete %ld, B (register) incomplete %ld\n", gset,
			    gstart, gdata, n, failA, failB);
			fflush(stdout);
		}
	}
	WR(0, 0x21);
	return 0;
}
