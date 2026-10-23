/* khz: the kernel's hertz, and lbolt's rate measured over 5 s.  The two
 * variables are found in the running kernel's symbol table (/stand/unix,
 * nlist(3E)) and read from /dev/kmem. */
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <nlist.h>
static long rd(int fd, long a)
{
	long v = -1;
	if (lseek(fd, a, 0) == -1 || read(fd, (char *)&v, 4) != 4)
		perror("kmem");
	return v;
}
int main(void)
{
	static struct nlist nl[3];
	int fd = open("/dev/kmem", 0);
	long h, b0, b1;
	time_t t0;
	nl[0].n_name = "hertz"; nl[1].n_name = "lbolt";
	if (nlist("/stand/unix", nl) == -1 || nl[0].n_value == 0 || nl[1].n_value == 0) {
		fprintf(stderr, "khz: hertz or lbolt not in /stand/unix's symbol table\n");
		return 1;
	}
	if (fd < 0) { perror("/dev/kmem"); return 1; }
	h = rd(fd, (long)nl[0].n_value);
	t0 = time(0); while (time(0) == t0) ;
	b0 = rd(fd, (long)nl[1].n_value); t0 = time(0);
	while (time(0) < t0 + 5) ;
	b1 = rd(fd, (long)nl[1].n_value);
	printf("hertz %ld (at 0x%lx), lbolt %ld -> %ld: %ld per second\n", h, (long)nl[0].n_value, b0, b1, (b1 - b0) / 5);
	return 0;
}
