/*
 * asvstatic.c - what a statically linked ASV program needs that ASV's
 * static libraries do not provide. Linked into every program by
 * asv-static-cc.
 */
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <netconfig.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>

#define LLONG_MAX	0x7fffffffffffffffLL
#define LLONG_MIN	(-LLONG_MAX - 1)

/* The shadow sysroot's <stdio.h> names the standard streams through
 * __asv_iob(): in a dynamic program the table must come from libc.so.1 at
 * run time (see ../xserver-r6/config/asviob.c). A static program has one
 * table, libc.a's own. */
extern FILE __iob[];

FILE *
__asv_iob(void)
{
	return __iob;
}

/* libsocket.a's socket() finds its transport device (/dev/tcp ...) in the
 * netconfig database, whose reader lives only in the shared libnsl.so.
 * These are the inet entries of ASV's /etc/netconfig. Name lookups (the
 * nc_lookups libraries are shared objects too) are not available:
 * statically linked programs use numeric addresses. */
static struct netconfig nc_inet[] = {
	{ "tcp",   NC_TPI_COTS_ORD, NC_VISIBLE, NC_INET, "tcp",  "/dev/tcp" },
	{ "udp",   NC_TPI_CLTS,     NC_VISIBLE, NC_INET, "udp",  "/dev/udp" },
	{ "icmp",  NC_TPI_RAW,      NC_NOFLAG,  NC_INET, "icmp", "/dev/icmp" },
	{ "rawip", NC_TPI_RAW,      NC_NOFLAG,  NC_INET, "-",    "/dev/rawip" },
};
#define NNC	(sizeof nc_inet / sizeof nc_inet[0])

void *
setnetconfig(void)
{
	static int pos[8];		/* a few concurrent walks */
	int i;

	for (i = 0; i < 8; i++)
		if (pos[i] == 0) {
			pos[i] = 1;	/* 1 + index of the next entry */
			return &pos[i];
		}
	return 0;
}

struct netconfig *
getnetconfig(void *h)
{
	int *p = h;

	if (p == 0 || *p < 1 || *p > NNC)
		return 0;
	return &nc_inet[(*p)++ - 1];
}

int
endnetconfig(void *h)
{
	if (h)
		*(int *)h = 0;
	return 0;
}

/* gethostname() is in no static library */
int
gethostname(char *name, int len)
{
	struct utsname u;

	if (uname(&u) < 0)
		return -1;
	strncpy(name, u.nodename, len);
	if (len > 0)
		name[len - 1] = '\0';
	return 0;
}

/* libsocket.a's resolver (res_init) asks for the NIS domain; there is none */
int
getdomainname(char *name, int len)
{
	if (len > 0)
		name[0] = '\0';
	return 0;
}

/* in no static library */
char *
inet_ntoa(struct in_addr in)
{
	static char buf[16];
	unsigned char *b = (unsigned char *)&in;

	sprintf(buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
	return buf;
}

/* C99, in no library */
long long
strtoll(const char *s, char **end, int base)
{
	const char *p = s;
	unsigned long long v = 0, lim;
	int neg = 0, d, any = 0, over = 0;

	while (isspace((unsigned char)*p))
		p++;
	if (*p == '-' || *p == '+')
		neg = *p++ == '-';
	if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		p += 2;
		base = 16;
	} else if (base == 0)
		base = *p == '0' ? 8 : 10;
	lim = neg ? (unsigned long long)LLONG_MAX + 1 : (unsigned long long)LLONG_MAX;
	for (;; p++) {
		if (isdigit((unsigned char)*p))
			d = *p - '0';
		else if (isalpha((unsigned char)*p))
			d = tolower((unsigned char)*p) - 'a' + 10;
		else
			break;
		if (d >= base)
			break;
		any = 1;
		if (v > (lim - d) / base)
			over = 1;
		else
			v = v * base + d;
	}
	if (end)
		*end = (char *)(any ? p : s);
	if (over) {
		errno = ERANGE;
		return neg ? LLONG_MIN : LLONG_MAX;
	}
	return neg ? -(long long)v : (long long)v;
}
