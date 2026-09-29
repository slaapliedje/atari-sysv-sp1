/* cachet: cycles per loop for a register-only loop, reads from a small
 * (cache-sized) array and reads from a big one, to tell whether the 030's
 * caches are on and how fast this process's memory is */
#include <stdio.h>
#include <stdlib.h>
#include <sys/times.h>
#include <unistd.h>
#define MHZ 32.0
static double cpu(void)
{
	struct tms t;
	times(&t);
	return (double)(t.tms_utime + t.tms_stime) / sysconf(_SC_CLK_TCK);
}
static void report(const char *what, double s, long n)
{
	printf("%-40s %6.1f cycles per iteration\n", what, s * MHZ * 1e6 / n);
}
int main(void)
{
	long n = 4000000, i;
	double t;
	volatile long sink;
	static long small[32];
	long *big = (long *)malloc(1L << 20), sum;

	t = cpu();
	{
		register long c asm("d0") = n - 1;
		register long a asm("d1") = 0;
		asm volatile("1:	add.l %%d1,%%d1\n	subq.l &1,%%d0\n	bne.b 1b" : : "d"(c), "d"(a) : "d0", "d1", "cc");
	}
	report("add + subq + bne (registers only)", cpu() - t, n);

	for (i = 0; i < 32; i++) small[i] = i;
	t = cpu(); sum = 0;
	for (i = 0; i < n; i++) sum += small[i & 31];
	sink = sum;
	report("C loop reading a 128-byte array", cpu() - t, n);

	for (i = 0; i < (1L << 18); i++) big[i] = i;
	t = cpu(); sum = 0;
	for (i = 0; i < n; i++) sum += big[(i * 67) & ((1L << 18) - 1)];
	sink = sum;
	report("C loop reading a 1 MB array, scattered", cpu() - t, n);
	printf("big array at %lx, small at %lx, code at %lx\n", (long)big, (long)small, (long)main);
	return 0;
}
