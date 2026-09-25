/* sndrate - time 2 s of silence at each of the four DMA sound rates: the
 * hardware clock check for driver-snd (each should take ~2000 ms). */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/ioctl.h>
#include "../../driver-snd/snd.h"
static char z[8192];
int main(void)
{
	static int rates[4] = { 6258, 12517, 25033, 50066 };
	int fd = open("/dev/audio", O_WRONLY), r, k;
	struct tms t; long t0, t1, hz = sysconf(_SC_CLK_TCK), n, left;
	setvbuf(stdout, 0, _IONBF, 0);
	for (k = 0; k < 4; k++) {
		r = ioctl(fd, SND_SETRATE, rates[k]);
		ioctl(fd, SND_SETSTEREO, 0);
		ioctl(fd, SND_DRAIN, 0);
		n = 2L * r;
		t0 = times(&t);
		for (left = n; left > 0; left -= sizeof z)
			write(fd, z, left < (long)sizeof z ? left : (long)sizeof z);
		ioctl(fd, SND_DRAIN, 0);
		t1 = times(&t);
		printf("%5d Hz: 2 s of mono in %ld ms, underruns %d\n", r, (t1 - t0) * 1000 / hz, ioctl(fd, SND_GETUNDERRUNS, 0));
	}
	return 0;
}
