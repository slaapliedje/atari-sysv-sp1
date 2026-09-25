/*
 * tlk - the ATW800/2's transputer links for Atari System V (TT).
 *
 * The card has two link interfaces laid out like an INMOS C011, with each
 * byte register in the low byte of a word (Programmer's Manual,
 * "Programming the Transputer"):
 *
 *   +0x01 input data   +0x03 output data   +0x05 input status   +0x07 output status
 *   +0x21 reset (write) / error (read)     +0x23 analyse
 *
 *   minor 0  C011 link  0xFEFFFAC0  a physical transputer in TRAM slot 1
 *   minor 1  FPGA link  0xFEDFFAC0  the T425 inside the FPGA
 *
 * User space cannot reach the C011: /dev/mem vets a page by reading its
 * first long, and 0xFEFFF000 bus-errors although the link registers do not.
 * The kernel maps physical I/O one to one (iosystm.h). open() reads EVERY
 * register through ioprobe() first and refuses the link (ENXIO) if any of
 * them bus-errors: on a card without the C011 some of that window's
 * registers answer and some do not, and a driver that probed only the
 * status register panicked a real TT on its first reset of that link.
 * An address that answered a read at open answers every time, so the data
 * path then reads and writes the registers directly; reset and analyse
 * still go through ioprobe().
 *
 * The link is polled: a byte moves when the status bit says so. A wait
 * spins briefly, then sleeps a tick at a time, interruptibly, up to the
 * timeout (TLK_TIMEOUT, default 2 s). A write that times out returns the
 * bytes it moved, or EIO if none; a read returns as soon as it has at
 * least one byte and the input is quiet, or EIO after the timeout.
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
#include "tlk.h"

int tlkdevflag = 0;			/* new-style (DDI) entry points */

#define R_IN	0x01
#define R_OUT	0x03
#define R_ISTAT	0x05
#define R_OSTAT	0x07
#define R_RESET	0x21
#define R_ANAL	0x23

#define NLINK	2
#define SPIN	400			/* status polls before sleeping a tick */
#define CHUNK	256			/* bytes moved per uiomove() */

#ifndef TLK_C011_BASE			/* a test build can point it elsewhere */
#define TLK_C011_BASE	0xFEFFFAC0
#endif
static unsigned long tlk_base[NLINK] = { TLK_C011_BASE, 0xFEDFFAC0 };

static struct tlk {
	caddr_t	r;			/* the registers, for ioprobe() */
	volatile unsigned char *v;	/* the same, once probed */
	int	open;
	int	timeout;		/* ticks; 0 = none */
	int	wchan;
} tlk[NLINK];


/* a register byte, or -1 if the access bus-errored */
static int
rd(t, off)
	struct tlk *t;
	int off;
{
	int v;

	if (ioprobe(IOP_READ | IOP_BYTE, t->r + off, &v))
		return -1;
	return v & 0xFF;
}

/* 0, or -1 if the access bus-errored; ioprobe writes the low byte of *&v */
static int
wr(t, off, val)
	struct tlk *t;
	int off, val;
{
	int v = val;

	return ioprobe(IOP_WRITE | IOP_BYTE, t->r + off, &v) ? -1 : 0;
}

static void
tlk_wake(t)
	struct tlk *t;
{
	wakeup((caddr_t)&t->wchan);
}

/* sleep one tick; nonzero if a signal came */
static int
tlk_tick(t)
	struct tlk *t;
{
	int id = timeout(tlk_wake, (caddr_t)t, 1);

	if (sleep((caddr_t)&t->wchan, (PZERO + 1) | PCATCH)) {
		untimeout(id);
		return 1;
	}
	return 0;
}

/* wait for bit 0 of a status register: 0 = ready, EIO = timed out,
 * EINTR */
static int
tlk_wait(t, reg)
	struct tlk *t;
	int reg;
{
	int i, ticks = 0;

	for (;;) {
		for (i = 0; i < SPIN; i++)
			if (t->v[reg] & 1)
				return 0;
		if (t->timeout && ticks++ >= t->timeout)
			return EIO;
		if (tlk_tick(t))
			return EINTR;
	}
}

static int
tlk_reset(t, analyse)
	struct tlk *t;
	int analyse;
{
	int n, v;

	if (wr(t, R_RESET, 1) || wr(t, R_ANAL, analyse ? 1 : 0))
		return EIO;
	(void) tlk_tick(t);
	(void) tlk_tick(t);
	if (wr(t, R_RESET, 0))
		return EIO;
	(void) tlk_tick(t);
	/* a byte left in the input register survives the reset */
	for (n = 0; n < 64; n++) {
		if ((v = rd(t, R_ISTAT)) < 0)
			return EIO;
		if (!(v & 1))
			break;
		if (rd(t, R_IN) < 0)
			return EIO;
	}
	return 0;
}

int
tlkinit()
{
	int i;

	for (i = 0; i < NLINK; i++) {
		tlk[i].r = (caddr_t)tlk_base[i];
		tlk[i].v = (volatile unsigned char *)tlk_base[i];
		tlk[i].timeout = 2 * HZ;
	}
	return 0;
}

/* ARGSUSED */
int
tlkopen(devp, flag, otyp, crp)
	dev_t *devp;
	int flag, otyp;
	struct cred *crp;
{
	int m = getminor(*devp), is;
	struct tlk *t;

	if (m < 0 || m >= NLINK || otyp != OTYP_CHR)
		return ENXIO;
	t = &tlk[m];
	/* every register must answer; the input data only when it holds
	 * nothing, since reading it takes a byte */
	if ((is = rd(t, R_ISTAT)) < 0 || rd(t, R_OSTAT) < 0 || rd(t, R_OUT) < 0 ||
	    rd(t, R_RESET) < 0 || rd(t, R_ANAL) < 0)
		return ENXIO;
	if (!(is & 1) && rd(t, R_IN) < 0)
		return ENXIO;
	if (t->open)
		return EBUSY;
	t->open = 1;
	return 0;
}

/* ARGSUSED */
int
tlkclose(dev, flag, otyp, crp)
	dev_t dev;
	int flag, otyp;
	struct cred *crp;
{
	tlk[getminor(dev)].open = 0;
	return 0;
}

/* ARGSUSED */
int
tlkread(dev, uiop, crp)
	dev_t dev;
	struct uio *uiop;
	struct cred *crp;
{
	struct tlk *t = &tlk[getminor(dev)];
	volatile unsigned char *r = t->v;
	unsigned char buf[CHUNK];
	int n, e, i;

	if ((e = tlk_wait(t, R_ISTAT)) != 0)
		return e;
	/* take bytes while they keep coming closely, then return */
	while (uiop->uio_resid > 0) {
		n = uiop->uio_resid < CHUNK ? uiop->uio_resid : CHUNK;
		for (i = 0; i < n; i++) {
			int k;

			for (k = 0; k < SPIN && !(r[R_ISTAT] & 1); k++)
				;
			if (k == SPIN)
				break;
			buf[i] = r[R_IN];
		}
		if (i > 0 && uiomove((caddr_t)buf, (long)i, UIO_READ, uiop))
			return EFAULT;
		if (i < n)
			break;
	}
	return 0;
}

/* ARGSUSED */
int
tlkwrite(dev, uiop, crp)
	dev_t dev;
	struct uio *uiop;
	struct cred *crp;
{
	struct tlk *t = &tlk[getminor(dev)];
	volatile unsigned char *r = t->v;
	unsigned char buf[CHUNK];
	int n, e, i, done = 0;

	while (uiop->uio_resid > 0) {
		n = uiop->uio_resid < CHUNK ? uiop->uio_resid : CHUNK;
		if (uiomove((caddr_t)buf, (long)n, UIO_WRITE, uiop))
			return EFAULT;
		for (i = 0; i < n; i++) {
			if (!(r[R_OSTAT] & 1) && (e = tlk_wait(t, R_OSTAT)) != 0) {
				/* give back what was not sent */
				uiop->uio_resid += n - i;
				return done + i ? 0 : e;
			}
			r[R_OUT] = buf[i];
		}
		done += n;
	}
	return 0;
}

/* ARGSUSED */
int
tlkioctl(dev, cmd, arg, mode, crp, rvalp)
	dev_t dev;
	int cmd, arg, mode;
	struct cred *crp;
	int *rvalp;
{
	struct tlk *t = &tlk[getminor(dev)];
	int i, o, e;

	switch (cmd) {
	case TLK_RESET:
		return tlk_reset(t, 0);
	case TLK_ANALYSE:
		return tlk_reset(t, 1);
	case TLK_STATUS:
		i = t->v[R_ISTAT];
		o = t->v[R_OSTAT];
		e = t->v[R_RESET];
		*rvalp = ((i & 1) ? TLK_ST_IN : 0) | ((o & 1) ? TLK_ST_OUT : 0) |
		    ((e & 1) ? TLK_ST_ERROR : 0);
		return 0;
	case TLK_TIMEOUT:
		if (arg < 0)
			return EINVAL;
		t->timeout = arg ? (arg * HZ + 999) / 1000 : 0;
		return 0;
	}
	return EINVAL;
}
