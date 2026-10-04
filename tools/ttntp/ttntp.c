/*
 * ttntp - set Atari System V's clock from an NTP server (SNTP, RFC 4330).
 *
 *   ttntp [-n] [-q] [-z] [server ...]   (default: pool.ntp.org)
 *   ttntp -b
 *
 *   -n   only print how far off the clock is; change nothing
 *   -q   quiet: print only errors (for cron and the boot script)
 *   -z   once the clock is right, make the TT's clock chip keep LOCAL
 *        time, as TOS does, and write the time to it (see below)
 *   -b   at boot, before the network is up: the kernel has read the
 *        chip's local time as GMT, so move the clock by the zone's offset.
 *        Once only (the boot script); the chip is not touched.
 *
 * A server is a dotted address or a name. Names are looked up with a DNS
 * query of ttntp's own, to the first nameserver in /etc/resolv.conf, so
 * nothing but the socket library is needed. Each address a name has is
 * tried in turn until one answers.
 *
 * ASV can only step its clock by whole seconds: stime() keeps the current
 * fraction of a second (and settimeofday() just rounds and calls it). So
 * the whole-second part of the error is stepped with stime() and the rest
 * slewed away with adjtime(); an error under half a second is only
 * slewed, so the hourly run from cron never makes the clock jump. A step
 * also has the kernel write the TT's clock chip (the one TOS reads too),
 * where the clock driver allows it.
 *
 * The clock chip. ASV's clock driver keeps it at GMT minus an offset it
 * is told with an ioctl on /dev/rtc ('R'<<8|1, seconds west of GMT, root
 * only), and until it is told it never writes the chip at all. TOS has no
 * time zones and keeps local time there, so -z sets the offset to the
 * local zone's (from TZ, daylight time included) and has the kernel write
 * the chip: after that ASV and TOS read the same, right, time from it. The
 * driver also needs the chip's year counted from 1968, as TOS counts it -
 * sp1's patched CLOCK (tools/ttntp/README.md).
 *
 * The chip is written only after a time server has answered: at boot the
 * clock is still the kernel's reading of the chip (off by the zone's
 * offset, unless -b ran), and writing that back made each boot whose
 * network wasn't up yet lose another 6 hours.
 *
 * At boot the driver's offset is 0 again, so the kernel reads the chip's
 * local time as GMT, and the stock /sbin/setclk does not correct it on
 * this system: -b does.
 *
 * Build (static, like the other sp1 tools):
 *   ASV_SYSROOT=... ../../rsync/asv-static-cc -O -I../../rsync/include -o ttntp ttntp.c -lsocket
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>

#define NTP_PORT	123
#define DNS_PORT	53
#define JAN_1970	2208988800UL	/* 1900 to 1970 in seconds */
#define TIMEOUT		3		/* seconds per query */

extern int stime(), adjtime();
extern long timezone, altzone;		/* libc: seconds west of GMT (tzset) */

static int quiet;

#define RTC_SETOFFSET	(('R' << 8) | 1)

static double now(void)
{
	struct timeval tv;

	gettimeofday(&tv, (struct timezone *)0);
	return (double)tv.tv_sec + tv.tv_usec / 1e6;
}

/* wait up to TIMEOUT seconds for fd to be readable */
static int readable(int fd)
{
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	tv.tv_sec = TIMEOUT;
	tv.tv_usec = 0;
	return select(fd + 1, &fds, (fd_set *)0, (fd_set *)0, &tv) > 0;
}

/* the first nameserver in /etc/resolv.conf, or 0 */
static unsigned long nameserver(void)
{
	FILE *f = fopen("/etc/resolv.conf", "r");
	char line[256], addr[64];
	unsigned long a = 0;

	if (f == NULL)
		return 0;
	while (fgets(line, sizeof line, f) != NULL)
		if (sscanf(line, "nameserver %63s", addr) == 1 &&
		    (a = inet_addr(addr)) != (unsigned long)-1)
			break;
		else
			a = 0;
	fclose(f);
	return a;
}

/* skip a (possibly compressed) name in a DNS message; returns the offset
 * after it, or -1 */
static int skipname(const unsigned char *m, int len, int o)
{
	while (o < len) {
		if (m[o] == 0)
			return o + 1;
		if ((m[o] & 0xC0) == 0xC0)
			return o + 2;
		o += m[o] + 1;
	}
	return -1;
}

/* name -> up to max IPv4 addresses (network order); returns how many */
static int resolve(const char *name, unsigned long *out, int max)
{
	unsigned char q[512], r[512];
	struct sockaddr_in sa;
	unsigned long ns;
	const char *p;
	int fd, n, len, o, i, an, found = 0;
	unsigned short id = (unsigned short)(getpid() ^ (int)now());

	if ((out[0] = inet_addr(name)) != (unsigned long)-1)
		return 1;			/* a dotted address */
	if ((ns = nameserver()) == 0) {
		fprintf(stderr, "ttntp: no nameserver in /etc/resolv.conf for %s\n", name);
		return 0;
	}
	/* header: id, recursion desired, one question */
	memset(q, 0, 12);
	q[0] = id >> 8; q[1] = id & 0xFF; q[2] = 0x01; q[5] = 1;
	n = 12;
	for (p = name; *p; ) {
		const char *dot = strchr(p, '.');
		int l = dot ? (int)(dot - p) : (int)strlen(p);

		if (l == 0 || l > 63 || n + l + 6 > (int)sizeof q)
			return 0;
		q[n++] = (unsigned char)l;
		memcpy(q + n, p, (size_t)l);
		n += l;
		p += l + (dot != NULL);
	}
	q[n++] = 0;
	q[n++] = 0; q[n++] = 1;			/* type A */
	q[n++] = 0; q[n++] = 1;			/* class IN */

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return 0;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(DNS_PORT);
	sa.sin_addr.s_addr = ns;
	if (sendto(fd, (char *)q, n, 0, (struct sockaddr *)&sa, sizeof sa) != n ||
	    !readable(fd) || (len = recv(fd, (char *)r, sizeof r, 0)) < 12 ||
	    r[0] != q[0] || r[1] != q[1] || (r[3] & 0x0F) != 0) {
		close(fd);
		fprintf(stderr, "ttntp: cannot look up %s\n", name);
		return 0;
	}
	close(fd);
	an = (r[6] << 8) | r[7];
	if ((o = skipname(r, len, 12)) < 0)
		return 0;
	o += 4;					/* the question's type and class */
	for (i = 0; i < an && found < max; i++) {
		int type, rdlen;

		if ((o = skipname(r, len, o)) < 0 || o + 10 > len)
			break;
		type = (r[o] << 8) | r[o + 1];
		rdlen = (r[o + 8] << 8) | r[o + 9];
		o += 10;
		if (o + rdlen > len)
			break;
		if (type == 1 && rdlen == 4)
			memcpy(&out[found++], r + o, 4);
		o += rdlen;
	}
	return found;
}

static double ntptime(const unsigned char *p)
{
	unsigned long s = ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
			  ((unsigned long)p[2] << 8) | p[3];
	unsigned long f = ((unsigned long)p[4] << 24) | ((unsigned long)p[5] << 16) |
			  ((unsigned long)p[6] << 8) | p[7];

	return (double)(s - JAN_1970) + f / 4294967296.0;
}

/* one SNTP exchange; on success sets *offset (server - us) and returns 1 */
static int query(unsigned long addr, double *offset, double *delay)
{
	unsigned char pk[48];
	struct sockaddr_in sa;
	int fd, n;
	double t1, t2, t3, t4;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return 0;
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(NTP_PORT);
	sa.sin_addr.s_addr = addr;
	memset(pk, 0, sizeof pk);
	pk[0] = 0x23;				/* LI 0, version 4, client */
	t1 = now();
	if (sendto(fd, (char *)pk, sizeof pk, 0, (struct sockaddr *)&sa, sizeof sa) != sizeof pk ||
	    !readable(fd) || (n = recv(fd, (char *)pk, sizeof pk, 0)) < 48) {
		close(fd);
		return 0;
	}
	t4 = now();
	close(fd);
	/* a server that is not synchronised (LI 3, stratum 0) says nothing */
	if ((pk[0] & 0xC0) == 0xC0 || (pk[0] & 7) != 4 || pk[1] == 0)
		return 0;
	t2 = ntptime(pk + 32);
	t3 = ntptime(pk + 40);
	*offset = ((t2 - t1) + (t3 - t4)) / 2;
	*delay = (t4 - t1) - (t3 - t2);
	return 1;
}

/* correct the clock by offset seconds: step the whole seconds, slew the
 * rest. Returns -1 (errno set) on failure, 1 if it stepped, 0 if not. */
static int setclock(double offset)
{
	long k = (long)(offset >= 0 ? offset + 0.5 : offset - 0.5);
	double r = offset - k, t;
	struct timeval d;
	int stepped = 0;

	if (k != 0) {
		/* stime() keeps the fraction, so the step is exact - unless a
		 * second boundary passes between reading the clock and setting
		 * it: stay clear of one */
		while ((t = now()) - (double)(long)t > 0.9)
			;
		{
			long s = (long)t + k;

			if (stime(&s) < 0)
				return -1;
		}
		stepped = 1;
	}
	d.tv_sec = 0;
	d.tv_usec = (long)(r * 1e6);
	if (d.tv_usec != 0 && adjtime(&d, (struct timeval *)0) < 0)
		return -1;
	return stepped;
}

/* -z: the chip keeps local time; write the time to it now. The kernel
 * writes the chip when the time is set, so set it to the second it
 * already is (stime() keeps the fraction). */
static int chip_local(void)
{
	time_t t = time((time_t *)0);
	struct tm *tm = localtime(&t);
	long off = tm->tm_isdst > 0 ? altzone : timezone;
	int fd = open("/dev/rtc", O_RDONLY);
	double n;
	long s;

	if (fd < 0 || ioctl(fd, RTC_SETOFFSET, &off) < 0) {
		perror("ttntp: /dev/rtc");
		if (fd >= 0)
			close(fd);
		return -1;
	}
	close(fd);
	while ((n = now()) - (double)(long)n > 0.9)
		;
	s = (long)n;
	if (stime(&s) < 0) {
		perror("ttntp: stime");
		return -1;
	}
	if (!quiet)
		printf("clock chip keeps local time (GMT%+ld:%02ld)\n",
		       -off / 3600, (off < 0 ? -off : off) % 3600 / 60);
	return 0;
}

/* -b: the kernel read the chip's local time as GMT; add the zone's
 * offset (seconds west), daylight time included. The driver hasn't been
 * told an offset since boot, so it doesn't write the chip. */
static int boot_local(void)
{
	time_t t = time((time_t *)0);
	time_t l = t + timezone;
	struct tm *tm = localtime(&l);
	long s = (long)t + (tm->tm_isdst > 0 ? altzone : timezone);

	if (stime(&s) < 0) {
		perror("ttntp: stime");
		return 1;
	}
	if (!quiet)
		printf("clock moved by the zone's offset: %+ld s\n", s - (long)t);
	return 0;
}

int main(int argc, char **argv)
{
	int dry = 0, zone = 0, boot = 0, i, j, n;
	const char *def[] = { "pool.ntp.org" };
	const char **servers = def;
	int nservers = 1;
	unsigned long addr[8];

	while (argc > 1 && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-n"))
			dry = 1;
		else if (!strcmp(argv[1], "-q"))
			quiet = 1;
		else if (!strcmp(argv[1], "-z"))
			zone = 1;
		else if (!strcmp(argv[1], "-b"))
			boot = 1;
		else {
			fprintf(stderr, "usage: ttntp [-n] [-q] [-z] [server ...] | ttntp [-q] -b\n");
			return 2;
		}
		argc--, argv++;
	}
	tzset();
	if (boot)
		return boot_local();
	if (argc > 1) {
		servers = (const char **)(argv + 1);
		nservers = argc - 1;
	}
	for (i = 0; i < nservers; i++) {
		n = resolve(servers[i], addr, 8);
		for (j = 0; j < n; j++) {
			double off, del;
			struct in_addr ia;

			if (!query(addr[j], &off, &del))
				continue;
			ia.s_addr = addr[j];
			if (!quiet || dry)
				printf("%s (%s): clock %s by %.3f s (round trip %.3f s)\n",
				       servers[i], inet_ntoa(ia),
				       off >= 0 ? "slow" : "fast", off >= 0 ? off : -off, del);
			if (dry)
				return 0;
			switch (setclock(off)) {
			case -1:
				perror("ttntp: setting the clock");
				return 1;
			case 1:
				if (!quiet)
					printf("clock stepped, and the rest slewed\n");
				break;
			default:
				if (!quiet)
					printf("clock slewed\n");
			}
			if (zone && chip_local() < 0)
				return 1;
			return 0;
		}
	}
	fprintf(stderr, "ttntp: no time server answered\n");
	return 1;
}
