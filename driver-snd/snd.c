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
 * Between the DMA sound and the outputs sits the LMC1992 (volume, tone,
 * PSG mix), set through the MICROWIRE registers (FFFF8922 data, 8924
 * mask). ASV's psginit sends it sensible values at boot, but waits only a
 * few microseconds between commands while each takes at least 16: on a
 * real TT they overrun one another and the DMA sound stayed silent.
 * open() sends them again, waiting for each (snd_mw).
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
#define MWDATA		(*(volatile unsigned short *)0xFFFF8922UL)
#define MWMASK		(*(volatile unsigned short *)0xFFFF8924UL)
#define PSG(r, v)	(*(volatile unsigned char *)0xFFFF8800UL = (r), \
			 *(volatile unsigned char *)0xFFFF8802UL = (v))
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
extern void drv_usecwait();
extern int drv_priv(), splhi(), delay();

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
	int	volume;			/* LMC1992 master, 0..40 */
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

/* one LMC1992 command (Atari Compendium, "The MICROWIRE Interface"):
 * write the mask, then the data; the mask rotates while the 11 bits shift
 * out and reads 0x7FF again when done (at least 16 us) */
static void
snd_mw(cmd)
	int cmd;
{
	int i;

	MWMASK = 0x7FF;
	MWDATA = cmd;
	for (i = 0; i < 1000 && MWMASK == 0x7FF; i++)	/* until it starts */
		;
	for (i = 0; i < 100000 && MWMASK != 0x7FF; i++)	/* until it is done */
		;
	drv_usecwait(30);
}

/* master 0..40 (0 dB at 40, 2 dB steps), both channels at 0 dB, tone
 * flat, the PSG mixed in (as ASV's own table) */
static void
snd_mwinit(master)
	int master;
{
	snd_mw(0x401);				/* mix the PSG */
	snd_mw(0x4C0 | master);			/* master volume */
	snd_mw(0x554);				/* left: 0 dB */
	snd_mw(0x514);				/* right: 0 dB */
	snd_mw(0x486);				/* treble: flat */
	snd_mw(0x446);				/* bass: flat */
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
	snd.volume = 40;			/* 0 dB */
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
	snd_mwinit(snd.volume);
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
	case SND_SETVOLUME:
		if (arg < 0 || arg > 40)
			return EINVAL;
		snd.volume = arg;
		snd_mw(0x4C0 | arg);
		return 0;
	case SND_DIAG:
		if (drv_priv(crp))
			return EPERM;
		{
			struct snd_diag d;
			int p;

			bzero((caddr_t)&d, sizeof d);
			d.phys = snd.phys;
			d.base = (unsigned long)REG(R_BASE) << 16 | REG(R_BASE + 2) << 8 | REG(R_BASE + 4);
			d.end = (unsigned long)REG(R_END) << 16 | REG(R_END + 2) << 8 | REG(R_END + 4);
			d.count = (unsigned long)REG(R_COUNT) << 16 | REG(R_COUNT + 2) << 8 | REG(R_COUNT + 4);
			d.mwmask = MWMASK;
			d.mwdata = MWDATA;
			d.ctrl = REG(R_CTRL);
			d.mode = REG(R_MODE);
			if (snd.ring) {
				p = snd_ppos();
				for (i = 0; i < 32; i++)
					d.at[i] = snd.ring[(p + i) & (RING - 1)];
			}
			return copyout((caddr_t)&d, (caddr_t)arg, sizeof d) ? EFAULT : 0;
		}
	case SND_BEEP:
		if (drv_priv(crp))
			return EPERM;
		if (arg < 1 || arg > 5000)
			return EINVAL;
		t = splhi();
		PSG(0, 284 & 0xFF);		/* 2 MHz / (16 x 440) */
		PSG(1, 284 >> 8);
		PSG(8, 15);			/* channel A: full volume */
		PSG(7, 0xFE);			/* tone A on (ports A, B stay outputs) */
		splx(t);
		delay((arg * HZ + 999) / 1000);
		t = splhi();
		PSG(8, 0);
		PSG(7, 0xFF);
		splx(t);
		return 0;
	case SND_GETUNDERRUNS:
		*rvalp = snd.underruns;
		return 0;
	}
	return EINVAL;
}
