/*
 * tmem - how much memory the transputer behind a link has, by POKE/PEEK.
 *
 *   tmem [-r] [/dev/link0|/dev/link1]     -r: the transputer beyond relay.tas
 *
 * After a reset the transputer takes POKE/PEEK commands without running
 * anything. tmem writes a distinct marker at 0x80000000 + 2^n for n = 12
 * (the first external address past the 4 KB on-chip RAM) to 24, then
 * reads them all back. RAM ends where a marker does not come back - or
 * comes back overwritten by a later one that wrapped around onto it.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "../../driver-tlk/tlk.h"
#include "relay.h"

#define N0	12
#define N1	24

static int fd;

static void le(unsigned char *b, unsigned long w)
{
	b[0] = w; b[1] = w >> 8; b[2] = w >> 16; b[3] = w >> 24;
}

static int poke(unsigned long a, unsigned long v)
{
	unsigned char b[9];
	b[0] = 0; le(b + 1, a); le(b + 5, v);
	return write(fd, b, 9) == 9 ? 0 : -1;
}

static int peek(unsigned long a, unsigned long *v)
{
	unsigned char b[5];
	int n, i;
	b[0] = 1; le(b + 1, a);
	if (write(fd, b, 5) != 5)
		return -1;
	for (n = 0; n < 4; n += i)
		if ((i = read(fd, b + n, 4 - n)) <= 0)
			return -1;
	*v = b[0] | b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
	return 0;
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/link0";
	int viarelay = 0, a, n, top = N0 - 1;
	unsigned long v;

	for (a = 1; a < argc; a++) {
		if (strcmp(argv[a], "-r") == 0) viarelay = 1;
		else dev = argv[a];
	}
	setvbuf(stdout, 0, _IONBF, 0);
	if ((fd = open(dev, O_RDWR)) < 0) { printf("%s: errno %d\n", dev, errno); return 1; }
	ioctl(fd, TLK_TIMEOUT, 500);
	ioctl(fd, TLK_RESET, 0);
	if (viarelay && write(fd, relay, sizeof relay) != sizeof relay) { printf("relay boot failed\n"); return 1; }
	for (n = N0; n <= N1; n++)
		if (poke(0x80000000UL + (1UL << n), 0x5A000000UL | n)) { printf("POKE failed at 2^%d\n", n); return 1; }
	for (n = N0; n <= N1; n++) {
		if (peek(0x80000000UL + (1UL << n), &v)) { printf("PEEK failed at 2^%d\n", n); return 1; }
		printf("  0x%08lx: %08lx%s\n", 0x80000000UL + (1UL << n), v,
		    v == (0x5A000000UL | n) ? "" : "  <- not what was written");
		if (v == (0x5A000000UL | n) && top == n - 1)
			top = n;
	}
	if (top < N0)
		printf("%s%s: no external RAM found\n", dev, viarelay ? " (via relay)" : "");
	else
		printf("%s%s: RAM reaches at least 0x%08lx: %lu KB (the next power of two failed)\n",
		    dev, viarelay ? " (via relay)" : "", 0x80000000UL + (1UL << top) + 3, (1UL << (top + 1)) / 1024);
	ioctl(fd, TLK_RESET, 0);
	return 0;
}
