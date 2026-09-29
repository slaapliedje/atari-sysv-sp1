/* sndstat [seconds [rate [stereo]]] - play a 440 Hz tone through
 * /dev/audio and print the frame interrupt's counters (SND_STATS) once a
 * second: with the chain working, intrs grows by bytes-per-second / block
 * and late, skips and fallbacks stay 0. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "../../driver-snd/snd.h"

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 10;
	int rate = argc > 2 ? atoi(argv[2]) : 25033;
	int stereo = argc > 3 ? atoi(argv[3]) : 0;
	int fd = open("/dev/audio", O_WRONLY), hz, ch, i, t;
	static char buf[4096];
	struct snd_stats st, prev;
	unsigned long phase = 0, inc;
	time_t last;

	if (fd < 0) { perror("/dev/audio"); return 1; }
	hz = ioctl(fd, SND_SETRATE, rate);
	ioctl(fd, SND_SETSTEREO, stereo);
	ch = stereo ? 2 : 1;
	inc = (unsigned long)((440.0 * 65536.0 * 65536.0) / hz);
	printf("%d Hz %s, %d s\n", hz, stereo ? "stereo" : "mono", secs);
	if (ioctl(fd, SND_STATS, &prev) < 0) { printf("SND_STATS: errno %d (old driver?)\n", errno); return 1; }
	last = time(0);
	for (t = 0; t < secs; ) {
		for (i = 0; i < (int)sizeof buf; i += ch) {
			phase += inc;
			buf[i] = (char)((phase & 0x80000000UL) ? 40 : -40);
			if (ch == 2)
				buf[i + 1] = buf[i];
		}
		if (write(fd, buf, sizeof buf) < 0) { perror("write"); return 1; }
		if (time(0) != last) {
			last = time(0);
			t++;
			ioctl(fd, SND_STATS, &st);
			printf("%2d s: intrs %lu (+%lu)  late %lu  skips %lu  fallbacks %lu  chained %ld  underruns %d\n",
			       t, st.intrs, st.intrs - prev.intrs, st.late, st.skips, st.fallbacks,
			       st.chained, ioctl(fd, SND_GETUNDERRUNS, 0));
			prev = st;
		}
	}
	close(fd);
	return 0;
}
