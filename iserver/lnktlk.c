/*
 * lnktlk.c - iserver's link module for Atari System V: the ATW800/2's
 * transputer links through the tlk driver (../driver-tlk).
 *
 * The link name (iserver -sl NAME, or $TRANSPUTER) is a device:
 * "/dev/link0" (the C011, TRAM slot 1; the default) or "/dev/link1" (the
 * FPGA's T425); "0" and "1" are short for them.
 *
 * The interface is INMOS's link module convention: link ids are > 0,
 * errors negative (ER_LINK_BAD, ER_LINK_CANT, ER_LINK_SOFT); timeouts are
 * in tenths of a second for the whole call, 0 = none. ReadLink and
 * WriteLink return the number of bytes moved, which may be short when
 * the timeout runs out.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/ioctl.h>
#include "../driver-tlk/tlk.h"

#define LINK_OK		1
#define ER_LINK_BAD	(-1)
#define ER_LINK_CANT	(-2)
#define ER_LINK_SOFT	(-3)

/* SP.GETENV's answers when the environment has no IBOARDSIZE / ISEARCH
 * (see asvhost.h). Both TRAMs on the ATW800/2 here have 2 MB (measured
 * with tools/tram/tmem); the FPGA's T425 has 1 MB, so set IBOARDSIZE
 * to #100000 when booting that. */
char *ibsize = "#200000";
char *isearch = "/usr/local/lib/transputer/libs/";
char *iccarg = "";

static int fd = -1;
static long hz;

void __main(void) { }		/* the mint GCC's constructor hook: none */

static long now(void)
{
	struct tms t;
	return (long)times(&t);
}

int OpenLink(char *name)
{
	char dev[64];

	if (name == 0 || *name == 0)
		name = "/dev/link0";
	if (strcmp(name, "0") == 0 || strcmp(name, "1") == 0) {
		sprintf(dev, "/dev/link%s", name);
		name = dev;
	}
	if (fd >= 0)
		return ER_LINK_CANT;
	hz = sysconf(_SC_CLK_TCK);
	if (hz <= 0)
		hz = 100;
	if ((fd = open(name, O_RDWR)) < 0) {
		fprintf(stderr, "iserver: %s: %s\n", name, strerror(errno));
		return ER_LINK_BAD;
	}
	return LINK_OK;
}

int CloseLink(int id)
{
	if (id != LINK_OK || fd < 0)
		return ER_LINK_BAD;
	close(fd);
	fd = -1;
	return LINK_OK;
}

/* move Count bytes one way or the other within Timeout tenths (0 = none);
 * the driver's own wait limit is kept to the time that is left */
static int move(int id, unsigned char *buf, unsigned int count, int timeout, int out)
{
	long end = timeout > 0 ? now() + (timeout * hz + 9) / 10 : 0;
	unsigned int done = 0;
	int n;

	if (id != LINK_OK || fd < 0)
		return ER_LINK_BAD;
	while (done < count) {
		long left = 0;

		if (end) {
			left = (end - now()) * 1000 / hz;
			if (left <= 0)
				break;
		}
		ioctl(fd, TLK_TIMEOUT, (int)left);
		n = out ? write(fd, buf + done, count - done) : read(fd, buf + done, count - done);
		if (n > 0) {
			done += n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && errno == EIO)	/* the driver's wait ran out */
			continue;
		return done ? (int)done : ER_LINK_SOFT;
	}
	return (int)done;
}

int ReadLink(int id, unsigned char *buf, unsigned int count, int timeout)
{
	return move(id, buf, count, timeout, 0);
}

int WriteLink(int id, unsigned char *buf, unsigned int count, int timeout)
{
	return move(id, buf, count, timeout, 1);
}

int ResetLink(int id)
{
	if (id != LINK_OK || fd < 0)
		return ER_LINK_BAD;
	return ioctl(fd, TLK_RESET, 0) < 0 ? ER_LINK_SOFT : LINK_OK;
}

int AnalyseLink(int id)
{
	if (id != LINK_OK || fd < 0)
		return ER_LINK_BAD;
	return ioctl(fd, TLK_ANALYSE, 0) < 0 ? ER_LINK_SOFT : LINK_OK;
}

static int status(int id)
{
	if (id != LINK_OK || fd < 0)
		return ER_LINK_BAD;
	return ioctl(fd, TLK_STATUS, 0);
}

/* 1 if the transputer's error flag is set, 0 if not */
int TestError(int id)
{
	int s = status(id);
	return s < 0 ? ER_LINK_BAD : (s & TLK_ST_ERROR) != 0;
}

/* 1 if a ReadLink of one byte would not wait */
int TestRead(int id)
{
	int s = status(id);
	return s < 0 ? ER_LINK_BAD : (s & TLK_ST_IN) != 0;
}

/* 1 if a WriteLink of one byte would not wait */
int TestWrite(int id)
{
	int s = status(id);
	return s < 0 ? ER_LINK_BAD : (s & TLK_ST_OUT) != 0;
}
