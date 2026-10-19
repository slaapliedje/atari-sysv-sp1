/*
 * nustat - the nu driver's counters: what the chip core has counted since
 * the card was set going.  -r also prints the chip's registers, pages 0-2,
 * as the kernel reads them.
 *
 *   cc -o nustat nustat.c && ./nustat [-r] [device]
 *   ./nustat -m [BLOCK DLY NOCACHE [PAD]]   how the driver reaches the card:
 *       BLOCK ff (0xFFFA0000/0xFFFB0000) or 00 (0x00FA0000/0x00FB0000,
 *       NOCACHE 1 only), DLY spin turns before each access, PAD: a
 *       same-address ST-bus cycle 1 before / 2 after each write, 4 for
 *       reads too, 8 the access with the instruction cache frozen
 *       (default 15); no arguments: only print it
 *   ./nustat -t                       probe + init now, in that mode
 *   ./nustat -x OP...                 register accesses by the kernel, back
 *       to back (NUIOC_RAW): rRR reads register RR, wRRVV writes VV to it,
 *       sNN waits NN*10 us; hex; prints what the reads gave.  Up to 64.
 *       A first dNNN (decimal) sleeps NNN ms after the open, before the
 *       accesses (the open itself probes, which resets the chip).  The
 *       ioctl is timed.
 *   ./nustat -X RR VV N               N times: write VV to RR, read it back
 *       (page 0 registers only); prints the values that came back wrong
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stropts.h>
#include <poll.h>
#include <sys/time.h>
#include "nu.h"
#include "nustat.h"

static int
nuioctl(int fd, int cmd, char *buf, int len)
{
	struct strioctl ic;

	ic.ic_cmd = cmd;
	ic.ic_timout = 5;
	ic.ic_len = len;
	ic.ic_dp = buf;
	return ioctl(fd, I_STR, &ic);
}

static void
pregs(struct nu_regs *rg)
{
	int p, i;

	printf("%s, CR %02x, %ld MFP reads/ms\n", rg->running ? "running" :
	    "stopped", rg->cr, rg->perms);
	printf("      ");
	for (i = 0; i < 16; i++)
		printf(" %x ", i);
	printf("\n");
	for (p = 0; p < 3; p++) {
		printf("page %d", p);
		for (i = 0; i < 16; i++)
			printf(" %02x", rg->page[p][i]);
		printf("\n");
	}
}

static int
mode(int fd, int argc, char **argv)
{
	struct nu_mode m;

	memset(&m, 0, sizeof m);
	if (argc >= 4) {
		m.set = 1;
		if (strcmp(argv[1], "00") == 0)
			m.rbase = 0x00fa0000UL, m.wbase = 0x00fb0000UL;
		else
			m.rbase = 0xfffa0000UL, m.wbase = 0xfffb0000UL;
		m.dly = atoi(argv[2]);
		m.nocache = atoi(argv[3]);
		m.pad = argc >= 5 ? atoi(argv[4]) : 15;
	}
	if (nuioctl(fd, NUIOC_MODE, (char *)&m, sizeof m) < 0) {
		perror("NUIOC_MODE");
		return 1;
	}
	printf("reads %08lx, writes %08lx, %d spin turns before each access, "
	    "data cache %s while the chip is worked, pad %d\n", m.rbase, m.wbase,
	    m.dly, m.nocache ? "off" : "on", m.pad);
	return 0;
}

static int
test(int fd)
{
	struct nu_test t;

	if (nuioctl(fd, NUIOC_TEST, (char *)&t, sizeof t) < 0) {
		perror("NUIOC_TEST");
		return 1;
	}
	printf("probe %s, init %s", t.probe < 0 ? "FAILED" : "ok",
	    t.init < 0 ? "FAILED" : "ok");
	if (t.init < 0 && t.probe == 0)
		printf(" (page %d reg %x read %02x, wanted %02x)", t.badreg >> 8,
		    t.badreg & 0xff, t.badgot, t.badwant);
	printf("; failed inits %lu, page selects redone %lu\n", t.initbad,
	    t.crmiss);
	pregs(&t.regs);
	return t.init < 0;
}

static int
raw(int fd, struct nu_raw *rw)
{
	if (nuioctl(fd, NUIOC_RAW, (char *)rw, sizeof *rw) < 0) {
		perror("NUIOC_RAW");
		return -1;
	}
	return 0;
}

/* -x: the accesses on the command line */
static int
xraw(int fd, int argc, char **argv)
{
	struct nu_raw rw;
	struct timeval t0, t1;
	int i, n = 0;

	if (argc > 1 && argv[1][0] == 'd') {
		(void)poll((struct pollfd *)0, 0, atoi(argv[1] + 1));
		argc--, argv++;
	}
	for (i = 1; i < argc && n < NU_RAWMAX; i++, n++) {
		char *a = argv[i];

		rw.a[n].pad = 0;
		rw.a[n].reg = (int)strtol(a + 1, (char **)0, 16) & 0xff;
		rw.a[n].val = 0;
		if (a[0] == 'r')
			rw.a[n].op = NU_RAW_RD;
		else if (a[0] == 'w' && strlen(a) >= 5) {
			rw.a[n].op = NU_RAW_WR;
			rw.a[n].reg = (int)strtol(a + 1, (char **)0, 16) >> 8 & 0xff;
			rw.a[n].val = (int)strtol(a + 1, (char **)0, 16) & 0xff;
		} else if (a[0] == 's') {
			rw.a[n].op = NU_RAW_SPIN;
			rw.a[n].reg = 0;
			rw.a[n].val = (int)strtol(a + 1, (char **)0, 16) & 0xff;
		} else {
			fprintf(stderr, "nustat -x: bad access %s\n", a);
			return 1;
		}
	}
	rw.n = n;
	gettimeofday(&t0, (struct timezone *)0);
	if (raw(fd, &rw) < 0)
		return 1;
	gettimeofday(&t1, (struct timezone *)0);
	for (i = 0; i < n; i++) {
		if (rw.a[i].op == NU_RAW_RD)
			printf("R %02x %02x\n", rw.a[i].reg, rw.a[i].val);
		else if (rw.a[i].op == NU_RAW_WR)
			printf("W %02x %02x\n", rw.a[i].reg, rw.a[i].val);
	}
	printf("(%ld us)\n", (t1.tv_sec - t0.tv_sec) * 1000000L + t1.tv_usec - t0.tv_usec);
	return 0;
}

/* -X RR VV N: write and read back N times, count the misses */
static int
xloop(int fd, int argc, char **argv)
{
	struct nu_raw rw;
	int reg, val, n, i, done = 0, bad = 0;
	int seen[256];

	if (argc < 4) {
		fprintf(stderr, "usage: nustat -X RR VV N\n");
		return 1;
	}
	reg = (int)strtol(argv[1], (char **)0, 16) & 0x1f;
	val = (int)strtol(argv[2], (char **)0, 16) & 0xff;
	n = atoi(argv[3]);
	memset(seen, 0, sizeof seen);
	while (done < n) {
		int k = n - done < NU_RAWMAX / 2 ? n - done : NU_RAWMAX / 2;

		for (i = 0; i < k; i++) {
			rw.a[2 * i].op = NU_RAW_WR, rw.a[2 * i].reg = reg;
			rw.a[2 * i].val = val, rw.a[2 * i].pad = 0;
			rw.a[2 * i + 1].op = NU_RAW_RD, rw.a[2 * i + 1].reg = reg;
			rw.a[2 * i + 1].val = 0, rw.a[2 * i + 1].pad = 0;
		}
		rw.n = 2 * k;
		if (raw(fd, &rw) < 0)
			return 1;
		for (i = 0; i < k; i++) {
			int got = rw.a[2 * i + 1].val;

			if (got != val)
				bad++;
			seen[got]++;
		}
		done += k;
	}
	printf("register %02x <- %02x, %d times: %d read back wrong\n", reg, val, n, bad);
	for (i = 0; i < 256; i++)
		if (seen[i])
			printf("  %02x: %d\n", i, seen[i]);
	return bad != 0;
}

int
main(int argc, char **argv)
{
	const char *dev = "/dev/nu0";
	struct nu_stats st;
	struct nu_regs rg;
	int fd, regs = 0;

	if (argc > 1 && argv[1][0] == '-' && strchr("mtxX", argv[1][1]) &&
	    argv[1][2] == '\0') {
		if ((fd = open(dev, O_RDWR)) < 0) {
			perror(dev);
			return 1;
		}
		switch (argv[1][1]) {
		case 'm':
			return mode(fd, argc - 1, argv + 1);
		case 'x':
			return xraw(fd, argc - 1, argv + 1);
		case 'X':
			return xloop(fd, argc - 1, argv + 1);
		}
		return test(fd);
	}
	if (argc > 1 && strcmp(argv[1], "-r") == 0) {
		regs = 1;
		argc--, argv++;
	}
	if (argc > 1)
		dev = argv[1];
	if ((fd = open(dev, O_RDWR)) < 0) {
		perror(dev);
		return 1;
	}
	if (nuioctl(fd, NUIOC_STATS, (char *)&st, sizeof st) < 0) {
		perror("NUIOC_STATS");
		return 1;
	}
	printf("in   %lu frames, %lu errors (crc %lu, alignment %lu, missed %lu, "
	    "ring overflows %lu, ring lost %lu, no memory %lu)\n",
	    st.ipackets, st.ierrors, st.crc, st.fae, st.missed, st.ovw,
	    st.rxbad, st.nomem);
	printf("out  %lu frames, %lu errors (collisions %lu, aborted %lu, "
	    "no carrier %lu, underrun %lu, late collisions %lu, timeouts %lu)\n",
	    st.opackets, st.oerrors, st.collisions, st.txabt, st.txcarrier,
	    st.txfu, st.txowc, st.txtimeout);
	printf("chip re-inits %lu, failed inits %lu, page selects redone %lu, "
	    "polls %lu\n", st.reinit, st.initbad, st.crmiss, st.polls);
	printf("re-probes %lu, config registers put back %lu, frames dropped "
	    "while down %lu\n", st.recover, st.cfgfix, st.txdead);
	if (!regs)
		return 0;
	if (nuioctl(fd, NUIOC_REGS, (char *)&rg, sizeof rg) < 0) {
		perror("NUIOC_REGS");
		return 1;
	}
	if (rg.badreg)
		printf("last failed init: page %d reg %x read %02x, wanted %02x\n",
		    rg.badreg >> 8, rg.badreg & 0xff, rg.badgot, rg.badwant);
	pregs(&rg);
	return 0;
}
