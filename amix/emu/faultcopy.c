/*
 * faultcopy - make the kernel's copyout() page-fault in the middle of a
 * copy, over and over, and check every byte arrived.
 *
 *   faultcopy FILE [ROUNDS]
 *
 * FILE is filled with a pattern that has no zero bytes. Each round reads
 * it into fresh memory from sbrk() (never touched, so every 2 KB page
 * faults inside copyout) at an odd offset, then checks it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define LEN 60000L

static unsigned char want(long i)
{
	return (unsigned char)(1 + (i * 7 + (i >> 8)) % 251);
}

int main(int argc, char **argv)
{
	long rounds = argc > 2 ? atol(argv[2]) : 100, r, i, n, bad = 0, zero = 0;
	unsigned char *p, blk[4096];
	int fd;

	fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) { perror(argv[1]); return 2; }
	for (i = 0; i < LEN; i += sizeof blk) {
		long k, m = LEN - i < (long)sizeof blk ? LEN - i : (long)sizeof blk;
		for (k = 0; k < m; k++) blk[k] = want(i + k);
		write(fd, blk, m);
	}
	for (r = 0; r < rounds; r++) {
		p = (unsigned char *)sbrk(LEN + 4096);
		if (p == (unsigned char *)-1) { printf("sbrk failed at round %ld\n", r); break; }
		p += 1 + (r * 37) % 2047;	/* an odd, varying offset into a page */
		lseek(fd, 0L, 0);
		n = read(fd, p, LEN);
		if (n != LEN) { printf("round %ld: read %ld\n", r, n); continue; }
		for (i = 0; i < LEN; i++) {
			if (p[i] != want(i)) {
				if (bad < 40)
					printf("round %ld: byte %ld at %lx is %02x, want %02x\n",
						r, i, (unsigned long)(p + i), p[i], want(i));
				bad++;
				if (p[i] == 0) zero++;
			}
		}
	}
	printf("%ld rounds, %ld bad bytes (%ld zero)\n", r, bad, zero);
	return bad != 0;
}
