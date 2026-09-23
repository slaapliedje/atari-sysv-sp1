/*
 * setboot - read or set the TT030's boot preference in NVRAM from Atari
 * System V.  Atari's own setboot (a WISh tool) is not on the disk image
 * that circulates; this uses the rtc driver's RTCNVMACCESS ioctl, the
 * kernel-side twin of TOS's NVMaccess().
 *
 *     setboot            print the current preference
 *     setboot tos        boot TOS       (NVRAM word 0x0080)
 *     setboot unix       boot System V  (NVRAM word 0x0040)
 *     setboot none       no preference: first bootable partition
 *
 * The preference is the word at NVRAM offset 0..1 and the block-zero
 * bootstrap (/etc/T0boot) compares its LOW byte against the partition
 * flag bytes (0x80 St-boot, 0x40 Unix-boot).  The driver maintains the
 * checksum TOS verifies on boot.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/rtc.h>

static int fd;

static int nvm(op, start, count, buf)
	int op, start, count;
	unsigned char *buf;
{
	struct nvmaccess a;

	a.nva_op = op;
	a.nva_start = start;
	a.nva_count = count;
	a.nva_addr = (caddr_t)buf;
	a.nva_ret = 0;
	if (ioctl(fd, RTCNVMACCESS, &a) < 0) {
		perror("RTCNVMACCESS");
		exit(1);
	}
	return a.nva_ret;
}

static const char *name(v)
	int v;
{
	switch (v) {
	case 0x80: return "TOS";
	case 0x40: return "Atari System V";
	case 0x00: return "no preference (first bootable partition)";
	}
	return "unknown";
}

int main(argc, argv)
	int argc;
	char **argv;
{
	unsigned char pref[2];
	int want = -1, r;

	if (argc > 1) {
		if (!strcmp(argv[1], "tos")) want = 0x80;
		else if (!strcmp(argv[1], "unix")) want = 0x40;
		else if (!strcmp(argv[1], "none")) want = 0x00;
		else {
			fprintf(stderr, "usage: setboot [tos|unix|none]\n");
			return 2;
		}
	}
	fd = open("/dev/rtc", O_RDWR);
	if (fd < 0) { perror("/dev/rtc"); return 1; }

	r = nvm(NVMREAD, 0, 2, pref);
	printf("boot preference: 0x%02x%02x = %s%s\n", pref[0], pref[1], name(pref[1]),
	       r ? "  (NVRAM checksum bad: TOS ignores it)" : "");
	if (want < 0)
		return 0;
	pref[0] = 0;
	pref[1] = want;
	r = nvm(NVMWRITE, 0, 2, pref);
	nvm(NVMREAD, 0, 2, pref);
	printf("set to:          0x%02x%02x = %s (rc %d)\n", pref[0], pref[1], name(pref[1]), r);
	return 0;
}
