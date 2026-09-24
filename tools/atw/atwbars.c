/* ATW800/2 direct-colour test: 640x480 (VESA 60 Hz timings) at
 * 16 or 32 bpp, bars red/green/blue/white/black top to bottom; each bar
 * written as the byte order the emulator models (16bpp: little-endian
 * RGB565; 32bpp: bytes B,G,R,x). usage: atwbars 16|32  - leaves the mode
 * set; restart X (or reboot) to get the screen back. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
static volatile unsigned char *m;
#define VTG	0x3FF800UL
#define R(n)	(*(volatile unsigned short *)(m + VTG + 2 * (n)))
int main(int argc, char **argv)
{
	int bpp = argc > 1 ? atoi(argv[1]) : 16, fd, x, y, bar;
	static const unsigned char rgb[5][3] = {{255,0,0},{0,255,0},{0,0,255},{255,255,255},{0,0,0}};
	fd = open("/dev/mem", O_RDWR);
	m = (volatile unsigned char *)mmap(0, 0x400000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0xFEA00000UL);
	if ((long)m == -1) { perror("mmap"); return 1; }
	*(volatile unsigned short *)(m + 0x1FF81E) = 3;		/* 4 MB layout */
	R(0) = 0;
	R(9) = 14 - 1; R(10) = 3 - 1; R(11) = 8 - 1;		/* 27 MHz * 14 / 3 = 5 x 25.2 MHz */
	R(1) = 16; R(2) = 96; R(3) = 48; R(4) = 640;
	R(5) = 10; R(6) = 2; R(7) = 33; R(8) = 480;
	R(12) = 0; R(13) = 0;
	for (y = 0; y < 480; y++) {
		const unsigned char *c = rgb[y / 96];
		volatile unsigned char *p = m + (unsigned long)y * 640 * (bpp / 8);
		for (x = 0; x < 640; x++) {
			if (bpp == 16) {
				unsigned short w = ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3);
				*p++ = w & 0xFF; *p++ = w >> 8;
			} else {
				*p++ = c[2]; *p++ = c[1]; *p++ = c[0]; *p++ = 0;
			}
		}
	}
	R(0) = bpp == 16 ? 0x29 : 0x39;			/* VTG on, PLL latch, depth 10/11 */
	printf("640x480 %d bpp bars set\n", bpp);
	return 0;
}
