/*
 * ubprobe - is the NetUSBee's ISP1160 USB host controller there?  From
 * user space through /dev/mem (user programs run from TT-RAM, so the
 * cartridge port sees clean cycles).  The ROM port is read-only, so the
 * chip is written by reading: a byte latched at 0xFA0000 + (b << 1), then
 * a read at 0xFBC000 (command: register number) or 0xFB8000 + (b << 1)
 * (data, with the latched byte as the high half); 16-bit data reads at
 * 0xFA8000.  Addresses from FreeMiNT's ucd/netusbee/isp116x.h.
 *
 *   cc -O -o ubprobe ubprobe.c && ./ubprobe
 *
 * HcChipID (0x27) must read 0x61xx; HcRevision (0x00) 0x10.
 */
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

__volatile__ unsigned short ub_sink;
__volatile__ unsigned char *win;

__volatile__ int ub_spin;

/* about a microsecond; the ROM port cycles themselves take ~0.5 us */
static void
delay()
{
	int i;

	for (i = 0; i < 24; i++)
		ub_spin = i;
}

/* command: the register number */
static void
setreg(r)
int r;
{
	ub_sink = win[(r & 0xff) << 1];		/* latch the byte */
	delay();
	ub_sink = win[0x1C000];			/* command write */
	delay();
}

static unsigned
rd16()
{
	unsigned v;

	v = *(__volatile__ unsigned short *)(win + 0x8000);
	delay();
	return v;		/* the card hands the 16 bits over as they are */
}

int
main(argc, argv)
int argc;
char **argv;
{
	int fd, i, pass;
	unsigned v;

	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		perror("/dev/mem");
		return 1;
	}
	win = (__volatile__ unsigned char *)mmap((caddr_t)0, 0x20000, PROT_READ,
	    MAP_SHARED, fd, (off_t)0xfa0000);
	if (win == (__volatile__ unsigned char *)-1) {
		perror("mmap");
		return 1;
	}
	/* the first touch of each page goes through the fault path and the
	 * intended cycle is lost: touch them all first, harmlessly */
	for (i = 0; i < 0x20000; i += 0x1000)
		ub_sink = win[i];
	for (pass = 0; pass < 3; pass++) {
		setreg(0x27);
		v = rd16();
		printf("HcChipID   %04x %s\n", v, (v & 0xff00) == 0x6100 ? "(ISP1160)" : "");
		setreg(0x00);
		v = rd16();
		printf("HcRevision %04x %s\n", v, v == 0x10 ? "(ok)" : "");
		setreg(0x01);
		v = rd16();
		printf("HcControl  %04x\n", v);
	}
	return 0;
}
