/*
 * snd - the TT's DMA sound as /dev/audio, for Atari System V.
 *
 * The STe/TT DMA sound plays 8-bit signed samples straight from ST-RAM
 * (Atari Compendium, "STe/TT030 DMA Sound"): byte registers at 0xFFFF8901
 * (control: 1 = play once, 3 = repeat), the frame base address (8903/05/
 * 07), the frame address counter (8909/0B/0D, read only), the frame end
 * address (890F/11/13) and the mode (8921: bit 7 mono, bits 1-0 rate).
 * Samples must lie in the first 4 MB and not in TT-RAM.
 *
 * This driver keeps a ring in ST-RAM (iomem_alloc with the ST attribute,
 * 0x40000000, as the kernel's own video memory), allocated at boot in
 * sndstart() as the floppy driver does: on a running TT the ST-RAM is
 * gone and a later iomem_alloc fails. The hardware plays it
 * in repeat mode for as long as the device is open. write() copies into
 * the ring behind the "cleaned" position; once a tick the driver reads
 * the frame address counter and zeroes what has played since, so a writer
 * that falls behind leaves silence playing, not old samples. When the
 * play position passes everything written (an underrun), the write
 * position restarts a little ahead of it: that lead is silence ("gap"),
 * played before anything written after the restart.
 *
 * Build WITHOUT -O (install.sh): ASV's cc ignores volatile when it
 * optimises and would read the counter once.
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
#include "snd.h"

int snddevflag = 0;			/* new-style (DDI) entry points */

#define REG(o)		(*(volatile unsigned char *)(0xFFFF8900UL + (o)))
#define R_CTRL		0x01
#define R_BASE		0x03		/* high, mid (+2), low (+4) */
#define R_COUNT		0x09
#define R_END		0x0F
#define R_MODE		0x21

#define RING		0x8000		/* 32 KB: 1.3 s of 25 kHz mono */
#define LEAD		512		/* after an underrun, restart this far ahead */
#define BOUNCE		1024		/* write() moves this much per step (on the kernel stack) */
#define ST_RAM		0x40000000	/* iomem_alloc attribute: ST-RAM */

extern caddr_t iomem_alloc();
extern long kvtophys();
extern int timeout(), untimeout(), sleep();
extern void wakeup();
extern int splclock(), splx();

static struct {
	int	open;
	caddr_t	ring;			/* kernel address */
	unsigned long phys;		/* its physical address */
	int	mode;			/* R_MODE value */
	int	wpos;			/* next byte write() fills */
	int	cpos;			/* play position as of the last tick */
	int	filled;			/* bytes written, not yet played */
	int	gap;			/* silence ahead of them (after a restart) */
	int	tid;			/* the tick's timeout id, 0 = none */
	int	underruns;
} snd;

static int rates[4] = { 6258, 12517, 25033, 50066 };

#define USED(a, b)	(((b) - (a)) & (RING - 1))	/* bytes from a to b */

/* the frame address counter, as an offset into the ring; read until two
 * reads agree, since it moves between the three byte reads */
static int
snd_ppos()
{
	unsigned long a, b;

	do {
		a = (unsigned long)REG(R_COUNT) << 16 | REG(R_COUNT + 2) << 8 | REG(R_COUNT + 4);
		b = (unsigned long)REG(R_COUNT) << 16 | REG(R_COUNT + 2) << 8 | REG(R_COUNT + 4);
	} while (a != b);
	return (int)((a - snd.phys) & (RING - 1));
}

static void
snd_setaddr(reg, a)
	int reg;
	unsigned long a;
{
	REG(reg) = (a >> 16) & 0xFF;
	REG(reg + 2) = (a >> 8) & 0xFF;
	REG(reg + 4) = a & 0xFE;
}

/* once a tick: zero what has played since the last tick, and wake a writer */
static void
snd_tick()
{
	int p = snd_ppos(), n = USED(snd.cpos, p), i;

	for (i = 0; i < n; i++)
		snd.ring[(snd.cpos + i) & (RING - 1)] = 0;
	if (n > snd.gap + snd.filled) {		/* played past the data */
		if (snd.filled)
			snd.underruns++;
		snd.wpos = (p + LEAD) & (RING - 1) & ~1;
		snd.gap = USED(p, snd.wpos);
		snd.filled = 0;
	} else if (n > snd.gap) {
		snd.filled -= n - snd.gap;
		snd.gap = 0;
	} else
		snd.gap -= n;
	snd.cpos = p;
	wakeup((caddr_t)&snd);
	snd.tid = snd.open ? timeout(snd_tick, (caddr_t)0, 1) : 0;
}

static void
snd_start()
{
	int i;

	REG(R_CTRL) = 0;
	for (i = 0; i < RING; i++)
		snd.ring[i] = 0;
	snd_setaddr(R_BASE, snd.phys);
	snd_setaddr(R_END, snd.phys + RING);
	REG(R_MODE) = snd.mode;
	snd.cpos = snd.filled = 0;
	snd.wpos = snd.gap = LEAD;
	REG(R_CTRL) = 3;			/* play, repeat */
}

int
sndinit()
{
	snd.mode = 0x80 | 2;			/* mono, 25033 Hz */
	return 0;
}

/* at boot, after memory is set up: the ring, while ST-RAM is still free */
int
sndstart()
{
	snd.ring = iomem_alloc(RING, ST_RAM);
	if (snd.ring == 0) {
		cmn_err(CE_WARN, "snd: no %d bytes of ST-RAM for the ring", RING);
		return 0;
	}
	snd.phys = (unsigned long)kvtophys(snd.ring);
	if (snd.phys + RING > 0x400000 || (snd.phys & 1)) {
		cmn_err(CE_WARN, "snd: ring at %x: not in the first 4 MB", snd.phys);
		snd.ring = 0;
	}
	return 0;
}

/* ARGSUSED */
int
sndopen(devp, flag, otyp, crp)
	dev_t *devp;
	int flag, otyp;
	struct cred *crp;
{
	if (getminor(*devp) != 0 || otyp != OTYP_CHR)
		return ENXIO;
	if (snd.open)
		return EBUSY;
	if (snd.ring == 0)		/* sndstart found no usable ST-RAM */
		return ENOMEM;
	snd.open = 1;
	snd.underruns = 0;
	snd_start();
	snd.tid = timeout(snd_tick, (caddr_t)0, 1);
	return 0;
}

/* ARGSUSED */
int
sndclose(dev, flag, otyp, crp)
	dev_t dev;
	int flag, otyp;
	struct cred *crp;
{
	int t;

	/* let what was written play out, a few seconds at most */
	for (t = 0; snd.filled > 0 && t < 5 * HZ; t++)
		if (sleep((caddr_t)&snd, (PZERO + 1) | PCATCH))
			break;
	REG(R_CTRL) = 0;
	snd.open = 0;
	if (snd.tid)
		untimeout(snd.tid);
	snd.tid = 0;
	return 0;
}

/* ARGSUSED */
int
sndwrite(dev, uiop, crp)
	dev_t dev;
	struct uio *uiop;
	struct cred *crp;
{
	char buf[BOUNCE];
	int n, room, chunk, s, i;

	while (uiop->uio_resid > 0) {
		/* take a piece from the user at base level (it may fault),
		 * then put it in the ring at splclock, where the tick cannot
		 * move the write position underneath */
		n = uiop->uio_resid < BOUNCE ? uiop->uio_resid : BOUNCE;
		s = splclock();
		while ((room = RING - 2 - snd.gap - snd.filled) < n)
			if (sleep((caddr_t)&snd, (PZERO + 1) | PCATCH)) {
				splx(s);
				return EINTR;
			}
		splx(s);
		if (uiomove(buf, (long)n, UIO_WRITE, uiop))
			return EFAULT;
		s = splclock();
		for (i = 0; i < n; ) {
			chunk = RING - snd.wpos < n - i ? RING - snd.wpos : n - i;
			bcopy(buf + i, snd.ring + snd.wpos, chunk);
			snd.wpos = (snd.wpos + chunk) & (RING - 1);
			i += chunk;
		}
		snd.filled += n;
		splx(s);
	}
	return 0;
}

/* ARGSUSED */
int
sndioctl(dev, cmd, arg, mode, crp, rvalp)
	dev_t dev;
	int cmd, arg, mode;
	struct cred *crp;
	int *rvalp;
{
	int i, best, t;

	switch (cmd) {
	case SND_SETRATE:
		for (best = 0, i = 1; i < 4; i++)
			if ((arg > rates[i] ? arg - rates[i] : rates[i] - arg) <
			    (arg > rates[best] ? arg - rates[best] : rates[best] - arg))
				best = i;
		snd.mode = (snd.mode & 0x80) | best;
		REG(R_MODE) = snd.mode;
		*rvalp = rates[best];
		return 0;
	case SND_SETSTEREO:
		snd.mode = (snd.mode & 3) | (arg ? 0 : 0x80);
		REG(R_MODE) = snd.mode;
		return 0;
	case SND_DRAIN:
		for (t = 0; snd.filled > 0 && t < 30 * HZ; t++)
			if (sleep((caddr_t)&snd, (PZERO + 1) | PCATCH))
				return EINTR;
		return 0;
	case SND_GETSPACE:
		*rvalp = RING - 2 - snd.gap - snd.filled;
		return 0;
	case SND_GETDELAY:
		*rvalp = snd.gap + snd.filled;
		return 0;
	case SND_GETRING:
		*rvalp = RING;
		return 0;
	case SND_GETUNDERRUNS:
		*rvalp = snd.underruns;
		return 0;
	}
	return EINVAL;
}
