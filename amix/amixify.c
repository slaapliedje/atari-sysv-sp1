/*
 * amixify - amixify.py for the TT itself: make AMIX (Amiga UNIX) ELF files
 * use AMIX's own C library, installed in /usr/amx instead of /usr/lib.
 *
 *   amixify [-q] FILE...
 *
 * Programs: the interpreter and any NEEDED /usr/lib/libc.so.1 become
 * /usr/amx/libc.so.1. AMIX's libc.so.1 and ld.so.1 also get their default
 * library directory, /usr/lib, changed to /usr/amx (see amixify.py).
 *
 * Files are patched in place with same-length strings, so the inode, owner
 * and mode stay as they were; a file that is already done is left alone.
 * Only big-endian 68k ELF files are touched. -q skips files that are not
 * ELF, and prints nothing for files that need no change, so it can be run
 * over everything a package installed.
 *
 * Exit status: 0 when every file was handled, 1 if one could not be read
 * or written, 2 on usage errors.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

static const char OLD[] = "/usr/lib/libc.so.1";	/* compared with its NUL */
static const char NEW[] = "/usr/amx/libc.so.1";
static const char DIR_OLD[] = "\0/usr/lib";		/* NUL on both sides */
static const char DIR_NEW[] = "\0/usr/amx";

/* replace every occurrence of old (n bytes) with new; returns the count */
static long patch(unsigned char *d, long len, const char *old, const char *new, long n)
{
	long i, count = 0;

	for (i = 0; i + n <= len; i++)
		if (d[i] == (unsigned char)old[0] && memcmp(d + i, old, n) == 0) {
			memcpy(d + i, new, n);
			count++;
			i += n - 1;
		}
	return count;
}

static const char *base(const char *p)
{
	const char *s = strrchr(p, '/');

	return s ? s + 1 : p;
}

static int amixify(const char *path, int quiet)
{
	struct stat st;
	unsigned char *d;
	long n, lib, dir = 0;
	int fd;

	if ((fd = open(path, O_RDWR)) < 0 || fstat(fd, &st) < 0) {
		perror(path);
		if (fd >= 0)
			close(fd);
		return 1;
	}
	if (!S_ISREG(st.st_mode) || st.st_size < 52) {
		close(fd);
		if (!quiet)
			fprintf(stderr, "%s: not an ELF file\n", path);
		return quiet ? 0 : 1;
	}
	if ((d = (unsigned char *)malloc(st.st_size)) == NULL) {
		fprintf(stderr, "%s: out of memory\n", path);
		close(fd);
		return 1;
	}
	if (read(fd, d, st.st_size) != st.st_size) {
		perror(path);
		free(d);
		close(fd);
		return 1;
	}
	/* ELF, 32-bit, big-endian, e_machine 4 (68k) */
	if (memcmp(d, "\177ELF", 4) != 0 || d[4] != 1 || d[5] != 2 || d[18] != 0 || d[19] != 4) {
		free(d);
		close(fd);
		if (!quiet)
			fprintf(stderr, "%s: not a 68k ELF file\n", path);
		return quiet ? 0 : 1;
	}
	n = st.st_size;
	lib = patch(d, n, OLD, NEW, sizeof OLD);
	if (strcmp(base(path), "libc.so.1") == 0 || strcmp(base(path), "ld.so.1") == 0)
		dir = patch(d, n, DIR_OLD, DIR_NEW, sizeof DIR_OLD);
	if (lib + dir > 0) {
		if (lseek(fd, 0L, SEEK_SET) != 0 || write(fd, d, n) != n) {
			perror(path);
			free(d);
			close(fd);
			return 1;
		}
	}
	free(d);
	if (close(fd) < 0) {
		perror(path);
		return 1;
	}
	if (lib + dir > 0 || !quiet)
		printf("%s: %ld libc reference(s), %ld library dir(s) rewritten\n", path, lib, dir);
	return 0;
}

int main(int argc, char **argv)
{
	int i = 1, quiet = 0, rc = 0;

	if (i < argc && strcmp(argv[i], "-q") == 0) {
		quiet = 1;
		i++;
	}
	if (i >= argc) {
		fprintf(stderr, "usage: amixify [-q] FILE...\n");
		return 2;
	}
	for (; i < argc; i++)
		rc |= amixify(argv[i], quiet);
	return rc;
}
