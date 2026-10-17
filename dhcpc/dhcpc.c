/*
 * dhcpc - a DHCP client for Atari System V.  It talks DLPI to the
 * Ethernet driver (/dev/en0, the DaynaPORT; /dev/nu0, the NetUSBee)
 * and does the DISCOVER/OFFER/REQUEST/ACK exchange itself, then prints
 * what the server gave, or writes it where ASV's own network startup
 * reads it from (-w).  ASV's TCP/IP has no DHCP; this is all there is.
 *
 *   dhcpc [-n | -w] [-t SECS] [-v] DEVICE
 *	-n	print the lease only (default)
 *	-w	write it: the node's line in /etc/inet/hosts.net (so that
 *		rc.inet's "ifconfig en0 `uname -n`" picks the address),
 *		/etc/inet/dhcp.sh (netmask, broadcast, default route: run
 *		after rc.inet), /etc/resolv.conf
 *	-t	give up after SECS seconds (default 30)
 *	-v	trace
 *
 * It must run while nothing else has the IP SAP on that device: before
 * /etc/init.d/inetinit's slink (the drivers allow one stream per SAP).
 * Built with the TT's cc or the sysv4 cross compiler:
 *   cc -O -o dhcpc dhcpc.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <stropts.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/dlpi.h>

#define ETHERTYPE_IP	0x800
#define BOOTPS		67
#define BOOTPC		68
#define DHCP_DISCOVER	1
#define DHCP_OFFER	2
#define DHCP_REQUEST	3
#define DHCP_ACK	5
#define DHCP_NAK	6

#define HOSTS		"/etc/inet/hosts.net"
#define DHCPSH		"/etc/inet/dhcp.sh"
#define RESOLV		"/etc/resolv.conf"

static int verbose;
static int fd;
static unsigned char mac[6];
static unsigned char xid[4];
static char node[64];

/* what the server gave */
struct lease {
	unsigned char	addr[4], mask[4], router[4], server[4];
	unsigned char	dns[3][4];
	int		ndns;
	char		domain[128];
	unsigned long	secs;
};

static void
trace(const char *s)
{
	if (verbose)
		fprintf(stderr, "dhcpc: %s\n", s);
}

static const char *
ip(const unsigned char *a)
{
	static char buf[4][20];
	static int n;
	char *p = buf[n++ & 3];

	sprintf(p, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
	return p;
}

/* ------------------------------------------------------------- DLPI */

/* one control message and its answer */
static int
dlreq(void *req, int len, void *ack, int acklen, int want)
{
	struct strbuf ctl;
	int flags = 0;

	ctl.buf = (char *)req;
	ctl.len = len;
	if (putmsg(fd, &ctl, (struct strbuf *)0, 0) < 0) {
		perror("dhcpc: putmsg");
		return -1;
	}
	ctl.buf = (char *)ack;
	ctl.maxlen = acklen;
	if (getmsg(fd, &ctl, (struct strbuf *)0, &flags) < 0) {
		perror("dhcpc: getmsg");
		return -1;
	}
	if (ctl.len < 4 || *(unsigned long *)ack != (unsigned long)want) {
		if (ctl.len >= (int)sizeof(dl_error_ack_t) &&
		    *(unsigned long *)ack == DL_ERROR_ACK) {
			dl_error_ack_t *e = (dl_error_ack_t *)ack;

			fprintf(stderr, "dhcpc: DLPI request %lx: error %lx, errno %ld\n",
			    e->dl_error_primitive, e->dl_errno, e->dl_unix_errno);
		} else
			fprintf(stderr, "dhcpc: unexpected DLPI answer %lx\n",
			    *(unsigned long *)ack);
		return -1;
	}
	return 0;
}

static int
dlbind(void)
{
	dl_bind_req_t br;
	unsigned char ack[64];
	dl_bind_ack_t *ba = (dl_bind_ack_t *)ack;

	memset(&br, 0, sizeof br);
	br.dl_primitive = DL_BIND_REQ;
	br.dl_sap = ETHERTYPE_IP;
	br.dl_service_mode = DL_CLDLS;
	if (dlreq(&br, sizeof br, ack, sizeof ack, DL_BIND_ACK) < 0)
		return -1;
	if (ba->dl_addr_length < 6) {
		fprintf(stderr, "dhcpc: the driver gave no Ethernet address\n");
		return -1;
	}
	memcpy(mac, ack + ba->dl_addr_offset, 6);
	return 0;
}

/* an IP datagram to the broadcast address */
static int
dlsend(const unsigned char *pkt, int len)
{
	unsigned char ctlbuf[sizeof(dl_unitdata_req_t) + 6];
	dl_unitdata_req_t *ur = (dl_unitdata_req_t *)ctlbuf;
	struct strbuf ctl, data;

	memset(ctlbuf, 0, sizeof ctlbuf);
	ur->dl_primitive = DL_UNITDATA_REQ;
	ur->dl_dest_addr_length = 6;
	ur->dl_dest_addr_offset = sizeof(dl_unitdata_req_t);
	memset(ctlbuf + sizeof(dl_unitdata_req_t), 0xff, 6);
	ctl.buf = (char *)ctlbuf;
	ctl.len = sizeof ctlbuf;
	data.buf = (char *)pkt;
	data.len = len;
	if (putmsg(fd, &ctl, &data, 0) < 0) {
		perror("dhcpc: putmsg data");
		return -1;
	}
	return 0;
}

/* the next frame within ms milliseconds: its IP datagram, or -1 */
static int
dlrecv(unsigned char *buf, int size, int ms)
{
	unsigned char ctlbuf[128];
	struct strbuf ctl, data;
	struct pollfd pf;
	int flags = 0;

	pf.fd = fd;
	pf.events = POLLIN | POLLPRI;
	if (poll(&pf, 1, ms) <= 0)
		return -1;
	ctl.buf = (char *)ctlbuf;
	ctl.maxlen = sizeof ctlbuf;
	data.buf = (char *)buf;
	data.maxlen = size;
	if (getmsg(fd, &ctl, &data, &flags) < 0) {
		if (errno == EINTR)
			return -1;
		perror("dhcpc: getmsg data");
		return -1;
	}
	if (ctl.len < (int)sizeof(dl_unitdata_ind_t) ||
	    *(unsigned long *)ctlbuf != DL_UNITDATA_IND)
		return -1;
	return data.len;
}

/* ------------------------------------------------------------- DHCP */

static unsigned
cksum(const unsigned char *p, int n)
{
	unsigned long s = 0;

	for (; n > 1; n -= 2, p += 2)
		s += (p[0] << 8) | p[1];
	if (n)
		s += p[0] << 8;
	while (s >> 16)
		s = (s & 0xffff) + (s >> 16);
	return ~s & 0xffff;
}

/*
 * A DHCP message of type t, from address 0 to the broadcast address.
 * REQUEST carries the offered address and the server's.
 */
static int
build(unsigned char *pkt, int t, const struct lease *l)
{
	unsigned char *ipp = pkt, *udp = ipp + 20, *u = udp + 8;
	int i, ulen, n;
	unsigned c;

	memset(pkt, 0, 20 + 8 + 300);
	u[0] = 1;				/* BOOTREQUEST */
	u[1] = 1;				/* Ethernet */
	u[2] = 6;
	memcpy(u + 4, xid, 4);
	u[10] = 0x80;				/* broadcast the reply */
	memcpy(u + 28, mac, 6);
	u[236] = 99, u[237] = 130, u[238] = 83, u[239] = 99;
	i = 240;
	u[i++] = 53, u[i++] = 1, u[i++] = t;
	if (t == DHCP_REQUEST) {
		u[i++] = 50, u[i++] = 4;
		memcpy(u + i, l->addr, 4), i += 4;
		u[i++] = 54, u[i++] = 4;
		memcpy(u + i, l->server, 4), i += 4;
	}
	u[i++] = 55, u[i++] = 5, u[i++] = 1, u[i++] = 3, u[i++] = 6, u[i++] = 15, u[i++] = 51;
	if ((n = strlen(node)) > 0 && n < 64) {
		u[i++] = 12, u[i++] = n;
		memcpy(u + i, node, n), i += n;
	}
	u[i++] = 255;
	if (i < 300)
		i = 300;			/* the BOOTP minimum */
	ulen = 8 + i;
	udp[1] = BOOTPC;
	udp[3] = BOOTPS;
	udp[4] = ulen >> 8;
	udp[5] = ulen;
	ipp[0] = 0x45;
	ipp[2] = (20 + ulen) >> 8;
	ipp[3] = 20 + ulen;
	ipp[8] = 64;
	ipp[9] = 17;
	memset(ipp + 16, 0xff, 4);
	c = cksum(ipp, 20);
	ipp[10] = c >> 8;
	ipp[11] = c;
	return 20 + ulen;
}

/*
 * A received datagram: a DHCP reply to our xid?  Its type, with the
 * lease filled in, or -1.
 */
static int
parse(const unsigned char *pkt, int len, struct lease *l)
{
	const unsigned char *u, *o, *end;
	int hl, t = -1, n;

	if (len < 20 || (pkt[0] >> 4) != 4 || pkt[9] != 17)
		return -1;
	hl = (pkt[0] & 15) * 4;
	if (len < hl + 8 + 240)
		return -1;
	u = pkt + hl + 8;
	if (((pkt[hl + 2] << 8) | pkt[hl + 3]) != BOOTPC)
		return -1;
	if (u[0] != 2 || memcmp(u + 4, xid, 4) != 0 || memcmp(u + 28, mac, 6) != 0)
		return -1;
	if (u[236] != 99 || u[237] != 130 || u[238] != 83 || u[239] != 99)
		return -1;
	memset(l, 0, sizeof *l);
	memcpy(l->addr, u + 16, 4);			/* yiaddr */
	end = pkt + len;
	for (o = u + 240; o + 1 < end && *o != 255; o += 2 + n) {
		if (*o == 0) {
			n = -1;
			continue;
		}
		n = o[1];
		if (o + 2 + n > end)
			break;
		switch (*o) {
		case 1:
			if (n >= 4)
				memcpy(l->mask, o + 2, 4);
			break;
		case 3:
			if (n >= 4)
				memcpy(l->router, o + 2, 4);
			break;
		case 6:
			for (l->ndns = 0; l->ndns < 3 && 4 * (l->ndns + 1) <= n; l->ndns++)
				memcpy(l->dns[l->ndns], o + 2 + 4 * l->ndns, 4);
			break;
		case 15:
			if (n > 0 && n < (int)sizeof l->domain) {
				memcpy(l->domain, o + 2, n);
				l->domain[n] = '\0';
			}
			break;
		case 51:
			if (n >= 4)
				l->secs = ((unsigned long)o[2] << 24) | (o[3] << 16) |
				    (o[4] << 8) | o[5];
			break;
		case 53:
			if (n >= 1)
				t = o[2];
			break;
		case 54:
			if (n >= 4)
				memcpy(l->server, o + 2, 4);
			break;
		}
	}
	return t;
}

/* send msg, wait up to ms for a reply of type want (or NAK): its type */
static int
exchange(int msg, int want, struct lease *l, int ms)
{
	unsigned char pkt[20 + 8 + 320], in[1600];
	struct lease got;
	time_t end;
	int n, t;

	n = build(pkt, msg, l);
	if (dlsend(pkt, n) < 0)
		return -1;
	trace(msg == DHCP_DISCOVER ? "discover sent" : "request sent");
	end = time((time_t *)0) + (ms + 999) / 1000;
	while (time((time_t *)0) < end) {
		n = dlrecv(in, sizeof in, 1000);
		if (n <= 0)
			continue;
		t = parse(in, n, &got);
		if (t == want || t == DHCP_NAK) {
			*l = got;
			return t;
		}
	}
	return -1;
}

/* ------------------------------------------------------------- output */

static void
show(const struct lease *l)
{
	int i;

	printf("address %s netmask %s", ip(l->addr), ip(l->mask));
	if (l->router[0] | l->router[1] | l->router[2] | l->router[3])
		printf(" router %s", ip(l->router));
	for (i = 0; i < l->ndns; i++)
		printf(" dns %s", ip(l->dns[i]));
	if (l->domain[0])
		printf(" domain %s", l->domain);
	printf(" server %s lease %lus\n", ip(l->server), l->secs);
}

/* hosts.net: the node's line gets the new address, its aliases kept */
static int
writehosts(const struct lease *l)
{
	char tmp[128], line[512], word[128];
	FILE *in, *out;
	int done = 0;

	sprintf(tmp, "%s.dhcp", HOSTS);
	if ((in = fopen(HOSTS, "r")) == (FILE *)0) {
		perror(HOSTS);
		return -1;
	}
	if ((out = fopen(tmp, "w")) == (FILE *)0) {
		perror(tmp);
		fclose(in);
		return -1;
	}
	while (fgets(line, sizeof line, in) != (char *)0) {
		char *rest;

		/* "ADDR NAME aliases...": is NAME the node? */
		if (line[0] != '#' && sscanf(line, "%127s %127s", word, word) == 2 &&
		    strcmp(word, node) == 0 && !done) {
			rest = line;
			while (*rest == ' ' || *rest == '\t') rest++;
			while (*rest && *rest != ' ' && *rest != '\t') rest++;
			fprintf(out, "%s%s", ip(l->addr), rest);
			done = 1;
		} else
			fputs(line, out);
	}
	if (!done)
		fprintf(out, "%s\t%s\n", ip(l->addr), node);
	fclose(in);
	if (fclose(out) != 0 || rename(tmp, HOSTS) < 0) {
		perror(HOSTS);
		return -1;
	}
	return 0;
}

static int
writerest(const struct lease *l, const char *dev)
{
	FILE *f;
	unsigned char bc[4];
	int i;
	const char *ifname;

	/* /dev/en0 -> en0 */
	ifname = strrchr(dev, '/');
	ifname = ifname ? ifname + 1 : dev;
	for (i = 0; i < 4; i++)
		bc[i] = l->addr[i] | ~l->mask[i];
	if ((f = fopen(DHCPSH, "w")) == (FILE *)0) {
		perror(DHCPSH);
		return -1;
	}
	fprintf(f, "# written by dhcpc %s: run after rc.inet\n", ip(l->addr));
	fprintf(f, "/usr/sbin/ifconfig %s netmask %s broadcast %s\n", ifname,
	    ip(l->mask), ip(bc));
	if (l->router[0] | l->router[1] | l->router[2] | l->router[3])
		fprintf(f, "/usr/sbin/route add default %s 1\n", ip(l->router));
	fclose(f);
	if (l->ndns > 0) {
		if ((f = fopen(RESOLV, "w")) == (FILE *)0) {
			perror(RESOLV);
			return -1;
		}
		if (l->domain[0])
			fprintf(f, "domain %s\n", l->domain);
		for (i = 0; i < l->ndns; i++)
			fprintf(f, "nameserver %s\n", ip(l->dns[i]));
		fclose(f);
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct lease l;
	struct utsname un;
	const char *dev = (const char *)0;
	int write = 0, secs = 30, i, t = -1, wait;
	time_t t0, end;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-n") == 0)
			write = 0;
		else if (strcmp(argv[i], "-w") == 0)
			write = 1;
		else if (strcmp(argv[i], "-v") == 0)
			verbose = 1;
		else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
			secs = atoi(argv[++i]);
		else if (argv[i][0] != '-' && dev == (const char *)0)
			dev = argv[i];
		else {
			fprintf(stderr, "usage: dhcpc [-n | -w] [-t SECS] [-v] /dev/en0\n");
			return 2;
		}
	}
	if (dev == (const char *)0)
		dev = "/dev/en0";
	if (uname(&un) < 0)
		strcpy(node, "noname");
	else
		strncpy(node, un.nodename, sizeof node - 1);
	if ((fd = open(dev, O_RDWR)) < 0) {
		perror(dev);
		return 1;
	}
	if (dlbind() < 0)
		return 1;
	if (verbose)
		fprintf(stderr, "dhcpc: %s, %02x:%02x:%02x:%02x:%02x:%02x, node %s\n",
		    dev, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], node);
	t0 = time((time_t *)0);
	i = (int)t0 ^ (int)getpid() << 8;
	xid[0] = i >> 24, xid[1] = i >> 16, xid[2] = i >> 8, xid[3] = i;
	end = t0 + secs;
	for (wait = 2; time((time_t *)0) < end; wait = wait < 8 ? wait * 2 : 8) {
		memset(&l, 0, sizeof l);
		t = exchange(DHCP_DISCOVER, DHCP_OFFER, &l, wait * 1000);
		if (t != DHCP_OFFER)
			continue;
		if (verbose)
			fprintf(stderr, "dhcpc: offer %s from %s\n", ip(l.addr), ip(l.server));
		t = exchange(DHCP_REQUEST, DHCP_ACK, &l, wait * 1000);
		if (t == DHCP_ACK)
			break;
		trace(t == DHCP_NAK ? "request refused (NAK)" : "no ack");
		xid[3]++;
	}
	close(fd);
	if (t != DHCP_ACK) {
		fprintf(stderr, "dhcpc: no DHCP server answered on %s in %d s\n", dev, secs);
		return 1;
	}
	if (l.mask[0] == 0)			/* no netmask given: classful */
		l.mask[0] = 255, l.mask[1] = l.addr[0] >= 128 ? 255 : 0,
		    l.mask[2] = l.addr[0] >= 192 ? 255 : 0;
	show(&l);
	if (write && (writehosts(&l) < 0 || writerest(&l, dev) < 0))
		return 1;
	return 0;
}
