/*
 * kvar -- print or set longs in the kernel or a loaded module by name.
 *
 *	kvar ncr_xcmd ncr_xbytes	print them
 *	kvar ncr_pdmamode=4		set it (root)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

extern int getksym();

main(argc, argv)
int argc;
char **argv;
{
	unsigned long a, info;
	char *eq;
	long v;
	int fd, i, rc = 0;

	if (argc < 2) {
		fprintf(stderr, "usage: kvar name[=value] ...\n");
		return 2;
	}
	for (i = 1; i < argc; i++) {
		if ((eq = strchr(argv[i], '=')) != 0)
			*eq++ = 0;
		a = 0;			/* nonzero asks for the name at that address */
		if (getksym(argv[i], &a, &info) < 0) {
			fprintf(stderr, "kvar: %s: no such symbol\n", argv[i]);
			rc = 1;
			continue;
		}
		if ((fd = open("/dev/kmem", eq ? O_RDWR : O_RDONLY)) < 0) {
			perror("kvar: /dev/kmem");
			return 1;
		}
		if (eq) {
			v = strtol(eq, (char **)0, 0);
			if (lseek(fd, (off_t)a, 0) == -1 || write(fd, (char *)&v, 4) != 4) {
				perror(argv[i]);
				rc = 1;
			}
		}
		if (lseek(fd, (off_t)a, 0) == -1 || read(fd, (char *)&v, 4) != 4) {
			perror(argv[i]);
			rc = 1;
		} else
			printf("%s %ld\n", argv[i], v);
		close(fd);
	}
	return rc;
}
