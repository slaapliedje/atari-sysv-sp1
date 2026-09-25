/*
 * tdiag - where a link's time goes, timed inside the tlk driver.
 *
 *   tdiag [/dev/link0|/dev/link1]
 *
 * Input: boots the manual's read test (it sends 1..10 forever) and prints
 * how long each of 64 bytes took to arrive. Output: resets, then streams
 * POKE commands and prints how long the link took to take each byte.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "../../driver-tlk/tlk.h"

static const unsigned char readtest[] = {
	32, 181, 36, 242, 33, 248, 36, 242, 33, 252, 37, 247,
	34, 249, 70, 33, 251, 36, 242, 74, 251, 96, 7, 1, 2, 3,
	4, 5, 6, 7, 8, 9, 10
};

static void show(const char *what, struct tlk_diag *d)
{
	long sum = 0, max = 0;
	int i;

	printf("%s: %d bytes timed, a status poll costs %d ns\n", what, d->n, d->nspoll);
	for (i = 0; i < d->n; i++) {
		long us = (long)d->polls[i] * d->nspoll / 1000;
		sum += us;
		if (us > max) max = us;
		printf("%6ld%s", us, i % 10 == 9 ? "\n" : " ");
	}
	if (d->n)
		printf("\n  (us per byte) mean %ld, max %ld, first %ld\n",
		    sum / d->n, max, (long)d->polls[0] * d->nspoll / 1000);
	if (d->mode == 1) {
		printf("  bytes:");
		for (i = 0; i < d->n; i++)
			printf(" %d", d->data[i]);
		printf("\n");
	}
}

static void raw(int fd, int mode, const char *what)
{
	struct tlk_diag d;
	int i;

	d.mode = mode;
	if (ioctl(fd, TLK_DIAG, &d) < 0) { printf("TLK_DIAG %d: errno %d\n", mode, errno); return; }
	printf("%s (waited %d polls), status right after:", what, d.polls[0]);
	for (i = 0; i < 64; i++)
		printf("%s%d", i % 32 ? "" : "\n  ", d.data[i] & 1);
	printf("\n");
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/link1";
	struct tlk_diag d;
	int fd;

	setvbuf(stdout, 0, _IONBF, 0);
	if ((fd = open(dev, O_RDWR)) < 0) { printf("%s: errno %d\n", dev, errno); return 1; }
	ioctl(fd, TLK_TIMEOUT, 500);

	ioctl(fd, TLK_RESET, 0);
	if (write(fd, readtest, sizeof readtest) != sizeof readtest) { printf("boot failed\n"); return 1; }
	d.mode = 1;
	if (ioctl(fd, TLK_DIAG, &d) < 0) { printf("TLK_DIAG: errno %d\n", errno); return 1; }
	show("input", &d);

	ioctl(fd, TLK_RESET, 0);
	d.mode = 2;
	if (ioctl(fd, TLK_DIAG, &d) < 0) { printf("TLK_DIAG: errno %d\n", errno); return 1; }
	show("output (POKEs)", &d);

	ioctl(fd, TLK_RESET, 0);
	if (write(fd, readtest, sizeof readtest) != sizeof readtest) { printf("boot failed\n"); return 1; }
	raw(fd, 3, "one input byte read");
	ioctl(fd, TLK_RESET, 0);
	raw(fd, 4, "one output byte written");

	/* input again, pausing between status polls */
	{
		static int pauses[] = { 10, 100, 1000 };
		int k;
		for (k = 0; k < 3; k++) {
			char what[64];
			ioctl(fd, TLK_RESET, 0);
			if (write(fd, readtest, sizeof readtest) != sizeof readtest) { printf("boot failed\n"); return 1; }
			d.mode = 5;
			d.n = pauses[k];
			if (ioctl(fd, TLK_DIAG, &d) < 0) { printf("TLK_DIAG 5: errno %d\n", errno); return 1; }
			sprintf(what, "input, %d loops between polls", pauses[k]);
			d.mode = 1;
			show(what, &d);
		}
	}
	ioctl(fd, TLK_RESET, 0);
	return 0;
}
