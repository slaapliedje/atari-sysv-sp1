/*
 * sndtest - check /dev/audio (driver-snd) by ear and by clock.
 *
 *   sndtest [rate]      default 25033
 *
 * Plays 2 s of 440 Hz (mono), 2 s of 440 Hz left / 660 Hz right (stereo),
 * then a 2 s sweep from 200 to 2000 Hz (mono), and prints how long the
 * whole took by the TT's clock: 6 s if the rate is right.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/ioctl.h>
#include "../../driver-snd/snd.h"

static signed char buf[8192];
static signed char sine[256];		/* one cycle, amplitude 90 */
static int fd, rate;
static unsigned long ph[2];		/* phase, 8.24 fixed point */

/* samples from a table with a phase accumulator, integer per sample, as a
 * mixer would: floating point per sample is too slow on the TT at 25 kHz */
static void tone(double f0, double f1, double f2, int stereo, double secs)
{
	long n = (long)(secs * rate), i = 0;
	int k, ch = stereo ? 2 : 1;
	double step = 16777216.0 * 256 / rate;	/* phase per Hz per sample */
	long d = (long)(f0 * step), d1 = (long)(f1 * step);
	long dd = (d1 - d) / n;			/* sweep: step change per sample */
	unsigned long d2 = (unsigned long)(f2 * step);

	while (i < n) {
		for (k = 0; k + ch <= (int)sizeof buf && i < n; i++) {
			ph[0] += d;
			d += dd;
			buf[k++] = sine[ph[0] >> 24];
			if (stereo) {
				ph[1] += d2;
				buf[k++] = sine[ph[1] >> 24];
			}
		}
		if (write(fd, buf, k) != k) { printf("write: errno %d\n", errno); exit(1); }
	}
}

int main(int argc, char **argv)
{
	struct tms t;
	long t0, t1, hz = sysconf(_SC_CLK_TCK);

	int i;

	for (i = 0; i < 256; i++)
		sine[i] = (signed char)(90 * sin(2 * M_PI * i / 256));
	setvbuf(stdout, 0, _IONBF, 0);
	if ((fd = open("/dev/audio", O_WRONLY)) < 0) { printf("/dev/audio: errno %d\n", errno); return 1; }
	rate = ioctl(fd, SND_SETRATE, argc > 1 ? atoi(argv[1]) : 25033);
	printf("rate %d Hz, ring %d bytes\n", rate, ioctl(fd, SND_GETRING, 0));
	t0 = times(&t);
	ioctl(fd, SND_SETSTEREO, 0);
	printf("440 Hz, mono\n");
	tone(440, 440, 0, 0, 2.0);
	ioctl(fd, SND_DRAIN, 0);
	ioctl(fd, SND_SETSTEREO, 1);
	printf("440 Hz left, 660 Hz right\n");
	tone(440, 440, 660, 1, 2.0);
	ioctl(fd, SND_DRAIN, 0);
	ioctl(fd, SND_SETSTEREO, 0);
	printf("sweep 200 -> 2000 Hz, mono\n");
	tone(200, 2000, 0, 0, 2.0);
	ioctl(fd, SND_DRAIN, 0);
	t1 = times(&t);
	printf("played 6 s of sound in %ld ms, %d underruns\n", (t1 - t0) * 1000 / hz, ioctl(fd, SND_GETUNDERRUNS, 0));
	close(fd);
	return 0;
}
