/*
 * nuuser - the NetUSBee chip core (nuchip.c) as an ordinary program, the
 * card reached through an mmap(2) of /dev/mem: a register write is a load
 * in the 0xFB0000 window, the byte on A1-A8.  It tries the card with no
 * kernel change.
 *
 *   cc -O -DNU_DEVMEM -o nuuser nuuser.c nuchip.c
 *   cc -O -DNU_DEVMEM -DNU_INLINE -o nuuseri nuuser.c nuchip.c
 *			(inline accesses, as tight as the kernel's; no -t/-T;
 *			 -g N first: N spin turns, about 0.7 us each, before
 *			 each access, after a call; without -g only the call)
 *   cc -O -DNU_DEVMEM -DNU_KIOCTL -o nuuserk nuuser.c nuchip.c
 *			(every access done by the kernel: NUIOC_RAW on
 *			 /dev/nu0, one ioctl each; the same chip core, the
 *			 kernel's bus cycles)
 *
 *   nuuser [-t] ...      -t: trace every register access
 *   nuuser [-T] ...      -T: the same, kept in memory and printed at the end
 *   nuuser               find the card: memory, station address
 *   nuuser listen SECS   print the frames that reach it for SECS seconds
 *   nuuser dhcp          send a DHCP discover, print what comes (10 s)
 *
 * Learned on the TT (ASV):
 * - the first touch of each page of the mapping faults, and the card sees
 *   that access three times (the fault path touches it too; read(2) of
 *   /dev/mem does the same every time): prefault() makes those harmless;
 * - a load whose value is thrown away is dropped by cc, volatile or not,
 *   so the write's load is stored to nu_sink;
 * - the mapping is not cached: the same address reads afresh each time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <poll.h>
#include <time.h>
#ifdef NU_KIOCTL
#include <stropts.h>
#include "nustat.h"
#endif
#include "nu.h"

static int trace;
static int gap = -1;			/* -g N: nu_pause spins N turns; -1 none */
__volatile__ int nu_spinv;

/* NU_INLINE: called before each access; -g -1 returns at once */
void
nu_pause(void)
{
	register int i;

	for (i = gap; i > 0; i--)
		nu_spinv++;
}
static volatile unsigned char *win;	/* 0xFA0000 - 0xFBFFFF */
/* (__volatile__: the TT cc's headers leave plain volatile empty) */
__volatile__ unsigned char *nu_win;	/* the same, for NU_INLINE */
__volatile__ unsigned char nu_sink;
static struct nu_softc sc;
static unsigned char xid[4];
static time_t t0;

/* -T: every access kept here at full speed, printed at the end */
#define NLOG	16384
static unsigned short tlog[NLOG];	/* W: 0x8000 | r << 8 | v; R: r << 8 | v */
static int ntlog;

static void
dumplog(void)
{
	int i, from = ntlog > NLOG ? ntlog - NLOG : 0;

	for (i = from; i < ntlog; i++) {
		unsigned short e = tlog[i % NLOG];

		printf("%c %02x %02x\n", e & 0x8000 ? 'W' : 'R', (e >> 8) & 0x1f,
		    e & 0xff);
	}
}

/* NU_INLINE: the accesses, for -T */
int
nu_rdlog(int r, int v)
{
	if (trace == 2)
		tlog[ntlog++ % NLOG] = r << 8 | (v & 0xff);
	return v & 0xff;
}

int
nu_wrlog(int r, int v)
{
	v &= 0xff;
	if (trace == 2)
		tlog[ntlog++ % NLOG] = 0x8000 | r << 8 | v;
	return v;
}

#ifdef NU_KIOCTL
static int kfd;				/* /dev/nu0 */

/* one register access by the kernel */
static int
kraw(int op, int r, int v)
{
	struct nu_raw rw;
	struct strioctl ic;

	rw.n = 1;
	rw.a[0].op = op, rw.a[0].reg = r, rw.a[0].val = v, rw.a[0].pad = 0;
	ic.ic_cmd = NUIOC_RAW;
	ic.ic_timout = 5;
	ic.ic_len = sizeof rw;
	ic.ic_dp = (char *)&rw;
	if (ioctl(kfd, I_STR, &ic) < 0) {
		perror("NUIOC_RAW");
		exit(1);
	}
	return rw.a[0].val;
}
#define RDCARD(r)	kraw(NU_RAW_RD, r, 0)
#define WRCARD(r, v)	kraw(NU_RAW_WR, r, v)
#else
#define RDCARD(r)	win[(r) << NU_REGSHIFT]
#define WRCARD(r, v)	(nu_sink = win[0x10000 + ((r) << NU_REGSHIFT) + ((v) << 1)])
#endif

int
nu_devrd(int r)
{
	int v = RDCARD(r);

	if (trace == 1)
		printf("R %02x %02x\n", r, v);
	else if (trace == 2)
		tlog[ntlog++ % NLOG] = r << 8 | v;
	return v;
}

void
nu_devwr(int r, int v)
{
	if (trace == 1)
		printf("W %02x %02x\n", r, v);
	else if (trace == 2)
		tlog[ntlog++ % NLOG] = 0x8000 | r << 8 | v;
	WRCARD(r, v);
}

/*
 * The first touch of each 4 KB page (registers 0-7, 8-15, 16-23, 24-31 in
 * each window) reaches the card three times: a harmless one each, in an
 * order that leaves the chip stopped and reset.
 */
static void
prefault(void)
{
	nu_sink = win[0x10000 + (0x21 << 1)];		/* CR: stop, no DMA */
	nu_sink = win[0];				/* CR */
	nu_sink = win[0x08 << NU_REGSHIFT];		/* CLDA0 */
	nu_sink = win[0x10000 + (0x08 << NU_REGSHIFT)];	/* RSAR0 = 0 */
	nu_sink = win[0x10 << NU_REGSHIFT];		/* data, no DMA */
	nu_sink = win[0x10000 + (0x10 << NU_REGSHIFT)];
	nu_sink = win[0x1f << NU_REGSHIFT];		/* reset */
	nu_sink = win[0x10000 + (0x1f << NU_REGSHIFT)];
	nu_devdelay(20000);
}

static int
devmap(void)
{
	int fd;
	caddr_t p;

#ifdef NU_KIOCTL
	if ((kfd = open("/dev/nu0", O_RDWR)) < 0) {
		perror("/dev/nu0");
		return -1;
	}
	return 0;
#endif
	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		perror("/dev/mem");
		return -1;
	}
	p = mmap((caddr_t)0, 0x20000, PROT_READ, MAP_SHARED, fd,
	    (off_t)0xfa0000);
	if (p == (caddr_t)-1) {
		perror("mmap /dev/mem");
		return -1;
	}
	nu_win = win = (volatile unsigned char *)p;
	prefault();
	return 0;
}

void
nu_devdelay(int us)
{
	(void)poll((struct pollfd *)0, 0, (us + 999) / 1000);
}

/* pages 0-2, read slowly (the function path, a pause around each) */
static void
dumppages(void)
{
	int p, i, v[16];

	printf("      ");
	for (i = 0; i < 16; i++)
		printf(" %x ", i);
	printf("\n");
	for (p = 0; p < 3; p++) {
		nu_devdelay(1000);
		nu_devwr(0, 0x21 | p << 6);
		nu_devdelay(1000);
		for (i = 0; i < 16; i++)
			v[i] = nu_devrd(i);
		printf("page %d", p);
		for (i = 0; i < 16; i++)
			printf(" %02x", v[i]);
		printf("\n");
	}
	nu_devdelay(1000);
	nu_devwr(0, 0x21);
}

static void
pmac(const unsigned char *a)
{
	printf("%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4],
	    a[5]);
}

static void
pip(const unsigned char *a)
{
	printf("%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
}

/* a DHCP reply to our discover: what it offers */
static void
dhcpreply(const unsigned char *u, int n)
{
	const unsigned char *o = u + 240, *end = u + n;

	if (n < 240 || u[0] != 2 || memcmp(u + 4, xid, 4) != 0)
		return;
	printf("    DHCP from server ");
	pip(u + 20);
	printf(", offers ");
	pip(u + 16);
	printf("\n");
	while (o + 2 <= end && *o != 255) {
		int t = o[0], l = o[1], i;

		if (t == 0) {
			o++;
			continue;
		}
		if (o + 2 + l > end)
			break;
		if (t == 53)
			printf("    message type %d\n", o[2]);
		else if ((t == 1 || t == 3 || t == 6 || t == 54) && l >= 4) {
			printf("    %s", t == 1 ? "netmask" : t == 3 ? "router" :
			    t == 6 ? "dns" : "server id");
			for (i = 0; i + 4 <= l; i += 4) {
				printf(" ");
				pip(o + 2 + i);
			}
			printf("\n");
		} else if (t == 51 && l == 4)
			printf("    lease %lu s\n", (unsigned long)o[2] << 24 |
			    (unsigned long)o[3] << 16 | o[4] << 8 | o[5]);
		o += 2 + l;
	}
}

void
nu_input(struct nu_softc *s, unsigned char *f, int len)
{
	int type = f[12] << 8 | f[13];

	printf("%3ld ", (long)(time((time_t *)0) - t0));
	pmac(f + 6);
	printf(" > ");
	pmac(f);
	if (type == 0x0806 && len >= 42) {
		int op = f[20] << 8 | f[21];

		if (op == 1) {
			printf(" ARP who-has ");
			pip(f + 38);
			printf(" tell ");
			pip(f + 28);
		} else if (op == 2) {
			printf(" ARP ");
			pip(f + 28);
			printf(" is-at ");
			pmac(f + 22);
		} else
			printf(" ARP op %d", op);
	} else if (type == 0x0800 && len >= 34) {
		int hl = (f[14] & 15) * 4, pr = f[23];

		printf(" IP ");
		pip(f + 26);
		printf(" > ");
		pip(f + 30);
		if ((pr == 17 || pr == 6) && len >= 14 + hl + 8) {
			const unsigned char *t = f + 14 + hl;
			int sp = t[0] << 8 | t[1], dp = t[2] << 8 | t[3];

			printf(" %s %d > %d", pr == 17 ? "udp" : "tcp", sp, dp);
			if (pr == 17 && dp == 68) {
				printf(" len %d\n", len);
				dhcpreply(t + 8, len - 14 - hl - 8);
				return;
			}
		} else
			printf(" proto %d", pr);
	} else
		printf(" type %04x", type);
	printf(" len %d\n", len);
	fflush(stdout);
}

void
nu_txdone(struct nu_softc *s)
{
}

static int
cksum(const unsigned char *p, int n)
{
	unsigned long s = 0;
	int i;

	for (i = 0; i + 1 < n; i += 2)
		s += p[i] << 8 | p[i + 1];
	if (i < n)
		s += p[i] << 8;
	while (s >> 16)
		s = (s & 0xffff) + (s >> 16);
	return (int)(~s & 0xffff);
}

/* DHCPDISCOVER, broadcast, asking for netmask, router and DNS */
static void
discover(void)
{
	unsigned char *f = sc.txbuf, *ip = f + 14, *udp = ip + 20, *u = udp + 8;
	int ulen = 8 + 240 + 60, c, i;

	memset(f, 0, 14 + 20 + ulen);
	memset(f, 0xff, 6);
	memcpy(f + 6, sc.ea, 6);
	f[12] = 0x08;
	u[0] = 1;
	u[1] = 1;
	u[2] = 6;
	memcpy(u + 4, xid, 4);
	u[10] = 0x80;
	memcpy(u + 28, sc.ea, 6);
	u[236] = 99;
	u[237] = 130;
	u[238] = 83;
	u[239] = 99;
	i = 240;
	u[i++] = 53, u[i++] = 1, u[i++] = 1;
	u[i++] = 55, u[i++] = 3, u[i++] = 1, u[i++] = 3, u[i++] = 6;
	u[i++] = 255;
	udp[1] = 68;
	udp[3] = 67;
	udp[4] = ulen >> 8;
	udp[5] = ulen;
	ip[0] = 0x45;
	ip[2] = (20 + ulen) >> 8;
	ip[3] = 20 + ulen;
	ip[8] = 64;
	ip[9] = 17;
	memset(ip + 16, 0xff, 4);
	c = cksum(ip, 20);
	ip[10] = c >> 8;
	ip[11] = c;
	nu_txstart(&sc, 14 + 20 + ulen);
}

int
main(int argc, char **argv)
{
	int secs = 0, dhcp = 0, i;
	time_t end;

	if (argc > 2 && strcmp(argv[1], "-g") == 0) {
		gap = atoi(argv[2]);
		argc -= 2, argv += 2;
	}
	if (argc > 1 && strcmp(argv[1], "-t") == 0) {
		trace = 1;
		argc--, argv++;
	} else if (argc > 1 && strcmp(argv[1], "-T") == 0) {
		trace = 2;
		argc--, argv++;
	}
	if (argc > 2 && strcmp(argv[1], "listen") == 0)
		secs = atoi(argv[2]);
	else if (argc > 1 && strcmp(argv[1], "dhcp") == 0)
		dhcp = 1, secs = 10;
	else if (argc > 1) {
		fprintf(stderr, "usage: nuuser [listen SECS | dhcp]\n");
		return 1;
	}
	if (devmap() < 0)
		return 1;
	if (nu_probe(&sc) < 0) {
		printf("no 8390 on the cartridge port\n");
		if (trace == 2)
			dumplog();
		return 1;
	}
	printf("%s, buffer pages %02x-%02x, station address ",
	    sc.rtl ? "RTL8019" : "8390", sc.memlo, sc.memhi);
	pmac(sc.prom);
	printf("\n");
	if (secs <= 0)
		return 0;

	memcpy(sc.ea, sc.prom, 6);
	sc.rcr = RCR_AB;
	sc.imr = 0;
	if (nu_init(&sc) < 0) {
		printf("init failed");
		if (sc.st.initbad)
			printf(": page %d register %x reads %02x, not %02x",
			    sc.badreg >> 8, sc.badreg & 0xff, sc.badgot, sc.badwant);
		printf("\n");
		if (trace == 2)
			dumplog();
		dumppages();
		return 1;
	}
	if (getenv("NU_DUMP"))			/* the registers after a good init */
		dumppages();
	if (getenv("NU_LOG") && trace == 2) {	/* the accesses so far */
		dumplog();
		return 0;
	}
	t0 = time((time_t *)0);
	i = (int)t0 ^ (int)getpid() << 8;
	xid[0] = i >> 24, xid[1] = i >> 16, xid[2] = i >> 8, xid[3] = i;
	if (dhcp) {
		discover();
		printf("discover sent\n");
	}
	fflush(stdout);
	end = t0 + secs;
	while (time((time_t *)0) < end) {
		if (!sc.running) {		/* nu_poll gave up on the chip */
			printf("re-init\n");
			if (nu_init(&sc) < 0)
				break;
		}
		if (!nu_poll(&sc))
			(void)poll((struct pollfd *)0, 0, 20);
	}
	nu_stop(&sc);
	printf("in %lu (errors %lu, missed %lu, overflows %lu, bad ring %lu), "
	    "out %lu (errors %lu), polls %lu\n", sc.st.ipackets, sc.st.ierrors,
	    sc.st.missed, sc.st.ovw, sc.st.rxbad, sc.st.opackets,
	    sc.st.oerrors, sc.st.polls);
	return 0;
}
