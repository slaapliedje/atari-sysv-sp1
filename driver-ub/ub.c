/*
 * ub.c - USB for Atari System V: FreeMiNT's stack (usb.c, hub.c, the
 * ISP1160 host driver) under a character driver, /dev/ub0.  A user
 * process drives it (ubd: UBIOC_START, then UBIOC_POLL every half
 * second), so the stack's waits are sleeps and its enumeration runs in
 * process context; ubtool lists what is there and reads blocks through
 * the mass storage class driver.  The Lightning VME's chip on the VME bus
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

#include "ub.h"
#include "usb.h"
#include "hub.h"
#include "usb_api.h"
#include "ubstor.h"
#include "ubioc.h"

extern int drv_priv();
extern int ioprobe();

int ub_state = UB_NOCHIP;
static int ub_open_count;

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

/* ARGSUSED */
int
ubopen(devp, flag, otyp, crp)
dev_t *devp;
int flag, otyp;
struct cred *crp;
{
	if (getminor(*devp) != 0 || otyp != OTYP_CHR)
		return ENXIO;
	if (ub_state == UB_NOCHIP)
		return ENXIO;
	ub_open_count++;
	return 0;
}

/* ARGSUSED */
int
ubclose(dev, flag, otyp, crp)
dev_t dev;
int flag, otyp;
struct cred *crp;
{
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

/* ARGSUSED */
int
ubioctl(dev, cmd, arg, mode, crp, rvalp)
dev_t dev;
int cmd, arg, mode;
struct cred *crp;
int *rvalp;
{
	int r;

	switch (cmd) {
	case UBIOC_STATUS:
		*rvalp = ub_state;
		return 0;
	case UBIOC_START:
		if ((r = ub_start()) != 0)
			return r;
		ub_poll_hubs();
		return 0;
	case UBIOC_POLL:
		if (ub_state != UB_RUNNING)
			return ENXIO;
		ub_poll_hubs();
		return 0;
	case UBIOC_DEVS:
		return ub_devs((caddr_t)arg);
	case UBIOC_STOR:
		return ub_storlist((caddr_t)arg);
	case UBIOC_RD:
		return ub_rw((caddr_t)arg, 0);
	case UBIOC_WR:
		if (drv_priv(crp))
			return EPERM;
		return ub_rw((caddr_t)arg, 1);
	}
	return EINVAL;
}
