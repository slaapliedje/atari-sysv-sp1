/*
 * nucr - the NetUSBee's registers from user space through /dev/mem, with
 * nothing else touched: to cross-check what the kernel reads or wrote.
 *
 *   cc -O -o nucr nucr.c && ./nucr [rRR | wRRVV | p]...   (hex)
 *
 * rRR reads register RR twice (the first touch of a mapped page is lost to
 * the fault path) and prints the second value; wRRVV writes VV to RR; p
 * prints page 0 (registers 0-15) as a line.  A write's first touch of a
 * page of the write window hits that page's base register with 0 (CR=0,
 * RSAR0=0, data port, reset) before the intended write.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

__volatile__ unsigned char nu_sink;

int
main(int argc, char **argv)
{
	int fd, i, r, v;
	__volatile__ unsigned char *win;
	char *a;

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
	if (argc < 2) {
		nu_sink = win[0];
		printf("R 00 %02x\n", win[0]);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		a = argv[i];
		if (a[0] == 'p') {
			printf("page");
			for (r = 0; r < 16; r++) {
				nu_sink = win[r << 9];
				printf(" %02x", win[r << 9]);
			}
			printf("\n");
		} else if (a[0] == 'w' && strlen(a) >= 5) {
			v = (int)strtol(a + 1, (char **)0, 16);
			r = (v >> 8) & 0x1f, v &= 0xff;
			nu_sink = win[0x10000 + (r << 9) + (v << 1)];
			printf("W %02x %02x\n", r, v);
		} else {
			r = (int)strtol(a + 1, (char **)0, 16) & 0x1f;
			nu_sink = win[r << 9];
			printf("R %02x %02x\n", r, win[r << 9]);
		}
	}
	return 0;
}
