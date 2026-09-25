/*
 * ttest - exercise a transputer through the tlk driver.
 *
 *   ttest [-p pace] [/dev/link0|/dev/link1]   default /dev/link1 (the FPGA's T425)
 *   -p: set the link's poll pace first (TLK_PACE, root)
 *
 * Resets the transputer, POKEs a word and PEEKs it back, boots the
 * Programmer's Manual's type probe, then boots its read test (a program
 * that sends 1..10 over and over) and times reading 64 KB of it.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/ioctl.h>
#include "../../driver-tlk/tlk.h"

static const unsigned char typeprobe[] = {
	23, 177, 209, 36, 242, 33, 252, 36, 242, 33, 248,
	240, 96, 92, 42, 42, 42, 74, 255, 33, 47, 255, 2, 0
};
static const unsigned char readtest[] = {	/* sends 1..10 forever */
	32, 181, 36, 242, 33, 248, 36, 242, 33, 252, 37, 247,
	34, 249, 70, 33, 251, 36, 242, 74, 251, 96, 7, 1, 2, 3,
	4, 5, 6, 7, 8, 9, 10
};

static int fd;

static int wr(const void *p, int n)
{
	int w = write(fd, p, n);
	if (w != n) { printf("write: %d of %d (errno %d)\n", w, n, errno); return -1; }
	return 0;
}

static void le(unsigned char *b, unsigned long w)
{
	b[0] = w; b[1] = w >> 8; b[2] = w >> 16; b[3] = w >> 24;
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/link1";
	int pace = -1, a;
	unsigned char b[9], buf[4096];
	long total, t0, t1, hz = sysconf(_SC_CLK_TCK);
	struct tms tm;
	int n, i, st, bad, calls;
	unsigned long w;

	for (a = 1; a < argc; a++) {
		if (strcmp(argv[a], "-p") == 0 && a + 1 < argc) pace = atoi(argv[++a]);
		else dev = argv[a];
	}
	setvbuf(stdout, 0, _IONBF, 0);
	if ((fd = open(dev, O_RDWR)) < 0) { printf("%s: errno %d\n", dev, errno); return 1; }
	if (pace >= 0 && ioctl(fd, TLK_PACE, pace) < 0) printf("TLK_PACE: errno %d\n", errno);
	st = ioctl(fd, TLK_STATUS, 0);
	printf("%s: status in %d out %d error %d\n", dev, !!(st & TLK_ST_IN), !!(st & TLK_ST_OUT), !!(st & TLK_ST_ERROR));
	ioctl(fd, TLK_TIMEOUT, 500);

	ioctl(fd, TLK_RESET, 0);
	b[0] = 0; le(b + 1, 0x80000100UL); le(b + 5, 0x12345678UL);
	if (wr(b, 9)) return 1;
	b[0] = 1;
	if (wr(b, 5)) return 1;
	for (n = 0; n < 4; n += i)
		if ((i = read(fd, b + n, 4 - n)) <= 0) { printf("PEEK: %d bytes, then errno %d\n", n, errno); return 1; }
	w = b[0] | b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
	printf("POKE/PEEK: read %08lx %s\n", w, w == 0x12345678UL ? "ok" : "WRONG");

	ioctl(fd, TLK_RESET, 0);
	if (wr(typeprobe, sizeof typeprobe)) return 1;
	n = 0;
	while (n < 8 && (i = read(fd, buf + n, 8 - n)) > 0)
		n += i;
	printf("type probe: %d bytes ->%s\n", n, n == 1 ? " C004" : n == 2 ? " 16-bit" : n == 4 ? " 32-bit transputer" : " nothing");

	ioctl(fd, TLK_RESET, 0);
	if (wr(readtest, sizeof readtest)) return 1;
	t0 = times(&tm);
	total = 0; bad = 0; calls = 0;
	while (total < 65536) {
		if ((n = read(fd, buf, sizeof buf)) <= 0) { printf("read test: stopped at %ld (errno %d)\n", total, errno); break; }
		for (i = 0; i < n; i++)
			if (buf[i] != (unsigned char)((total + i) % 10 + 1)) bad++;
		total += n;
		calls++;
	}
	t1 = times(&tm);
	if (t1 == t0) t1++;
	printf("read test: %ld bytes in %ld ms = %ld KB/s, %d out of sequence, %d reads\n",
	    total, (t1 - t0) * 1000 / hz, total * hz / (t1 - t0) / 1024, bad, calls);

	/* write speed: POKE commands (9 bytes each) into a transputer that
	 * waits to boot, 64 KB of them */
	ioctl(fd, TLK_RESET, 0);
	{
		static unsigned char pk[9 * 455];
		for (i = 0; i < 455; i++) {
			pk[9 * i] = 0;
			le(pk + 9 * i + 1, 0x80000100UL);
			le(pk + 9 * i + 5, (unsigned long)i);
		}
		t0 = times(&tm);
		for (total = 0; total < 65536; total += sizeof pk)
			if (wr(pk, sizeof pk)) break;
		t1 = times(&tm);
		if (t1 == t0) t1++;
		printf("write test: %ld bytes in %ld ms = %ld KB/s",
		    total, (t1 - t0) * 1000 / hz, total * hz / (t1 - t0) / 1024);
		/* the last POKE wrote 454: PEEK it back */
		b[0] = 1; le(b + 1, 0x80000100UL);
		if (wr(b, 5) == 0) {
			for (n = 0; n < 4; n += i)
				if ((i = read(fd, b + n, 4 - n)) <= 0) break;
			w = b[0] | b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
			printf(", PEEK after it: %lu %s\n", w, w == 454 ? "ok" : "WRONG");
		}
	}
	ioctl(fd, TLK_RESET, 0);
	close(fd);
	return 0;
}
