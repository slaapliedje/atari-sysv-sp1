/*
 * atwfb - bring up the ATW800/2 "Seurat" framebuffer from Atari System V.
 *
 * The card is NOT an ET4000: it is an FPGA with a video timing generator
 * (VTG), a PLL and a 256-entry RGB565 LUT, all memory mapped at the top of
 * its 2 MB window (ATW800/2 Programmer's Manual, "Programming Seurat").
 * On a TT with the ADDR jumper open the window is at 0xFEC00000.
 *
 * Mapping: /dev/atw if our driver is configured (offset 0 = card base),
 * else /dev/mem at the physical address (the stock mm driver maps any
 * address that does not bus-error).
 *
 *	cc -O -o atwfb atwfb.c
 *	atwfb		640x480x256 @60 Hz + test pattern
 *	atwfb off	stop the VTG (card output off)
 *	atwfb info	print the FPGA id string, touch nothing
 *
 * VESA 640x480 60 Hz timing; the PLL runs at five times the pixel clock
 * from a 27 MHz reference (Programmer's Manual).
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>

#define ATW_PHYS	0xFEC00000UL	/* TT, ADDR jumper open */
#define ATW_SIZE	0x00200000UL	/* 2 MB card */

#define OFF_LUT		0x1FF000UL	/* 256 x RGB565, big-endian words */
#define OFF_INFO	0x1FF200UL	/* 32-byte FPGA id string */
#define OFF_VTG		0x1FF800UL	/* word registers below */

#define R_CTRL		0x00
#define R_HFP		0x02
#define R_HSY		0x04
#define R_HBP		0x06
#define R_HDI		0x08
#define R_VFP		0x0A
#define R_VSY		0x0C
#define R_VBP		0x0E
#define R_VDI		0x10
#define R_PLLFB		0x12
#define R_PLLID		0x14
#define R_PLLOD		0x16		/* write LAST: PLL relocks on it */
#define R_VMLO		0x18
#define R_VMHI		0x1A
#define R_MEM		0x1E		/* 1 = 2 MB */

#define W	640
#define H	480

static volatile unsigned char *fb;

#define REGW(o)	(*(volatile unsigned short *)(fb + OFF_VTG + (o)))
#define LUTW(i)	(*(volatile unsigned short *)(fb + OFF_LUT + 2 * (i)))

static unsigned short
rgb565(r, g, b)
int r, g, b;
{
	return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static int
map_card()
{
	int fd;
	char *p;

	fd = open("/dev/atw", O_RDWR);
	if (fd >= 0) {
		p = (char *)mmap((caddr_t)0, ATW_SIZE, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, (off_t)0);
		if (p != (char *)-1) {
			printf("mapped via /dev/atw at %lx\n", (unsigned long)p);
			fb = (volatile unsigned char *)p;
			return 0;
		}
		printf("/dev/atw: mmap failed, errno %d\n", errno);
	}
	fd = open("/dev/mem", O_RDWR);
	if (fd < 0) {
		printf("/dev/mem: open failed, errno %d\n", errno);
		return -1;
	}
	p = (char *)mmap((caddr_t)0, ATW_SIZE, PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, (off_t)ATW_PHYS);
	if (p == (char *)-1) {
		printf("/dev/mem: mmap of %lx failed, errno %d\n", ATW_PHYS, errno);
		return -1;
	}
	printf("mapped via /dev/mem at %lx\n", (unsigned long)p);
	fb = (volatile unsigned char *)p;
	return 0;
}

static void
show_info()
{
	char id[33];
	int i;

	for (i = 0; i < 32; i++) {
		id[i] = (char)fb[OFF_INFO + i];
		if (id[i] != 0 && (id[i] < 32 || id[i] > 126))
			id[i] = '.';
	}
	id[32] = 0;
	printf("FPGA id: \"%s\"\n", id);
}

static void
set_mode()
{
	REGW(R_CTRL) = 0;		/* stop the VTG */
	REGW(R_MEM) = 1;		/* 2 MB */
	REGW(R_PLLFB) = 14 - 1;		/* 27 MHz * 14 / 3 = 5 x 25.2 MHz */
	REGW(R_PLLID) = 3 - 1;
	REGW(R_PLLOD) = 8 - 1;
	REGW(R_HFP) = 16;
	REGW(R_HSY) = 96;
	REGW(R_HBP) = 48;
	REGW(R_HDI) = W;
	REGW(R_VFP) = 10;
	REGW(R_VSY) = 2;
	REGW(R_VBP) = 33;
	REGW(R_VDI) = H;
	REGW(R_VMLO) = 0;
	REGW(R_VMHI) = 0;
	REGW(R_CTRL) = 0x19;		/* enable, 8 bpp through the LUT */
}

static void
set_palette()
{
	int i;

	/* 0..15: the 16 bar colours; 16..255: a grey ramp */
	static unsigned char bar[16][3] = {
		{0,0,0}, {255,255,255}, {255,0,0}, {0,255,0},
		{0,0,255}, {255,255,0}, {0,255,255}, {255,0,255},
		{128,128,128}, {192,192,192}, {128,0,0}, {0,128,0},
		{0,0,128}, {128,128,0}, {0,128,128}, {128,0,128}
	};
	for (i = 0; i < 16; i++)
		LUTW(i) = rgb565(bar[i][0], bar[i][1], bar[i][2]);
	for (i = 16; i < 256; i++)
		LUTW(i) = rgb565(i, i, i);
}

static void
draw_pattern()
{
	int x, y;
	volatile unsigned char *row;

	for (y = 0; y < H; y++) {
		row = fb + (long)y * W;
		for (x = 0; x < W; x++) {
			if (y < 320)
				row[x] = (unsigned char)(x / 40);	/* 16 bars */
			else
				row[x] = (unsigned char)(16 + (x * 240L) / W);	/* ramp */
		}
	}
	/* a white frame + one diagonal: a wrong stride or size shows at once */
	for (x = 0; x < W; x++) {
		fb[x] = 1;
		fb[(long)(H - 1) * W + x] = 1;
	}
	for (y = 0; y < H; y++) {
		fb[(long)y * W] = 1;
		fb[(long)y * W + W - 1] = 1;
		fb[(long)y * W + y] = 5;
	}
}

int
main(argc, argv)
int argc;
char **argv;
{
	if (map_card() < 0)
		return 1;
	show_info();
	if (argc > 1 && strcmp(argv[1], "info") == 0)
		return 0;
	if (argc > 1 && strcmp(argv[1], "off") == 0) {
		REGW(R_CTRL) = 0;
		printf("VTG stopped\n");
		return 0;
	}
	set_mode();
	set_palette();
	draw_pattern();
	printf("640x480x256 @60 Hz set; 16 colour bars over a grey ramp,\n");
	printf("white frame, yellow diagonal from the top-left corner.\n");
	return 0;
}
