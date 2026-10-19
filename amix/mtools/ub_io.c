/*
 * ub_io.c - mtools on Atari System V: a drive on the USB stack's storage
 * unit N (sp1 driver-ub), written "ub:N" as the drive's file in
 * mtools.conf.  The blocks go through /dev/ub0's UBIOC_RD and UBIOC_WR
 * by block number, not through /dev/rdsk/usbN: a 32-bit lseek stops
 * that one at 2 GB, and a 16 GB stick's FAT32 lives beyond it.  Needs
 * the daemon (ubtool daemon) to have enumerated the stick.
 *
 * GPL-3, as mtools (see COPYING).
 */
#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "stream.h"
#include "ub_io.h"
#include "ubioc.h"

#define UB_DEV	"/dev/ub0"

typedef struct Ub_t {
	struct Stream_t head;

	int fd;
	int unit;
	int wr;				/* opened for writing */
	uint32_t blocks;
	unsigned bs;			/* block size: 512 */
	mt_off_t lastwhere;
	char *bounce;			/* one block, for partial ones */
} Ub_t;

/* n blocks at lba from or to buf; 0 or -1 */
static int ub_blocks(Ub_t *This, char *buf, unsigned long lba,
		     unsigned long n, int write)
{
	struct ub_rw rw;

	rw.unit = This->unit;
	rw.lba = lba;
	rw.n = n;
	rw.buf = buf;
	return ioctl(This->fd, write ? UBIOC_WR : UBIOC_RD, &rw) < 0 ? -1 : 0;
}

static ssize_t ub_xfer(Ub_t *This, char *buf, mt_off_t where, size_t len,
		       int write)
{
	size_t done = 0;
	unsigned bs = This->bs;

	if (write && !This->wr) {
		errno = EROFS;
		return -1;
	}
	if (where < 0) {
		errno = EINVAL;
		return -1;
	}
	while (done < len) {
		mt_off_t pos = where + (mt_off_t)done;
		unsigned long lba = (unsigned long)(pos / bs);
		unsigned off = (unsigned)(pos % bs);
		unsigned long n;
		size_t chunk;

		if (lba >= This->blocks)
			break;				/* the end */
		if (off == 0 && len - done >= bs) {
			n = (len - done) / bs;
			if (n > UB_RWMAX / bs)
				n = UB_RWMAX / bs;
			if (lba + n > This->blocks)
				n = This->blocks - lba;
			if (ub_blocks(This, buf + done, lba, n, write) < 0)
				return done ? (ssize_t)done : -1;
			done += n * bs;
			continue;
		}
		/* part of a block: through the bounce buffer */
		chunk = bs - off;
		if (chunk > len - done)
			chunk = len - done;
		if (ub_blocks(This, This->bounce, lba, 1, 0) < 0)
			return done ? (ssize_t)done : -1;
		if (write) {
			memcpy(This->bounce + off, buf + done, chunk);
			if (ub_blocks(This, This->bounce, lba, 1, 1) < 0)
				return done ? (ssize_t)done : -1;
		} else
			memcpy(buf + done, This->bounce + off, chunk);
		done += chunk;
	}
	return (ssize_t)done;
}

static ssize_t ub_read(Stream_t *Stream, char *buf, size_t len)
{
	DeclareThis(Ub_t);
	ssize_t r = ub_xfer(This, buf, This->lastwhere, len, 0);

	if (r > 0)
		This->lastwhere += r;
	return r;
}

static ssize_t ub_write(Stream_t *Stream, char *buf, size_t len)
{
	DeclareThis(Ub_t);
	ssize_t r = ub_xfer(This, buf, This->lastwhere, len, 1);

	if (r > 0)
		This->lastwhere += r;
	return r;
}

static ssize_t ub_pread(Stream_t *Stream, char *buf, mt_off_t where,
			size_t len)
{
	DeclareThis(Ub_t);
	return ub_xfer(This, buf, where, len, 0);
}

static ssize_t ub_pwrite(Stream_t *Stream, char *buf, mt_off_t where,
			 size_t len)
{
	DeclareThis(Ub_t);
	return ub_xfer(This, buf, where, len, 1);
}

static int ub_flush(Stream_t *Stream UNUSEDP)
{
	return 0;
}

static int ub_free(Stream_t *Stream)
{
	DeclareThis(Ub_t);

	free(This->bounce);
	return close(This->fd);
}

/* the whole stick, in 512-byte sectors; the file system's own geometry
 * comes from its boot sector */
static int ub_geom(Stream_t *Stream, struct device *dev,
		   struct device *orig_dev UNUSEDP)
{
	DeclareThis(Ub_t);
	mt_off_t sectors = (mt_off_t)This->blocks * This->bs / 512;

	dev->tot_sectors = sectors > UINT32_MAX ? UINT32_MAX : (uint32_t)sectors;
	if (dev->sector_size == 0)
		dev->sector_size = This->bs;
	return 0;
}

static int ub_data(Stream_t *Stream, time_t *date, mt_off_t *size,
		   int *type, uint32_t *address)
{
	DeclareThis(Ub_t);

	if (date)
		*date = 0;
	if (size)
		*size = (mt_off_t)This->blocks * This->bs;
	if (type)
		*type = 0;
	if (address)
		*address = 0;
	return 0;
}

static int ub_discard(Stream_t *Stream UNUSEDP)
{
	return 0;
}

static Class_t UbClass = {
	ub_read,
	ub_write,
	ub_pread,
	ub_pwrite,
	ub_flush,
	ub_free,
	ub_geom,
	ub_data,
	0, /* pre_allocate */
	0, /* dos-convert */
	ub_discard
};

Stream_t *UbOpen(struct device *dev UNUSEDP, const char *unit, int mode,
		 char *errmsg, mt_off_t *maxSize)
{
	Ub_t *This;
	struct ub_storinfo st[UB_NSTOR];
	char *end;
	long u;

	u = strtol(unit, &end, 10);
	if (*unit == '\0' || *end != '\0' || u < 0 || u >= UB_NSTOR) {
		sprintf(errmsg, "ub:%.20s: the unit is 0 to %d", unit, UB_NSTOR - 1);
		return NULL;
	}
	This = New(Ub_t);
	if (!This) {
		sprintf(errmsg, "out of memory");
		return NULL;
	}
	memset(This, 0, sizeof *This);
	init_head(&This->head, &UbClass, NULL);
	This->unit = (int)u;
	This->wr = (mode & O_ACCMODE) != O_RDONLY;
	This->fd = open(UB_DEV, This->wr ? O_RDWR : O_RDONLY);
	if (This->fd < 0) {
		sprintf(errmsg, "%s: %.60s", UB_DEV, strerror(errno));
		free(This);
		return NULL;
	}
	if (ioctl(This->fd, UBIOC_STOR, st) < 0) {
		sprintf(errmsg, "%s: UBIOC_STOR: %.60s (is the USB driver in?)",
			UB_DEV, strerror(errno));
		close(This->fd);
		free(This);
		return NULL;
	}
	if (!st[u].present || !st[u].ready) {
		sprintf(errmsg, "USB storage unit %ld: %s (is ubtool daemon running, "
			"the stick in?)", u, st[u].present ? "not ready" : "none");
		close(This->fd);
		free(This);
		return NULL;
	}
	if (st[u].blocksize != 512) {
		sprintf(errmsg, "USB storage unit %ld: %lu-byte blocks, only 512 "
			"handled", u, st[u].blocksize);
		close(This->fd);
		free(This);
		return NULL;
	}
	This->blocks = (uint32_t)st[u].blocks;
	This->bs = (unsigned)st[u].blocksize;
	This->bounce = malloc(This->bs);
	if (!This->bounce) {
		sprintf(errmsg, "out of memory");
		close(This->fd);
		free(This);
		return NULL;
	}
	if (maxSize)
		*maxSize = (mt_off_t)This->blocks * This->bs;
	return &This->head;
}
