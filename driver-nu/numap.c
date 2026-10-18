/*
 * numap - the same remote DMA read as tt/dmaread.sh, but through an
 * mmap(2) of /dev/mem, so that each access is one load: does the chip
 * see one read strobe per CPU bus cycle?
 *
 *   cc -O -o numap numap.c && ./numap
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

static volatile unsigned char *win;	/* 0xFA0000 - 0xFBFFFF */
volatile unsigned char nu_sink;	/* cc drops a load whose value goes nowhere */

/* every read at the same address now */
#define RD(r)		(win[(r) << 9])
#define WR(r, v)	(nu_sink = win[0x10000 + ((r) << 9) + ((v) << 1)])

/*
 * The first touch of each page of the mapping takes a fault, and the
 * card sees three accesses: make each a harmless one, stopped, before
 * anything that counts.  4 KB pages: registers 0-7, 8-15, 16-23 (data),
 * 24-31 (reset) in each window.
 */
static void
prefault(void)
{
	WR(0, 0x21);		/* stop, no DMA */
	nu_sink = RD(0);
	nu_sink = RD(8);	/* CLDA0 */
	WR(8, 0);		/* RSAR0 */
	nu_sink = RD(0x10);	/* data port, no DMA running */
	WR(0x10, 0);
	nu_sink = RD(0x1f);	/* reset */
	WR(0x1f, 0);
}

int
main(void)
{
	int fd, i, c1, c2, c3;
	caddr_t p;
	unsigned char d[16], a0[16], a1[16];

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
	prefault();
	WR(7, 0xff);
	c1 = RD(0);
	WR(0, 0x61);
	c2 = RD(0);
	WR(0, 0x21);
	c3 = RD(0);
	printf("CR %02x %02x %02x (21 61 21: uncached)\n", c1, c2, c3);

	/* remote read of the PROM: 16 bytes at 0 */
	WR(0, 0x22);
	WR(7, 0x40);
	WR(0x0a, 16);
	WR(0x0b, 0);
	WR(8, 0x00);
	WR(9, 0x00);
	WR(0, 0x0a);
	printf("CR %02x CRDA %02x%02x before\n", RD(0), RD(9), RD(8));
	for (i = 0; i < 16; i++) {
		d[i] = RD(0x10);
		a1[i] = RD(9);
		a0[i] = RD(8);
	}
	for (i = 0; i < 16; i++)
		printf("%02x/%02x%02x ", d[i], a1[i], a0[i]);
	printf("\nISR %02x CR %02x\n", RD(7), RD(0));
	WR(0, 0x21);
	return 0;
}
