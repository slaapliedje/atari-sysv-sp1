/*
 * ub.c - USB for Atari System V: FreeMiNT's stack (usb.c, hub.c, the
 * ISP1160 host driver) under one driver, major 46:
 *
 *   /dev/ub0            minor 0, character: the control device.  A user
 *                       process drives the stack through it (ubtool:
 *                       UBIOC_START, then UBIOC_POLL every half second),
 *                       so the stack's waits are sleeps and its
 *                       enumeration runs in process context; ubtool lists
 *                       what is there and reads blocks.
 *   /dev/dsk/usbN       minor 16+N, block: mass storage unit N whole
 *   /dev/rdsk/usbN      minor 16+N, character: the same, raw (physiock)
 *
 * The kernel calls this driver the OLD way, as it does its own disk
 * drivers: a driver with a block side goes through cunix's gateway
 * (gen_copen, gen_read, gen_ioctl, gen_strategy...), which passes the
 * device number by value in the SVR3 form (major << 8 | minor), sets up
 * u.u_base/u_count/u_offset for read and write instead of passing a uio,
 * passes no credentials and no rvalp, and takes the error from u.u_error.
 * (A character-only driver is wired directly, the SVR4 way; this one was,
 * before it had /dev/dsk/usbN, and a version written the SVR4 way took the
 * machine down on every open of a disk minor: the device number read as
 * a pointer.)  So: entry points take dev by value, errors go to u.u_error,
 * raw I/O is physck/physio over the u-area, the ioctl's result to
 * u.u_rval1.
 *
 * The stack only ever runs from the ioctl path: a process in UBIOC_WORK
 * (ubtool daemon) takes the queued disk requests one at a time, does the
 * transfer through the class driver (ubstor.c), polls the hubs and waits
 * for more; the strategy routine only queues.  Running the stack from
 * the open or the read path instead (through spec_open, physiock) took
 * the TT down twice on 2026-10-18: the kernel stack there is too shallow
 * for the stack's buffers, is the likely reason.  One process uses the
 * stack at a time (ub_lock).  The Lightning VME's chip on the VME bus
 * first; the NetUSBee's through the cartridge port is next.
 *
 * Built on the PC (../kbuild.sh driver-ub/ub.c): one compilation unit,
 * the stack's files included below.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/cred.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/sysmacros.h>
#include <sys/iosystm.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/buf.h>

/*
 * The u-area fields the old conventions use.  sys/user.h does not
 * compile with this compiler's headers (sigset_t and friends are hidden
 * from an ANSI compiler), so the fields are reached by their offsets in
 * this kernel's struct user, read off the kernel's own gen_ioctl,
 * physck and suser code: u_error the byte at 0x4d1, u_rval1 the int at
 * 0x4e4, u_procp the pointer at 0x4dc (u_cred is its p_cred, at 16).
 */
extern char u[];
#define UB_UERROR	(*(char *)(u + 0x4d1))
#define UB_URVAL1	(*(int *)(u + 0x4e4))
#define UB_UPROCP	(*(char **)(u + 0x4dc))
#define UB_UCRED	(*(struct cred **)(UB_UPROCP + 16))
/* no sys/ddi.h: it turns getminor into a function the kernel lacks */

#include "ub.h"
#include "usb.h"
#include "hub.h"
#include "usb_api.h"
#include "ubstor.h"
#include "ubioc.h"

extern int drv_priv();
extern int ioprobe();

extern void biodone();
extern int physio(), physck();
extern int hertz;

int ub_state = UB_NOCHIP;
static int ub_open_count;

/* minors: 0 the control device, UB_DISKBASE + unit the storage units.
 * The minor is the low byte whether the number comes in the old form
 * (the gateway's) or the new (a buffer's b_edev). */
#define UB_DISKBASE	16
#define UB_MINOR(dev)	((int)((dev) & 0xff))
#define UB_UNIT(dev)	(UB_MINOR(dev) - UB_DISKBASE)
#define UB_CHUNK	(64 * 1024)	/* bytes per class driver call */

/* the stack serves one process at a time */
static int ub_busy, ub_wanted;

static void
ub_lock()
{
	while (ub_busy) {
		ub_wanted = 1;
		sleep((caddr_t)&ub_busy, PRIBIO);
	}
	ub_busy = 1;
}

static void
ub_unlock()
{
	ub_busy = 0;
	if (ub_wanted) {
		ub_wanted = 0;
		wakeup((caddr_t)&ub_busy);
	}
}

/* ---- the kernel side of ub.h ---------------------------------------- */

void
ub_kprintf(fmt, a, b, c, d, e, f, g, h)
char *fmt;
long a, b, c, d, e, f, g, h;
{
	cmn_err(CE_CONT, fmt, a, b, c, d, e, f, g, h);
}

void
ub_memset(p, c, n)
char *p;
int c;
unsigned n;
{
	while (n-- > 0)
		*p++ = c;
}

char *
strcat(d, s)
char *d, *s;
{
	char *r = d;

	while (*d)
		d++;
	while ((*d++ = *s++) != 0)
		;
	return r;
}

/* the little of sprintf the stack's dumps use: %s %c %d %ld %u %lu %x %lx %p */
int
sprintf(buf, fmt, a1, a2, a3, a4, a5, a6, a7, a8)
char *buf, *fmt;
long a1, a2, a3, a4, a5, a6, a7, a8;
{
	long args[8], v;
	char *o = buf, *s, tmp[24];
	int ai = 0, lng, i, neg;
	unsigned long u;

	args[0] = a1; args[1] = a2; args[2] = a3; args[3] = a4;
	args[4] = a5; args[5] = a6; args[6] = a7; args[7] = a8;
	while (*fmt) {
		if (*fmt != '%') {
			*o++ = *fmt++;
			continue;
		}
		fmt++;
		while (*fmt >= '0' && *fmt <= '9')
			fmt++;
		lng = 0;
		if (*fmt == 'l') {
			lng = 1;
			fmt++;
		}
		v = ai < 8 ? args[ai++] : 0;
		switch (*fmt++) {
		case 's':
			for (s = (char *)v; s && *s; s++)
				*o++ = *s;
			break;
		case 'c':
			*o++ = (char)v;
			break;
		case 'd':
			if (!lng)
				v = (int)v;
			neg = v < 0;
			u = neg ? -v : v;
			goto num10;
		case 'u':
			u = lng ? (unsigned long)v : (unsigned)v;
			neg = 0;
		num10:
			i = 0;
			do {
				tmp[i++] = '0' + u % 10;
			} while ((u /= 10) != 0);
			if (neg)
				*o++ = '-';
			while (i > 0)
				*o++ = tmp[--i];
			break;
		case 'x':
		case 'p':
			u = lng || fmt[-1] == 'p' ? (unsigned long)v : (unsigned)v;
			i = 0;
			do {
				tmp[i++] = "0123456789abcdef"[u & 15];
			} while ((u >>= 4) != 0);
			while (i > 0)
				*o++ = tmp[--i];
			break;
		case '%':
			*o++ = '%';
			ai--;
			break;
		default:
			*o++ = '?';
		}
	}
	*o = 0;
	return o - buf;
}

/* the stack, one compilation unit */
#include "ub_io.c"
#include "usb.c"
#include "hub.c"
#include "ucd.c"
#include "udd.c"
#include "isp116x.c"
#include "ubstor.c"

/* ---- the driver ------------------------------------------------------ */

int
ubinit()
{
	int v;

	/* the Lightning VME: its address register answers (an empty VME
	 * slot bus-errors) and the chip behind it says 0x61xx */
	if (ioprobe(IOP_READ | IOP_SHORT, (caddr_t)0xFEFF8004, &v) == 0 && ub_io_init(UB_LIGHTNING) == 0) {
		ub_state = UB_CHIP;
		cmn_err(CE_CONT, "ub0: ISP1160 USB host controller on the Lightning VME\n");
	} else
		cmn_err(CE_CONT, "ub0: no USB host controller\n");
	return 0;
}

static int ub_start();
static void ub_poll_hubs();

/* a storage unit that is there and ready, or 0 */
static struct ub_stor *
ub_unit(dev)
dev_t dev;
{
	int unit = UB_UNIT(dev);
	struct ub_stor *s;

	if (unit < 0 || unit >= UB_STOR_MAX)
		return (struct ub_stor *)0;
	s = &ub_stor[unit];
	if (s->dev == (struct usb_device *)0 || !s->ready || s->blocksize == 0)
		return (struct ub_stor *)0;
	return s;
}

/* ARGSUSED */
int
ubopen(dev, flag, otyp)
dev_t dev;
int flag, otyp;
{
	if (ub_state == UB_NOCHIP) {
		UB_UERROR = ENXIO;
		return 0;
	}
	if (UB_MINOR(dev) == 0) {
		if (otyp != OTYP_CHR)
			UB_UERROR = ENXIO;
		else
			ub_open_count++;
		return 0;
	}
	if (otyp != OTYP_CHR && otyp != OTYP_BLK) {
		UB_UERROR = ENXIO;
		return 0;
	}
	/* a storage unit the daemon has found; nothing runs here */
	if (ub_unit(dev) == (struct ub_stor *)0)
		UB_UERROR = ENXIO;
	return 0;
}

/* ARGSUSED */
int
ubclose(dev, flag, otyp)
dev_t dev;
int flag, otyp;
{
	if (UB_MINOR(dev) == 0 && ub_open_count > 0)
		ub_open_count--;
	return 0;
}

static int
ub_start()
{
	if (ub_state == UB_RUNNING)
		return 0;
	usb_init();
	ub_stor_init();
	if (ub_hcd_init() < 0)
		return EIO;
	ub_state = UB_RUNNING;
	return 0;
}

static void
ub_poll_hubs()
{
	int i;

	for (i = 0; i < USB_MAX_HUB; i++) {
		struct usb_hub_device *hub = usb_get_hub_index(i);

		if (hub)
			usb_hub_events(hub);
	}
}

static void
ub_strcpy_n(d, s, n)
char *d, *s;
int n;
{
	while (--n > 0 && *s)
		*d++ = *s++;
	*d = 0;
}

static int
ub_devs(arg)
caddr_t arg;
{
	static struct ub_devinfo info[UB_NDEV];
	int i;

	ub_memset((char *)info, 0, sizeof info);
	for (i = 0; i < USB_MAX_DEVICE && i < UB_NDEV; i++) {
		struct usb_device *d = usb_get_dev_index(i);

		if (d == (struct usb_device *)0)
			continue;
		info[i].present = 1;
		info[i].addr = d->devnum;
		info[i].hub = d->maxchild != 0;
		info[i].vid = d->descriptor.idVendor;
		info[i].pid = d->descriptor.idProduct;
		info[i].class = d->descriptor.bDeviceClass;
		ub_strcpy_n(info[i].mf, d->mf, sizeof info[i].mf);
		ub_strcpy_n(info[i].prod, d->prod, sizeof info[i].prod);
		ub_strcpy_n(info[i].serial, d->serial, sizeof info[i].serial);
	}
	return copyout((caddr_t)info, arg, sizeof info) ? EFAULT : 0;
}

static int
ub_storlist(arg)
caddr_t arg;
{
	static struct ub_storinfo info[UB_NSTOR];
	int i;

	ub_memset((char *)info, 0, sizeof info);
	for (i = 0; i < UB_STOR_MAX && i < UB_NSTOR; i++) {
		struct ub_stor *s = &ub_stor[i];

		if (s->dev == (struct usb_device *)0)
			continue;
		info[i].present = 1;
		info[i].ready = s->ready;
		info[i].removable = s->removable;
		info[i].devaddr = s->dev->devnum;
		info[i].blocks = s->blocks;
		info[i].blocksize = s->blocksize;
		ub_strcpy_n(info[i].vendor, s->vendor, sizeof info[i].vendor);
		ub_strcpy_n(info[i].product, s->product, sizeof info[i].product);
		ub_strcpy_n(info[i].rev, s->rev, sizeof info[i].rev);
	}
	return copyout((caddr_t)info, arg, sizeof info) ? EFAULT : 0;
}

static int
ub_rw(arg, write)
caddr_t arg;
int write;
{
	struct ub_rw rw;
	struct ub_stor *s;
	char *buf;
	unsigned long bytes;
	int r = 0;

	if (copyin(arg, (caddr_t)&rw, sizeof rw))
		return EFAULT;
	if (rw.unit < 0 || rw.unit >= UB_STOR_MAX)
		return EINVAL;
	s = &ub_stor[rw.unit];
	if (s->dev == (struct usb_device *)0 || !s->ready)
		return ENXIO;
	bytes = rw.n * s->blocksize;
	if (rw.n == 0 || bytes > UB_RWMAX || rw.lba + rw.n > s->blocks)
		return EINVAL;
	buf = kmem_alloc(bytes, KM_SLEEP);
	if (write && copyin((caddr_t)rw.buf, buf, bytes))
		r = EFAULT;
	else if ((write ? ub_stor_write(s, rw.lba, rw.n, (unsigned char *)buf)
	    : ub_stor_read(s, rw.lba, rw.n, (unsigned char *)buf)) < 0)
		r = EIO;
	else if (!write && copyout(buf, (caddr_t)rw.buf, bytes))
		r = EFAULT;
	kmem_free(buf, bytes);
	return r;
}

static int ub_work();

/* the error of an old-style entry point goes to u.u_error */
static int
ub_err(r)
int r;
{
	if (r)
		UB_UERROR = r;
	return 0;
}

/* ARGSUSED */
int
ubioctl(dev, cmd, arg, mode)
dev_t dev;
int cmd, arg, mode;
{
	int r;

	switch (cmd) {
	case UBIOC_STATUS:
		UB_URVAL1 = ub_state;
		return 0;
	case UBIOC_START:
		ub_lock();
		if ((r = ub_start()) == 0)
			ub_poll_hubs();
		ub_unlock();
		return ub_err(r);
	case UBIOC_POLL:
		if (ub_state != UB_RUNNING)
			return ub_err(ENXIO);
		ub_lock();
		ub_poll_hubs();
		ub_unlock();
		return 0;
	case UBIOC_WORK:
		return ub_err(ub_work());
	case UBIOC_DEVS:
		return ub_err(ub_devs((caddr_t)arg));
	case UBIOC_STOR:
		return ub_err(ub_storlist((caddr_t)arg));
	case UBIOC_RD:
		ub_lock();
		r = ub_rw((caddr_t)arg, 0);
		ub_unlock();
		return ub_err(r);
	case UBIOC_WR:
		if (drv_priv(UB_UCRED))
			return ub_err(EPERM);
		ub_lock();
		r = ub_rw((caddr_t)arg, 1);
		ub_unlock();
		return ub_err(r);
	}
	return ub_err(EINVAL);
}

/* ---- the storage units as disks ------------------------------------- */

/*
 * A request is done there and then, in the caller's process context,
 * through the class driver in chunks; the stack sleeps while the chip
 * works, which the caller may.  b_blkno counts 512-byte blocks whatever
 * the unit's block size; a request past the end is cut short, one
 * entirely past it transfers nothing.
 *
 * Where the data goes depends on the buffer.  A cache buffer's address
 * is the kernel's own.  A raw request's (B_PHYS, from physio) is the
 * calling process's: user space is its own address space on this
 * kernel (copyin reaches it with moves), so the transfer goes through
 * a kernel buffer and copyin/copyout, which work because physio calls
 * the strategy routine from that process, with the pages locked.
 * bp_mapin is no use for it: this kernel's maps page lists only
 * (B_PAGEIO) and panics on anything else, which is how ub 0.5 died.  A
 * page list (B_PAGEIO: paging to the stick) is mapped with bp_mapin.
 *
 * UB_TRACE prints each step to the console; the kernel replays its
 * message buffer into console.log at the next boot, which is how a
 * silent reset is placed.
 */
#define UB_BOUNCE	(16 * 1024)	/* a raw request's kernel buffer */
static char ub_bounce[UB_BOUNCE];
extern int copyin(), copyout(), bp_mapin(), bp_mapout();

/* #define UB_TRACE 1 */	/* each strategy/raw call to the console */
#ifdef UB_TRACE
#define TR(x)	cmn_err x
#else
#define TR(x)
#endif

static void
ub_fail(bp, err)
struct buf *bp;
int err;
{
	bp->b_error = err;
	bp->b_flags |= B_ERROR;
	bp->b_resid = bp->b_bcount;
	biodone(bp);
}

/* one piece of a request from or to where the buffer's data lives */
static int
ub_piece(s, bp, lba, chunk, addr, write)
struct ub_stor *s;
struct buf *bp;
unsigned long lba, chunk;
unsigned char *addr;
int write;
{
	unsigned long len = chunk * s->blocksize;

	if ((bp->b_flags & B_PHYS) == 0)
		return write ? ub_stor_write(s, lba, chunk, addr)
		    : ub_stor_read(s, lba, chunk, addr);
	if (write) {
		if (copyin((caddr_t)addr, ub_bounce, len))
			return -1;
		return ub_stor_write(s, lba, chunk, (unsigned char *)ub_bounce);
	}
	if (ub_stor_read(s, lba, chunk, (unsigned char *)ub_bounce) < 0)
		return -1;
	return copyout(ub_bounce, (caddr_t)addr, len) ? -1 : 0;
}

void
ubstrategy(bp)
struct buf *bp;
{
	struct ub_stor *s;
	unsigned long bs, lba, n, done, chunk, max;
	unsigned char *addr;
	int write = (bp->b_flags & B_READ) == 0;
	int mapped = 0;

	TR((CE_CONT, "ub: strat bp %x dev %x flags %x blk %d cnt %d addr %x\n",
	    bp, bp->b_dev, bp->b_flags, bp->b_blkno, bp->b_bcount, bp->b_un.b_addr));
	bp->b_resid = bp->b_bcount;
	if ((s = ub_unit(bp->b_dev)) == (struct ub_stor *)0) {
		ub_fail(bp, ENXIO);
		return;
	}
	bs = s->blocksize;
	if (bs < DEV_BSIZE || bs % DEV_BSIZE != 0 || bp->b_bcount % bs != 0 ||
	    (bp->b_blkno * DEV_BSIZE) % bs != 0) {
		ub_fail(bp, EINVAL);
		return;
	}
	if ((bp->b_flags & B_PHYS) && bp->b_proc != (struct proc *)UB_UPROCP) {
		ub_fail(bp, EIO);	/* another process's pages: not reachable */
		return;
	}
	lba = bp->b_blkno / (bs / DEV_BSIZE);
	n = bp->b_bcount / bs;
	if (lba >= s->blocks) {
		biodone(bp);		/* at the end: nothing transferred */
		return;
	}
	if (lba + n > s->blocks)
		n = s->blocks - lba;
	if (bp->b_flags & B_PAGEIO) {
		bp_mapin(bp);
		mapped = 1;
	}
	addr = (unsigned char *)bp->b_un.b_addr;
	max = (bp->b_flags & B_PHYS) ? UB_BOUNCE / bs : UB_CHUNK / bs;
	ub_lock();
	for (done = 0; done < n; done += chunk) {
		chunk = n - done;
		if (chunk > max)
			chunk = max;
		if (ub_piece(s, bp, lba + done, chunk, addr, write) < 0)
			break;
		addr += chunk * bs;
	}
	ub_unlock();
	if (mapped)
		bp_mapout(bp);
	TR((CE_CONT, "ub: strat done %d of %d\n", done, n));
	bp->b_resid = bp->b_bcount - done * bs;
	if (done < n) {
		bp->b_error = EIO;
		bp->b_flags |= B_ERROR;
	}
	biodone(bp);
	TR((CE_CONT, "ub: biodone\n"));
}

/*
 * UBIOC_WORK, the daemon's loop body: the stack started if need be, the
 * hubs polled, then a wait of half a second (interruptible, so the
 * daemon can be killed).
 */
static int
ub_work()
{
	int r = 0;

	ub_lock();
	if (ub_state != UB_RUNNING)
		r = ub_start();
	if (r == 0)
		ub_poll_hubs();
	ub_unlock();
	if (r)
		return r;
	delay(hertz / 2);
	return 0;
}

/*
 * The unit's size in 512-byte blocks, as the kernel may be told it:
 * physck and the block-special read multiply it by 512 in 32-bit
 * arithmetic, so more than 2 GB would come out negative and refuse
 * everything.  The raw and block devices reach the first 2 GB of a
 * bigger stick; the ioctls (UBIOC_RD/WR) reach all of it.
 */
#define UB_MAXBLOCKS	0x3fffffUL	/* 2 GB less a block */

static daddr_t
ub_nblocks(dev)
dev_t dev;
{
	struct ub_stor *s = ub_unit(dev);
	unsigned long n;

	if (s == (struct ub_stor *)0)
		return 0;
	n = s->blocks * (s->blocksize / DEV_BSIZE);
	return n > UB_MAXBLOCKS ? UB_MAXBLOCKS : n;
}

/* the raw device: the request is in u.u_base/u_count/u_offset (the
 * gateway put it there), physck keeps it inside the unit */
static void
ub_rawio(dev, rw)
dev_t dev;
int rw;
{
	if (ub_unit(dev) == (struct ub_stor *)0) {
		UB_UERROR = ENXIO;
		return;
	}
	TR((CE_CONT, "ub: raw %s dev %x base %x count %x off %x\n", rw == B_READ ? "read" : "write",
	    dev, *(long *)(u + 0x4ec), *(long *)(u + 0x4f0), *(long *)(u + 0x4f4)));
	if (physck(ub_nblocks(dev), rw))
		physio(ubstrategy, (struct buf *)0, dev, rw);
	TR((CE_CONT, "ub: raw back, error %d\n", UB_UERROR));
}

int
ubread(dev)
dev_t dev;
{
	ub_rawio(dev, B_READ);
	return 0;
}

int
ubwrite(dev)
dev_t dev;
{
	ub_rawio(dev, B_WRITE);
	return 0;
}

int
ubprint(dev, str)
dev_t dev;
char *str;
{
	cmn_err(CE_NOTE, "usb%d: %s\n", UB_UNIT(dev), str);
	return 0;
}

/* for swap: the size in 512-byte blocks, -1 when the unit is not there */
int
ubsize(dev)
dev_t dev;
{
	daddr_t n = ub_nblocks(dev);

	return n ? (int)n : -1;
}
