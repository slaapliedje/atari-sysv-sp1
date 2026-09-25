/* mandel.c - a floating-point benchmark for the T800 and the TT's 68882:
 * a 320x200 Mandelbrot set, 256 iterations at most, in double. Prints
 * the iteration total (a checksum: both machines must agree) and the time
 * from its own clock. */
#include <stdio.h>
#include <time.h>

#define W 320
#define H 200
#define MAXIT 256

int main(void)
{
	long total = 0;
	int x, y, i;
	clock_t t0, t1;
	double secs;

	t0 = clock();
	for (y = 0; y < H; y++) {
		double ci = -1.2 + 2.4 * y / H;
		for (x = 0; x < W; x++) {
			double cr = -2.1 + 3.0 * x / W;
			double zr = 0.0, zi = 0.0, zr2 = 0.0, zi2 = 0.0;
			for (i = 0; i < MAXIT && zr2 + zi2 < 4.0; i++) {
				zi = 2.0 * zr * zi + ci;
				zr = zr2 - zi2 + cr;
				zr2 = zr * zr;
				zi2 = zi * zi;
			}
			total += i;
		}
	}
	t1 = clock();
	secs = (double)(t1 - t0) / CLOCKS_PER_SEC;
	printf("mandel: %ld iterations in %.2f s = %.0f K iterations/s\n",
	    total, secs, total / secs / 1000.0);
	return 0;
}
