/*
 * tfpu - which transputer is on a link: word size, FPU, device id.
 *
 *   tfpu [-n] [/dev/link0|/dev/link1]    -n: skip the FPU instructions
 *                                        (a T4xx stops on them)
 *
 * Boots a hand-assembled program (below) that answers with two words:
 *  1. a marker word 0x80000000 after "fpldzerosn; fpstnlsn" was aimed at
 *     it: a T8xx FPU overwrites it with 0.0 (0x00000000), a T4xx has no
 *     FPU and leaves it alone;
 *  2. the A register after "ldc 0; ldc 0; ldc 0; lddevid" (0 on parts
 *     that predate lddevid).
 * Then it executes "start", as the Programmer's Manual's type probe does,
 * which returns the transputer to its boot state.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "../../driver-tlk/tlk.h"

static const unsigned char prog[] = {
	0,				/* length, filled in */
	0xB4,				/* ajw 4 */
	0xD1,				/* stl 1 */
	0x24, 0xF2, 0x21, 0xFC,		/* mint; stlf: empty low-priority queue */
	0x24, 0xF2, 0x21, 0xF8,		/* mint; sthf: empty high-priority queue */
	0xF0,				/* rev: A = the boot input channel */
	0x60, 0x5C,			/* ldnlp -4: its output channel */
	0xD2,				/* stl 2 */
	0x24, 0xF2, 0xD3,		/* mint; stl 3: the marker */
	0x29, 0xFF,			/* fpldzerosn (opr 0x9F): FA = 0.0 */
	0x13,				/* ldlp 3 */
	0x28, 0xF8,			/* fpstnlsn (opr 0x88): store FA at A */
	0x72, 0x73, 0xFF,		/* ldl 2; ldl 3; outword */
	0x40, 0x40, 0x40,		/* ldc 0 x3 */
	0x21, 0x27, 0xFC,		/* lddevid (opr 0x17C) */
	0xD3, 0x72, 0x73, 0xFF,		/* stl 3; ldl 2; ldl 3; outword */
	0x21, 0x2F, 0xFF,		/* start (opr 0x1FF): back to boot state */
	0x20, 0x20			/* padding */
};

static long word(int fd, int *ok)
{
	unsigned char b[4];
	int n, i;

	for (n = 0; n < 4; n += i)
		if ((i = read(fd, b + n, 4 - n)) <= 0) { *ok = 0; return 0; }
	*ok = 1;
	return b[0] | b[1] << 8 | (long)b[2] << 16 | (long)b[3] << 24;
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/link1";
	int nofpu = 0, a;
	unsigned char p[sizeof prog];
	long fpu, id;
	int fd, ok;

	for (a = 1; a < argc; a++) {
		if (strcmp(argv[a], "-n") == 0) nofpu = 1;
		else dev = argv[a];
	}
	setvbuf(stdout, 0, _IONBF, 0);
	if ((fd = open(dev, O_RDWR)) < 0) { printf("%s: errno %d\n", dev, errno); return 1; }
	ioctl(fd, TLK_TIMEOUT, 500);
	ioctl(fd, TLK_RESET, 0);
	memcpy(p, prog, sizeof p);
	p[0] = sizeof p - 1;
	if (nofpu) {			/* pfix 0 is a no-op */
		p[18] = p[19] = 0x20;	/* fpldzerosn */
		p[21] = p[22] = 0x20;	/* fpstnlsn */
	}
	if (write(fd, p, sizeof p) != sizeof p) { printf("%s: boot failed (errno %d)\n", dev, errno); return 1; }
	fpu = word(fd, &ok);
	if (!ok) { printf("%s: no answer\n", dev); ioctl(fd, TLK_RESET, 0); return 1; }
	if (nofpu)
		printf("%s: FPU test skipped (marker %08lx)\n", dev, fpu);
	else
		printf("%s: FPU test word %08lx -> %s\n", dev, fpu,
		    fpu == 0 ? "FPU present (T8xx)" : fpu == (long)0x80000000UL ? "no FPU (T4xx)" : "unexpected");
	id = word(fd, &ok);
	if (ok)
		printf("%s: lddevid A = %08lx\n", dev, id);
	else
		printf("%s: no answer after lddevid (an older part without it)\n", dev);
	ioctl(fd, TLK_RESET, 0);
	return 0;
}
