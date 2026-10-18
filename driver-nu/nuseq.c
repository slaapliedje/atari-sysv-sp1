/*
 * nuseq - a sequence of single NetUSBee register accesses, one CPU load
 * each, through an mmap of /dev/mem (nuprobe's read(2) touches each byte
 * three times).  Prints what every read returns.
 *
 *   cc -O -o nuseq nuseq.c && ./nuseq OP...
 *
 *   rR       read register R (hex), print it
 *   wR,V     write V (hex) to register R: a load at 0xFB0000+(R<<9)+(V<<1)
 *   sN       spin N turns (about 0.7 us each) between the ops that follow
 *   xN       repeat the ops after it N times (one level)
 *   pN       sleep N ms (poll)
 *   bN       busy: spin N turns once, here
 *   RR, WR,V as rR and wR,V but by read(2) of /dev/mem (the kernel's
 *            copy, three touches: a reference that no cache can answer)
 *   -n       (first) no prefault: the first touch of each page happens
 *            in the sequence, so its fault shows
 *   -b BASE  (first) map the card at BASE (hex), e.g. fffa0000, the top
 *            mirror of the ST space that the kernel driver uses
 *
 * Avoid r10 and w10 (the data port) unless a remote DMA is set up, and
 * r1f / w1f (reset).
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <poll.h>

static volatile unsigned char *win;	/* 0xFA0000 - 0xFBFFFF */
static int memfd;
static unsigned long base = 0xfa0000;

static int
sysrd(unsigned long a)
{
	unsigned char b;

	if (lseek(memfd, (off_t)a, 0) == (off_t)-1 || read(memfd, (char *)&b, 1) != 1)
		return -1;
	return b;
}
volatile unsigned char nu_sink;
volatile int spin;

#define RD(r)		(win[(r) << 9])
#define WR(r, v)	(nu_sink = win[0x10000 + ((r) << 9) + ((v) << 1)])

int
main(int argc, char **argv)
{
	int fd, i, k, rep = 1, first = 1, gap = 0, s, r, v, n, pre = 1;
	unsigned char got[64];
	int gotr[64];
	caddr_t p;

	for (;;) {
		if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n') {
			pre = 0;
			argv++, argc--;
		} else if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'b') {
			base = strtoul(argv[2], (char **)0, 16);
			argv += 2, argc -= 2;
		} else
			break;
	}
	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		perror("/dev/mem");
		return 1;
	}
	p = mmap((caddr_t)0, 0x20000, PROT_READ, MAP_SHARED, fd, (off_t)base);
	if (p == (caddr_t)-1) {
		perror("mmap");
		return 1;
	}
	win = (volatile unsigned char *)p;
	memfd = fd;
	/* first touches fault (triple access): harmless ones, page 0 kept */
	if (pre) {
		nu_sink = RD(0);
		nu_sink = RD(8);
		v = RD(0) & 0xc0;	/* keep the page CR shows */
		nu_sink = win[0x10000 + ((0x21 | v) << 1)];
		nu_sink = win[0x10000 + (8 << 9)];	/* RSAR0 = 0 */
	}

	for (i = 1; i < argc; i++)
		if (argv[i][0] == 'x') {
			rep = (int)strtol(argv[i] + 1, (char **)0, 10);
			first = i + 1;
		}
	for (k = 0; k < rep; k++) {
		n = 0;
		for (i = first > 1 && argv[first - 1][0] == 'x' ? first : 1;
		    i < argc; i++) {
			char *a = argv[i], *e;

			switch (a[0]) {
			case 's':
				gap = (int)strtol(a + 1, (char **)0, 10);
				continue;
			case 'x':
				continue;
			case 'b':
				for (s = (int)strtol(a + 1, (char **)0, 10); s > 0; s--)
					spin++;
				continue;
			case 'p':
				(void)poll((struct pollfd *)0, 0,
				    (int)strtol(a + 1, (char **)0, 10));
				continue;
			case 'r':
				r = (int)strtol(a + 1, (char **)0, 16);
				if (n < 64) {
					gotr[n] = r;
					got[n++] = RD(r);
				} else
					nu_sink = RD(r);
				break;
			case 'R':
				r = (int)strtol(a + 1, (char **)0, 16);
				if (n < 64) {
					gotr[n] = 0x100 | r;
					got[n++] = sysrd(base + (r << 9));
				}
				break;
			case 'W':
				r = (int)strtol(a + 1, &e, 16);
				v = *e == ',' ? (int)strtol(e + 1, (char **)0, 16) : 0;
				(void)sysrd(base + 0x10000 + ((r & 0x1f) << 9) + ((v & 0xff) << 1));
				break;
			case 'w':
				r = (int)strtol(a + 1, &e, 16);
				v = *e == ',' ? (int)strtol(e + 1, (char **)0, 16) : 0;
				WR(r & 0x1f, v & 0xff);
				break;
			default:
				fprintf(stderr, "%s: rR, wR,V, RR, WR,V, sN, pN, bN or xN\n", a);
				return 1;
			}
			for (s = gap; s > 0; s--)
				spin++;
		}
		if (k < 4 || k == rep - 1) {
			printf("%d:", k);
			for (i = 0; i < n; i++)
				printf(" %c%x=%02x", gotr[i] & 0x100 ? 'R' : 'r',
				    gotr[i] & 0xff, got[i]);
			printf("\n");
		}
	}
	return 0;
}
