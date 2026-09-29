/* khz: the kernel's hertz, and lbolt's rate measured over 5 s */
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
static long rd(int fd, long a)
{
	long v = -1;
	if (lseek(fd, a, 0) == -1 || read(fd, (char *)&v, 4) != 4)
		perror("kmem");
	return v;
}
int main(void)
{
	int fd = open("/dev/kmem", 0);
	long h, b0, b1;
	time_t t0, t1;
	if (fd < 0) { perror("/dev/kmem"); return 1; }
	h = rd(fd, 0xd4694L);
	t0 = time(0); while (time(0) == t0) ;
	b0 = rd(fd, 0xf8de4L); t0 = time(0);
	while (time(0) < t0 + 5) ;
	b1 = rd(fd, 0xf8de4L);
	printf("hertz %ld, lbolt %ld -> %ld: %ld per second\n", h, b0, b1, (b1 - b0) / 5);
	return 0;
}
