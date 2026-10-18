/*
 * nupage - how soon after a CR write (a page select) does the NetUSBee's
 * RTL8019AS act on the new page?  nugap found a register read right
 * after selecting page 1 still answered from page 0, while the same read
 * a few microseconds later did not.  The chip must be stopped and carry
 * its station address in PAR0-5 (nuuser or the driver has set it).
 *
 *   cc -O -o nupage nupage.c && ./nupage [N]
 *
 * Gap unit: one turn of a spin loop; the program times it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/times.h>

static volatile unsigned char *win;	/* 0xFA0000 - 0xFBFFFF */
volatile unsigned char nu_sink;
volatile int spin;

#define RD(r)		(win[(r) << 9])
#define WR(r, v)	(nu_sink = win[0x10000 + ((r) << 9) + ((v) << 1)])
#define SPIN(n)		{ register int s_; for (s_ = (n); s_ > 0; s_--) spin++; }

/* page 0 reg 4 is TSR, page 1 reg 4 is PAR3: which page a read sees */
static int par3, tsr;

int
main(int argc, char **argv)
{
	static int gaps[] = { 0, 1, 2, 3, 4, 6, 8, 12, 16, 32, -1 };
	int fd, n, i, g, gap, a, b, c, d;
	struct tms t0, t1;
	caddr_t p;

	n = argc > 1 ? atoi(argv[1]) : 2000;
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
	/* first touches of each page fault (triple access): harmless ones */
	WR(0, 0x21);
	nu_sink = RD(0);
	nu_sink = RD(8);
	WR(8, 0);
	if ((RD(0) & 0x03) == 0x02) {
		printf("the chip is running: stop it first (CR %02x)\n", RD(0));
		return 1;
	}
	(void)times(&t0);
	SPIN(1000000);
	(void)times(&t1);
	printf("spin: 1000000 turns in %ld ticks of 1/%ld s\n",
	    (long)(t1.tms_utime - t0.tms_utime), sysconf(_SC_CLK_TCK));

	/* the two values, read slowly */
	WR(0, 0x61);
	SPIN(1000);
	par3 = RD(4);
	WR(0, 0x21);
	SPIN(1000);
	tsr = RD(4);
	printf("page 1 reg 4 (PAR3) %02x, page 0 reg 4 (TSR) %02x\n", par3, tsr);
	if (par3 == tsr) {
		printf("cannot tell the pages apart\n");
		return 1;
	}

	printf("gap  A:sel1,read  B:sel0,read  C:sel1,write  D:sel1,sel0  "
	    "(wrong of %d)\n", n);
	for (g = 0; (gap = gaps[g]) >= 0; g++) {
		a = b = c = d = 0;
		for (i = 0; i < n; i++) {
			/* A: select page 1, read reg 4 after the gap */
			WR(0, 0x21);
			SPIN(200);
			WR(0, 0x61);
			SPIN(gap);
			if (RD(4) != par3)
				a++;
			SPIN(200);
			/* B: from page 1, select page 0, read after the gap */
			WR(0, 0x21);
			SPIN(gap);
			if (RD(4) != tsr)
				b++;
			SPIN(200);
			/* C: select page 1, write PAR3 after the gap, check slowly */
			WR(0, 0x61);
			SPIN(gap);
			WR(4, par3 ^ 0x01);
			SPIN(200);
			WR(0, 0x61);
			SPIN(200);
			if (RD(4) != (par3 ^ 0x01))
				c++;
			WR(4, par3);
			SPIN(200);
			/* D: select page 1, then page 0 after the gap */
			WR(0, 0x61);
			SPIN(gap);
			WR(0, 0x21);
			SPIN(200);
			if (RD(4) != tsr)
				d++;
			WR(0, 0x21);
			SPIN(200);
		}
		printf("%3d  %5d        %5d        %5d         %5d\n", gap, a, b,
		    c, d);
		fflush(stdout);
	}
	/* leave page 1 as found */
	WR(0, 0x61);
	SPIN(1000);
	WR(4, par3);
	SPIN(1000);
	WR(0, 0x21);
	return 0;
}
