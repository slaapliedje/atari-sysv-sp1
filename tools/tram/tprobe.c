/*
 * tprobe - find the transputers on an ATW800/2's links (TT, ASV or AMIX).
 *
 *   tprobe [c011|fpga] [-n]      default: both links; -n = status only, no reset
 *
 * The card has two link interfaces, each laid out like an INMOS C011 with
 * the byte registers in the low byte of each word (Programmer's Manual,
 * "Programming the Transputer"):
 *
 *   +0x01 input data   +0x03 output data   +0x05 input status   +0x07 output status
 *   +0x11 reset (write) / error (read)     +0x17 analyse
 *
 * (The manual's register table can be read as +0x21/+0x23; on a real
 * card those bytes only mirror the input data. +0x11/+0x17 are what its
 * GfA demo uses, and what resets the transputer.)
 *
 *   C011 link  0xFEFFFAC0  the physical transputer in TRAM slot 1 ("+T" cards)
 *   FPGA link  0xFEDFFAC0  the T425 inside the FPGA
 *
 * After a reset a transputer that boots from link takes one control byte:
 * 0 = POKE (address, word), 1 = PEEK (address; answers a word), and 2..255
 * = boot that many bytes of code and run them. tprobe POKEs a word, PEEKs it
 * back, then boots the manual's 23-byte type probe, which answers with 1, 2
 * or 4 bytes: a C004, a 16-bit or a 32-bit transputer.
 *
 * Every wait has a time limit, so a missing or silent transputer cannot hang
 * the machine. /dev/mem refuses (ENXIO) a page that would bus-error, so a
 * card without the C011 is reported, not crashed on.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/times.h>
#include <errno.h>

#define R_IN	0x01
#define R_OUT	0x03
#define R_ISTAT	0x05
#define R_OSTAT	0x07
#define R_RESET	0x11
#define R_ANAL	0x17

static volatile unsigned char *lk;
static long hz;

static long ticks(void) { struct tms t; return (long)times(&t); }

/* spin until (reg & 1) or the time runs out; 1 = ready */
static int ready(int reg, long ms)
{
	long end = ticks() + (ms * hz + 999) / 1000 + 1;
	while (!(lk[reg] & 1))
		if (ticks() > end)
			return 0;
	return 1;
}

static int put(int b)
{
	if (!ready(R_OSTAT, 500))
		return -1;
	lk[R_OUT] = b;
	return 0;
}

static int get(long ms)
{
	if (!ready(R_ISTAT, ms))
		return -1;
	return lk[R_IN];
}

static void pause_ms(long ms)
{
	long end = ticks() + (ms * hz + 999) / 1000 + 1;
	while (ticks() <= end)
		;
}

/* a byte left in the input register survives a reset of the transputer */
static int drain(void)
{
	int n = 0;
	while ((lk[R_ISTAT] & 1) && n < 64) {
		(void)lk[R_IN];
		n++;
	}
	return n;
}

static void treset(void)
{
	lk[R_RESET] = 1;
	lk[R_ANAL] = 0;
	pause_ms(20);
	lk[R_RESET] = 0;
	pause_ms(20);
	drain();
}

static int putword(unsigned long w)		/* little-endian, as the link carries it */
{
	int i;
	for (i = 0; i < 4; i++)
		if (put((w >> (8 * i)) & 0xFF))
			return -1;
	return 0;
}

static const unsigned char typeprobe[] = {	/* Programmer's Manual, T_DEMO.GFA */
	23, 177, 209, 36, 242, 33, 252, 36, 242, 33, 248,
	240, 96, 92, 42, 42, 42, 74, 255, 33, 47, 255, 2, 0
};

static void status(const char *name)
{
	printf("%s: in-status %d, out-status %d, error %d\n", name,
	    lk[R_ISTAT] & 1, lk[R_OSTAT] & 1, lk[R_RESET] & 1);
}

static void probe(const char *name, unsigned long pa, int statonly)
{
	int fd, i, n, b[8];
	unsigned long pg = pa & ~0xFFFUL, w;
	volatile unsigned char *p;

	fd = open("/dev/mem", O_RDWR);
	if (fd < 0) { printf("%s: /dev/mem: errno %d\n", name, errno); return; }
	p = (volatile unsigned char *)mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)pg);
	close(fd);
	if (p == (volatile unsigned char *)-1) {
		printf("%s @%08lx: not there (errno %d%s)\n", name, pa, errno,
		    errno == ENXIO ? ": the page bus-errors" : "");
		return;
	}
	lk = p + (pa - pg);
	status(name);
	if (statonly)
		goto out;

	/* POKE a word, PEEK it back */
	treset();
	status(name);
	if (put(0) || putword(0x80000100UL) || putword(0x12345678UL)) {
		printf("%s: POKE: the link does not take bytes (no transputer, or not listening)\n", name);
		goto out;
	}
	if (put(1) || putword(0x80000100UL)) {
		printf("%s: PEEK: the link does not take bytes\n", name);
		goto out;
	}
	for (i = 0, w = 0; i < 4; i++) {
		int c = get(500);
		if (c < 0) { printf("%s: PEEK: no answer after %d bytes\n", name, i); goto out; }
		w |= (unsigned long)c << (8 * i);
	}
	printf("%s: POKE/PEEK 0x80000100: wrote 12345678, read %08lx %s\n", name, w,
	    w == 0x12345678UL ? "- ok" : "- WRONG");

	/* boot the type probe */
	treset();
	for (i = 0; i < (int)sizeof typeprobe; i++)
		if (put(typeprobe[i])) {
			printf("%s: boot: stalled at byte %d\n", name, i);
			goto out;
		}
	for (n = 0; n < 8; n++) {
		int c = get(300);
		if (c < 0)
			break;
		b[n] = c;
	}
	printf("%s: type probe answered %d byte%s:", name, n, n == 1 ? "" : "s");
	for (i = 0; i < n; i++)
		printf(" %02x", b[i]);
	printf(" -> %s\n", n == 1 ? "C004" : n == 2 ? "16-bit transputer" :
	    n == 4 ? "32-bit transputer" : "nothing recognised");
	treset();
out:
	(void)p;		/* the mapping goes with the process */
}

int main(int argc, char **argv)
{
	int i, statonly = 0, c011 = 1, fpga = 1;

	hz = sysconf(_SC_CLK_TCK);
	if (hz <= 0)
		hz = 100;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-n") == 0) statonly = 1;
		else if (strcmp(argv[i], "c011") == 0) fpga = 0;
		else if (strcmp(argv[i], "fpga") == 0) c011 = 0;
		else { fprintf(stderr, "usage: tprobe [c011|fpga] [-n]\n"); return 2; }
	}
	setvbuf(stdout, 0, _IONBF, 0);
	if (c011) probe("C011 link", 0xFEFFFAC0UL, statonly);
	if (fpga) probe("FPGA link", 0xFEDFFAC0UL, statonly);
	return 0;
}
