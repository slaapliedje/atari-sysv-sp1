/*
 * tspy - what is on the other links of the transputer behind a link.
 *
 *   tspy [/dev/link0]
 *
 * Marks the FPGA's T425 first (POKEs 0xC0FFEE25 at 0x80000100 through
 * /dev/link1, and leaves it waiting to boot), then boots linkspy.tas on
 * the transputer behind the given link. That program PEEKs 0x80000100
 * down each of its links 1-3 and reports which answered, and with what:
 * the marker means that link leads to the T425.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "../../driver-tlk/tlk.h"
#include "linkspy.h"

#define MARK	0xC0FFEE25UL

static void le(unsigned char *b, unsigned long w)
{
	b[0] = w; b[1] = w >> 8; b[2] = w >> 16; b[3] = w >> 24;
}

static int getword(int fd, unsigned long *w)
{
	unsigned char b[4];
	int n, i;

	for (n = 0; n < 4; n += i)
		if ((i = read(fd, b + n, 4 - n)) <= 0)
			return -1;
	*w = b[0] | b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
	return 0;
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/link0";
	unsigned char poke[9];
	unsigned long w[6];
	int fd, k;

	setvbuf(stdout, 0, _IONBF, 0);
	if (strcmp(dev, "/dev/link1") != 0 && (fd = open("/dev/link1", O_RDWR)) >= 0) {
		ioctl(fd, TLK_TIMEOUT, 500);
		ioctl(fd, TLK_RESET, 0);
		poke[0] = 0; le(poke + 1, 0x80000100UL); le(poke + 5, MARK);
		if (write(fd, poke, 9) == 9)
			printf("marked the FPGA's T425 (0x80000100 = %08lx)\n", MARK);
		close(fd);
	}
	if ((fd = open(dev, O_RDWR)) < 0) { printf("%s: errno %d\n", dev, errno); return 1; }
	ioctl(fd, TLK_TIMEOUT, 1000);
	ioctl(fd, TLK_RESET, 0);
	if (write(fd, linkspy, sizeof linkspy) != sizeof linkspy) { printf("boot failed (errno %d)\n", errno); return 1; }
	for (k = 0; k < 6; k++)
		if (getword(fd, &w[k])) { printf("no report from the spy (%d words)\n", k); ioctl(fd, TLK_RESET, 0); return 1; }
	printf("%s: link 0 is this host\n", dev);
	for (k = 1; k <= 3; k++) {
		unsigned long flag = w[2 * k - 2], val = w[2 * k - 1];
		printf("%s: link %d: ", dev, k);
		if (flag != 1)
			printf("nothing answered (flag %lx)\n", flag);
		else if (val == MARK)
			printf("the FPGA's T425 (PEEK = the marker)\n");
		else
			printf("a transputer waiting to boot (PEEK 0x80000100 = %08lx)\n", val);
	}
	ioctl(fd, TLK_RESET, 0);
	return 0;
}
