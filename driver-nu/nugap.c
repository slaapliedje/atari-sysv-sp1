/*
 * nugap - do tight sequences of cartridge accesses reach the NetUSBee's
 * chip as meant?  The kernel driver lost page switches: a page-0 select
 * stayed on page 1, and its value turned up in PAR3.  This repeats the
 * driver's patterns through an mmap of /dev/mem, the chip stopped, with
 * a gap of G spins between accesses, and counts what lands wrong.
 *
 *   cc -O -o nugap nugap.c && ./nugap [N [BASE]]
 *
 * BASE (hex): where to map the card, fa0000 or fffa0000 (the mirror of
 * the ST space at the top, which the kernel driver uses).
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/mman.h>

static volatile unsigned char *win;	/* 0xFA0000 - 0xFBFFFF */
volatile unsigned char nu_sink;
volatile int spin;
static int gap;

#define RD(r)		(win[(r) << 9])
#define WR(r, v)	(nu_sink = win[0x10000 + ((r) << 9) + ((v) << 1)])
#define GAP()		{ register int g_; for (g_ = gap; g_ > 0; g_--) spin++; }

static void
msleep(int ms)
{
	(void)poll((struct pollfd *)0, 0, ms);
}

/* as nuuser: each page's first touch, harmless; leaves the chip reset */
static void
prefault(void)
{
	WR(0, 0x21);
	nu_sink = RD(0);
	nu_sink = RD(8);
	WR(8, 0);
	nu_sink = RD(0x10);
	WR(0x10, 0);
	nu_sink = RD(0x1f);
	WR(0x1f, 0);
	msleep(30);
}

static unsigned char mac[6] = { 0x00, 0x4f, 0xd3, 0xde, 0xb6, 0x9c };

static void
setpar(void)
{
	int i;

	WR(0, 0x61);
	for (i = 0; i < 6; i++)
		WR(1 + i, mac[i]);
	WR(7, 0x4c);			/* CURR */
	WR(0, 0x21);
}

static int
checkpar(const char *what)
{
	int i, bad = 0;
	unsigned char p[7];

	WR(0, 0x61);
	for (i = 0; i < 7; i++)
		p[i] = RD(1 + i);
	WR(0, 0x21);
	for (i = 0; i < 6; i++)
		if (p[i] != mac[i])
			bad++;
	if (p[6] != 0x4c)
		bad++;
	if (bad)
		printf("  %s: PAR %02x %02x %02x %02x %02x %02x CURR %02x\n", what,
		    p[0], p[1], p[2], p[3], p[4], p[5], p[6]);
	return bad;
}

/* remote DMA, as nuchip.c's nu_dma: n bytes at a; cmd 0x08 read, 0x10 write */
static void
dma(int a, int n, int cmd)
{
	WR(0, 0x22);
	WR(7, 0x40);
	WR(10, n & 0xff);
	WR(11, n >> 8);
	WR(8, a & 0xff);
	WR(9, a >> 8);
	WR(0, cmd | 0x02);
}

static int
dmadone(void)
{
	int i;

	for (i = 0; i < 1000 && !(RD(7) & 0x40); i++)
		;
	WR(7, 0x40);
	return i < 1000;
}

/* chip started but storing nothing: monitor mode, loopback */
static void
quiet(void)
{
	WR(0, 0x21);
	WR(14, 0x48);			/* DCR: byte-wide, FIFO 8 */
	WR(12, 0x20);			/* RCR: monitor */
	WR(13, 0x02);			/* TCR: loopback */
	WR(15, 0);			/* IMR */
	WR(7, 0xff);
}

/* 4: the PROM, 32 bytes read back to back; ref from a slow read */
static int
promtest(unsigned char *ref, int n)
{
	unsigned char b[32];
	int i, k, bad = 0, nodone = 0;

	for (k = 0; k < n; k++) {
		dma(0, 32, 0x08);
		for (i = 0; i < 32; i++) {
			b[i] = RD(0x10);
			GAP();
		}
		if (!dmadone())
			nodone++;
		for (i = 0; i < 32; i++)
			if (b[i] != ref[i])
				break;
		if (i < 32 && bad++ < 3) {
			printf("  prom %d:", k);
			for (i = 0; i < 32; i++)
				printf(" %02x", b[i]);
			printf("\n");
		}
	}
	if (nodone)
		printf("  prom: %d reads without remote DMA complete\n", nodone);
	return bad;
}

/* 5: 256 bytes of buffer memory written and read back to back */
static int
memtest(int n)
{
	unsigned char w[256], r[256];
	int i, k, bad = 0, nodone = 0;

	for (k = 0; k < n; k++) {
		for (i = 0; i < 256; i++)
			w[i] = (i * 13 + k) ^ 0x5a;
		dma(0x5000, 256, 0x10);
		for (i = 0; i < 256; i++) {
			WR(0x10, w[i]);
			GAP();
		}
		if (!dmadone())
			nodone++;
		dma(0x5000, 256, 0x08);
		for (i = 0; i < 256; i++) {
			r[i] = RD(0x10);
			GAP();
		}
		if (!dmadone())
			nodone++;
		for (i = 0; i < 256; i++)
			if (r[i] != w[i])
				break;
		if (i < 256 && bad++ < 3)
			printf("  mem %d: byte %d wrote %02x read %02x\n", k, i,
			    w[i], r[i]);
	}
	if (nodone)
		printf("  mem: %d transfers without remote DMA complete\n", nodone);
	return bad;
}

int
main(int argc, char **argv)
{
	int fd, n, i, g, c, cr, badcr, badpar, badrb, badprom, badmem;
	unsigned long base;
	unsigned char ref[32];
	static int gaps[] = { 0, 1, 4, 16, -1 };
	caddr_t p;

	n = argc > 1 ? atoi(argv[1]) : 20000;
	base = argc > 2 ? strtoul(argv[2], (char **)0, 16) : 0xfa0000;
	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		perror("/dev/mem");
		return 1;
	}
	p = mmap((caddr_t)0, 0x20000, PROT_READ, MAP_SHARED, fd, (off_t)base);
	if (p == (caddr_t)-1) {
		perror("mmap");
		return 1;
	}
	win = (volatile unsigned char *)p;
	prefault();
	WR(0, 0x21);
	printf("card at %08lx: CR %02x, ID %02x %02x\n", base, RD(0), RD(10),
	    RD(11));
	if (RD(10) != 0x50 || RD(11) != 0x70) {
		printf("no RTL8019AS\n");
		return 1;
	}
	/* the PROM read slowly, a byte at a time, as the reference */
	quiet();
	gap = 64;
	dma(0, 32, 0x08);
	for (i = 0; i < 32; i++) {
		ref[i] = RD(0x10);
		GAP();
	}
	(void)dmadone();
	printf("PROM");
	for (i = 0; i < 32; i++)
		printf(" %02x", ref[i]);
	printf("\n");
	for (g = 0; gaps[g] >= 0; g++) {
		gap = gaps[g];
		WR(0, 0x21);
		setpar();
		badcr = badpar = badrb = 0;
		/* 1: nu_curr: page 1, read CURR, back to page 0 */
		for (i = 0; i < n; i++) {
			WR(0, 0x61);
			GAP();
			nu_sink = RD(7);
			GAP();
			WR(0, 0x21);
			GAP();
			cr = RD(0);
			GAP();
			if (cr != 0x21) {
				if (badcr++ < 5)
					printf("  flip %d: CR %02x\n", i, cr);
				WR(0, 0x21);
			}
		}
		badpar += checkpar("after flips");
		setpar();
		/* 2: write, then read back, a page-0 register (RSAR0) */
		for (i = 0; i < n; i++) {
			c = i & 0xff;
			WR(8, c);
			GAP();
			cr = RD(0);		/* CR between, as the driver does */
			GAP();
			WR(0, 0x61);
			GAP();
			WR(0, 0x21);
			GAP();
			if (cr != 0x21) {
				if (badcr++ < 5)
					printf("  w/r %d: CR %02x\n", i, cr);
				WR(0, 0x21);
			}
		}
		badpar += checkpar("after writes");
		/* 3: CLDA0 is not writable; RSAR is write-only: use PAR3 instead */
		for (i = 0; i < n; i++) {
			c = (i * 7) & 0xff;
			WR(0, 0x61);
			GAP();
			WR(4, c);
			GAP();
			cr = RD(4);
			GAP();
			WR(0, 0x21);
			GAP();
			if (cr != c && badrb++ < 5)
				printf("  par3 %d: wrote %02x read %02x\n", i, c, cr);
		}
		setpar();
		badpar += checkpar("end");
		quiet();
		badprom = promtest(ref, n / 20);
		badmem = memtest(n / 100);
		WR(0, 0x21);
		printf("gap %2d: %d rounds each: CR wrong %d, PAR/CURR wrong %d, "
		    "PAR3 read-back wrong %d; PROM reads wrong %d of %d, "
		    "memory blocks wrong %d of %d\n", gap, n, badcr, badpar, badrb,
		    badprom, n / 20, badmem, n / 100);
		fflush(stdout);
	}
	WR(0, 0x21);
	return 0;
}
