/*
 * nuprobe - read single bytes from /dev/mem (or another memory device),
 * one read(2) of one byte each, so nothing but the byte asked for is
 * touched. For the NetUSBee on the cartridge port, where a READ at
 * 0xFB0000 and up is a register WRITE: only the offsets named are read.
 *
 *   nuprobe [-d dev] hexoffset... | wREG,VAL...
 *
 * wREG,VAL (hex) writes chip register REG: a read of 0xFB0000 +
 * (REG << 9) + (VAL << 1).
 *
 * The RTL8019AS's registers read at 0xFA0000 + (reg << 9); after power-on
 * it is on page 0, where reg 0x0A and 0x0B are its ID, 0x50 0x70.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *dev = "/dev/mem";
	int fd, i = 1;

	if (argc > 2 && strcmp(argv[1], "-d") == 0) {
		dev = argv[2];
		i = 3;
	}
	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		perror(dev);
		return 1;
	}
	for (; i < argc; i++) {
		unsigned long a;
		unsigned char b;
		int n;

		if (argv[i][0] == 'w') {
			char *e;
			unsigned long r = strtoul(argv[i] + 1, &e, 16);
			unsigned long v = *e == ',' ? strtoul(e + 1, (char **)0, 16) : 0;

			if (*e != ',' || r > 0x1f || v > 0xff) {
				printf("%s: wREG,VAL\n", argv[i]);
				return 1;
			}
			a = 0xfb0000 + (r << 9) + (v << 1);
		} else
			a = strtoul(argv[i], (char **)0, 16);

		if (lseek(fd, (off_t)a, SEEK_SET) == (off_t)-1) {
			printf("%08lx lseek: errno %d\n", a, errno);
			continue;
		}
		n = read(fd, (char *)&b, 1);
		if (n == 1)
			printf("%08lx %02x\n", a, b);
		else
			printf("%08lx read %d: errno %d\n", a, n,
				n < 0 ? errno : 0);
		fflush(stdout);
	}
	return 0;
}
