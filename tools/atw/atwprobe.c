/* read-only ATW800/2 probe; each read in a child so a bus error kills
 * only the child */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
static void probe(const char *what, unsigned long pa, int nwords)
{
	int st;
	if (fork() == 0) {
		unsigned long pg = pa & ~0xFFFUL;
		int fd = open("/dev/mem", O_RDWR), i;
		volatile unsigned short *p;
		p = (volatile unsigned short *)mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)pg);
		if ((long)p == -1) { printf("%-10s %08lx: mmap failed errno %d fd %d\n", what, pa, errno, fd); fflush(stdout); _exit(1); }
		p += (pa - pg) / 2;
		printf("%-10s %08lx:", what, pa); fflush(stdout);
		for (i = 0; i < nwords; i++) printf(" %04x", p[i]);
		printf("\n"); fflush(stdout);
		_exit(0);
	}
	wait(&st);
	if (st & 0x7F) printf("  -> killed by signal %d\n", st & 0x7F);
}
int main(void)
{
	unsigned long a;
	for (a = 0xFEA00000UL; a < 0xFEE00000UL; a += 0x100000UL)
		probe("vram", a, 4);
	probe("id@2M-top", 0xFEDFF200UL, 16);	/* 2 MB card at FEC00000, or 4 MB at FEA00000 */
	probe("id@FEBFF", 0xFEBFF200UL, 16);	/* a 2 MB card at FEA00000 */
	probe("vtg", 0xFEDFF800UL, 16);
	probe("cpld", 0xFEFFFAD8UL, 1);
	return 0;
}
