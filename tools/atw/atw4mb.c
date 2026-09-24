/* ATW800/2 4 MB test (measured on a real card): with the
 * 4 MB window decoded, register 15 = 1 (both aliases) is the 2 MB layout,
 * mirrored; 3 via the lower alias switches to 4 MB, where the id block
 * is at the top of the 4 MB only. Leaves the card in 2 MB (register 15 =
 * 1) unless run with "keep". Touches: register 15, and one VRAM word at
 * 0x100000 (saved and restored), which is off-screen below 1024x1024. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#define BASE	0xFEA00000UL
#define SIZE	0x400000UL
static volatile unsigned char *m;
#define W(o)	(*(volatile unsigned short *)(m + (o)))
#define L(o)	(*(volatile unsigned long *)(m + (o)))
#define CPM	0x2063706DUL
int main(int argc, char **argv)
{
	int fd = open("/dev/mem", O_RDWR), four;
	unsigned short save_lo, save_hi;
	if (fd < 0) { perror("/dev/mem"); return 1; }
	m = (volatile unsigned char *)mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)BASE);
	if ((long)m == -1) { perror("mmap 4 MB at FEA00000 (jumpers open?)"); return 1; }
	printf("before:      id@2M-top %s  id@4M-top %s\n",
	       L(0x1FF218) == CPM ? "yes" : "no ", L(0x3FF218) == CPM ? "yes" : "no ");
	W(0x1FF81E) = 1;			/* 2 MB, via both aliases */
	W(0x3FF81E) = 1;
	save_lo = W(0x100000); save_hi = W(0x300000);
	W(0x100000) = 0x1234;
	printf("reg15=1:     VRAM +1M=%04x +3M=%04x (%s)\n", W(0x100000), W(0x300000),
	       W(0x300000) == 0x1234 ? "mirrored = 2 MB" : "NOT mirrored");
	W(0x100000) = save_lo;
	W(0x1FF81E) = 3;			/* 4 MB, via the lower alias */
	four = L(0x3FF218) == CPM && L(0x1FF218) != CPM;
	save_lo = W(0x100000); save_hi = W(0x300000);
	W(0x100000) = 0x1234; W(0x300000) = 0x5678;
	printf("reg15=3:     id@2M-top %s  id@4M-top %s  VRAM +1M=%04x +3M=%04x\n",
	       L(0x1FF218) == CPM ? "yes" : "no ", L(0x3FF218) == CPM ? "yes" : "no ",
	       W(0x100000), W(0x300000));
	four = four && W(0x100000) == 0x1234 && W(0x300000) == 0x5678;
	W(0x100000) = save_lo; W(0x300000) = save_hi;
	printf("=> %s\n", four ? "4 MB WORKS" : "no 4 MB");
	if (!(argc > 1 && strcmp(argv[1], "keep") == 0)) {
		W(0x3FF81E) = 1;		/* back to 2 MB: registers at 4M-top */
		W(0x1FF81E) = 1;
		printf("restored:    id@2M-top %s\n", L(0x1FF218) == CPM ? "yes" : "no");
	}
	return 0;
}
