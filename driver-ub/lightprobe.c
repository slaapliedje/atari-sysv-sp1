/*
 * lightprobe - is the Lightning VME's ISP1160 there?  From user space
 * through /dev/mem.  VME A16 at 0xFEFF8000: data register (16 bits) at
 * +0, address register at +4 (the register number in the high byte: the
 * card crosses the byte lanes), the CPLD control register at +9
 * (addresses from FreeMiNT's ucd/vttusb/isp116x.h).
 *
 *   cc -O -o lightprobe lightprobe.c && ./lightprobe
 */
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

__volatile__ unsigned short ub_sink;
__volatile__ int ub_spin;

static void
delay()
{
	int i;

	for (i = 0; i < 24; i++)
		ub_spin = i;
}

int
main(argc, argv)
int argc;
char **argv;
{
	__volatile__ unsigned char *win;
	__volatile__ unsigned short *data, *addr;
	int fd, pass;
	unsigned v;

	if ((fd = open("/dev/mem", O_RDWR)) < 0) {
		perror("/dev/mem");
		return 1;
	}
	win = (__volatile__ unsigned char *)mmap((caddr_t)0, 0x1000,
	    PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0xfeff8000);
	if (win == (__volatile__ unsigned char *)-1) {
		perror("mmap");
		return 1;
	}
	data = (__volatile__ unsigned short *)win;
	addr = (__volatile__ unsigned short *)(win + 4);
	ub_sink = win[9];			/* the page's first touch, harmless */
	printf("CPLD CR    %02x %02x\n", win[9], win[9]);
	for (pass = 0; pass < 3; pass++) {
		*addr = 0x2700;			/* HcChipID */
		delay();
		v = *data;
		printf("HcChipID   %04x %s\n", v, (v & 0xff00) == 0x6100 ? "(ISP1160)" : "");
		*addr = 0x0000;			/* HcRevision */
		delay();
		v = *data;
		printf("HcRevision %04x %s\n", v, v == 0x10 ? "(ok)" : "");
		*addr = 0x0100;			/* HcControl */
		delay();
		v = *data;
		printf("HcControl  %04x\n", v);
	}
	return 0;
}
