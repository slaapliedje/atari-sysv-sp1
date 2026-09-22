/*
 * dptest - exercise the dp raw-frame device from user space.
 *
 *	cc -O -o dptest dptest.c
 *	dptest listen			print the next frames seen on the LAN
 *	dptest arp MY.IP TARGET.IP	send an ARP who-has, wait for the reply
 *
 * MY.IP is any UNUSED address on your LAN (nothing is configured with it;
 * it only fills the ARP sender field). TARGET.IP is something that answers,
 * e.g. your router.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#define DPGETMAC	(('D' << 8) | 1)

static unsigned char mac[6];
static unsigned char frame[1600];

static void
delay(ms)
int ms;
{
	(void) poll((struct pollfd *)0, 0, ms);
}

static int
parse_ip(s, ip)
char *s;
unsigned char *ip;
{
	int a, b, c, d;

	if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
		return -1;
	ip[0] = a; ip[1] = b; ip[2] = c; ip[3] = d;
	return 0;
}

static void
show(n)
int n;
{
	int type;

	type = (frame[12] << 8) | frame[13];
	printf("%4d bytes  %02x:%02x:%02x:%02x:%02x:%02x > %02x:%02x:%02x:%02x:%02x:%02x  type %04x",
	    n, frame[6], frame[7], frame[8], frame[9], frame[10], frame[11],
	    frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], type);
	if (type == 0x0806 && n >= 42)
		printf("  ARP %s %d.%d.%d.%d -> %d.%d.%d.%d",
		    frame[21] == 2 ? "reply" : "request",
		    frame[28], frame[29], frame[30], frame[31],
		    frame[38], frame[39], frame[40], frame[41]);
	printf("\n");
}

int
main(argc, argv)
int argc;
char **argv;
{
	int fd, n, tries, seen;
	unsigned char myip[4], tip[4];
	int arp;

	arp = (argc == 4 && strcmp(argv[1], "arp") == 0);
	if (!arp && !(argc == 2 && strcmp(argv[1], "listen") == 0)) {
		fprintf(stderr, "usage: dptest listen | dptest arp MY.IP TARGET.IP\n");
		return 2;
	}
	if (arp && (parse_ip(argv[2], myip) < 0 || parse_ip(argv[3], tip) < 0)) {
		fprintf(stderr, "bad IP address\n");
		return 2;
	}
	fd = open("/dev/dp", O_RDWR);
	if (fd < 0) {
		printf("open /dev/dp failed, errno %d\n", errno);
		return 1;
	}
	if (ioctl(fd, DPGETMAC, mac) < 0) {
		printf("DPGETMAC failed, errno %d\n", errno);
		return 1;
	}
	printf("MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
	    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	if (arp) {
		memset(frame, 0, 60);
		memset(frame, 0xff, 6);			/* broadcast */
		memcpy(frame + 6, mac, 6);
		frame[12] = 0x08; frame[13] = 0x06;	/* ARP */
		frame[14] = 0; frame[15] = 1;		/* ethernet */
		frame[16] = 0x08; frame[17] = 0;	/* IPv4 */
		frame[18] = 6; frame[19] = 4;
		frame[20] = 0; frame[21] = 1;		/* request */
		memcpy(frame + 22, mac, 6);
		memcpy(frame + 28, myip, 4);
		memcpy(frame + 38, tip, 4);
		n = write(fd, (char *)frame, 42);
		printf("sent ARP who-has %s: write returned %d (errno %d)\n",
		    argv[3], n, n < 0 ? errno : 0);
	}

	seen = 0;
	for (tries = 0; tries < 100 && seen < 20; tries++) {	/* ~10 s */
		n = read(fd, (char *)frame, sizeof frame);
		if (n < 0) {
			printf("read failed, errno %d\n", errno);
			break;
		}
		if (n == 0) {
			delay(100);
			continue;
		}
		seen++;
		show(n);
		if (arp && n >= 42 && frame[12] == 0x08 && frame[13] == 0x06 &&
		    frame[21] == 2 && memcmp(frame + 28, tip, 4) == 0) {
			printf("ARP REPLY: %s is at %02x:%02x:%02x:%02x:%02x:%02x\n",
			    argv[3], frame[22], frame[23], frame[24],
			    frame[25], frame[26], frame[27]);
			break;
		}
	}
	printf("%d frame(s) seen in %d poll(s)\n", seen, tries);
	close(fd);
	return 0;
}
