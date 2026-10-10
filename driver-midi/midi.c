/*
 * midi - the TT's MIDI port (the 6850 ACIA at $FFFFFC04/06) as /dev/midi
 * and /dev/midit, for Atari System V.
 *
 * The stock kernel has no MIDI driver: its keyboard driver (ikbd) sets up
 * and services only the keyboard ACIA, and the two ACIAs share one
 * interrupt line, the ST MFP's GPIP4 (line 6, ipl 6). This driver takes
 * that line over at its first open with a handler that serves the MIDI
 * ACIA and then calls the kernel's own ikbdint for the keyboard, looping
 * until neither chip asserts its interrupt - as TOS does: the line is
 * edge-triggered, and a chip left asserting it would silence the other
 * for good. Before the first open the stock handler runs untouched.
 *
 * Output only. write() queues bytes in a ring and the ACIA's transmit
 * interrupt (control $35: transmit interrupt on, 8N1, /16 = 31250 baud)
 * moves them out, one per 320 us, with no polling; the control goes back
 * to $15 (no interrupts) when the ring is empty. Receive interrupts stay
 * off - TOS leaves them on, and nothing under ASV would read MIDI IN.
 *
 * Minor 0, /dev/midi: raw. Minor 1, /dev/midit: timed - the stream may
 * carry $F9 n, "wait n ms before the bytes that follow", counted from the
 * previous wait, so a writer can hand over a second of music at once and
 * the kernel plays it in time, from the clock's ticks (hertz, 128). A
 * stream that has fallen more than 4 ticks behind the clock restarts its
 * timeline at now, so a writer that stalled resumes at the right speed
 * rather than in a rush. MIDI_FLUSH drops what is queued and restarts
 * the timeline too; the writer then sends its own notes-off.
 *
 * Build on the PC with ../kbuild.sh (GCC 2.7.2 -O2 honours volatile);
 * the TT's own cc is the fallback in install.sh, without -O.
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
#include "midi.h"

int mididevflag = 0;			/* new-style (DDI) entry points */

#define ACIA_CTRL	(*(volatile unsigned char *)0xFFFFFC04UL)
#define ACIA_DATA	(*(volatile unsigned char *)0xFFFFFC06UL)
#define KBD_CTRL	(*(volatile unsigned char *)0xFFFFFC00UL)
#define S_RDRF		0x01
#define S_TDRE		0x02
#define S_IRQ		0x80
#define CR_IDLE		0x15		/* /16, 8N1, RTS low, no interrupts */
#define CR_SEND		0x35		/* ... and the transmit interrupt on */

/* the ST MFP (Atari Compendium, MC68901): GPIP4 is channel 6, in the B registers */
#define MFP(o)		(*(volatile unsigned char *)(0xFFFFFA00UL + (o)))
#define M_ISRB		0x11
#define ACIA_BIT	0x40
#define ACIA_LINE	6
#define ACIA_IPL	6		/* the keyboard driver's level: the line is shared */

#define RING		8192		/* 2.6 s at full rate; ~40 s of a game's music */
#define BOUNCE		512		/* write() moves this much per step (kernel stack) */
#define WAIT		0xF9		/* the timed stream's wait command */
#define SNAP_TICKS	4		/* this far behind, the timeline restarts at now */

extern int timeout(), untimeout(), sleep(), spl6(), splx();
extern int mfp_intrreq(), ikbdint(), drv_priv();
extern void wakeup();
extern int hertz;			/* the clock's ticks per second (128) */
extern long lbolt;			/* ticks since boot */

static struct {
	int	open;
	int	timed;			/* minor 1: WAIT commands are honoured */
	int	hooked;			/* our handler is on the ACIA line */
	unsigned char ring[RING];
	int	head, tail;		/* head = next to send, tail = next free */
	int	sending;		/* the transmit interrupt is on */
	int	waiting;		/* a wait's timeout is pending */
	int	tid;
	long	start;			/* the timeline's origin, in ticks */
	long	due_ms;			/* the next bytes' time, ms from start */
	long	due;			/* ... in ticks */
	struct midi_stats st;
} midi;

#define USED()		((midi.tail - midi.head) & (RING - 1))
#define FREE()		(RING - 1 - USED())

static void midi_kick();

static long
ms_to_ticks(ms)
	long ms;
{
	return (ms / 1000) * hertz + ((ms % 1000) * hertz) / 1000;
}

static void
midi_quiet()
{
	if (midi.sending) {
		ACIA_CTRL = CR_IDLE;
		midi.sending = 0;
	}
}

/* a timed wait is over */
static void
midi_wake()
{
	int s = spl6();

	midi.waiting = 0;
	midi.tid = 0;
	if (midi.open)
		midi_kick();
	splx(s);
}

/* Send what can go now. Called with the ACIA interrupt masked (spl6, or
 * from the handler itself). Leaves the transmit interrupt on when bytes
 * remain and the port is busy with one, off otherwise. */
static void
midi_kick()
{
	unsigned char b;
	long now;

	for (;;) {
		if (midi.head == midi.tail) {
			midi_quiet();
			wakeup((caddr_t)&midi);
			return;
		}
		b = midi.ring[midi.head];
		if (midi.timed && b == WAIT) {
			if (USED() < 2) {	/* its count is not here yet */
				midi_quiet();
				return;
			}
			now = lbolt;
			if (midi.due + SNAP_TICKS < now) {	/* far behind: from now */
				midi.start = now;
				midi.due_ms = 0;
				midi.st.snaps++;
			}
			midi.due_ms += midi.ring[(midi.head + 1) & (RING - 1)];
			midi.due = midi.start + ms_to_ticks(midi.due_ms);
			midi.head = (midi.head + 2) & (RING - 1);
			midi.st.waits++;
			if (midi.due > now) {
				midi_quiet();
				midi.waiting = 1;
				midi.tid = timeout(midi_wake, (caddr_t)0, (int)(midi.due - now));
				return;
			}
			midi.st.late++;
			continue;
		}
		if (!(ACIA_CTRL & S_TDRE)) {
			if (!midi.sending) {
				ACIA_CTRL = CR_SEND;
				midi.sending = 1;
			}
			return;			/* the transmit interrupt resumes us */
		}
		ACIA_DATA = b;
		midi.head = (midi.head + 1) & (RING - 1);
		midi.st.bytes++;
		if (!midi.sending) {
			ACIA_CTRL = CR_SEND;
			midi.sending = 1;
		}
	}
}

/* The shared ACIA line: our chip, then the keyboard's, until neither
 * asserts the interrupt. ikbdint clears the whole ISRB itself. */
static void
midiint()
{
	unsigned char b;

	MFP(M_ISRB) = (unsigned char)~ACIA_BIT;	/* GPIP4 no longer in service */
	for (;;) {
		if (ACIA_CTRL & S_IRQ) {
			if (midi.open) {
				midi.st.intrs++;
				midi_kick();
			} else {		/* not ours to serve, but the line must clear */
				if (ACIA_CTRL & S_RDRF)
					b = ACIA_DATA;
				ACIA_CTRL = CR_IDLE;
			}
		}
		ikbdint();
		if (!(ACIA_CTRL & S_IRQ) && !(KBD_CTRL & S_IRQ))
			return;
	}
}

/* once a tick while open: a lost edge would leave bytes waiting with the
 * port idle; resume them */
static void
midi_tick()
{
	int s = spl6();

	if (midi.open) {
		if (midi.head != midi.tail && !midi.sending && !midi.waiting) {
			midi.st.kicks++;
			midi_kick();
		}
		timeout(midi_tick, (caddr_t)0, 1);
	}
	splx(s);
}

int
midiinit()
{
	return 0;
}

int
midistart()
{
	return 0;
}

/* ARGSUSED */
int
midiopen(devp, flag, otyp, crp)
	dev_t *devp;
	int flag, otyp;
	struct cred *crp;
{
	int m = getminor(*devp), s;
	unsigned char b;

	if ((m != 0 && m != 1) || otyp != OTYP_CHR)
		return ENXIO;
	if (midi.open)
		return EBUSY;
	s = spl6();
	midi.timed = m;
	midi.head = midi.tail = 0;
	midi.sending = midi.waiting = 0;
	midi.tid = 0;
	midi.start = lbolt;
	midi.due_ms = 0;
	midi.due = lbolt;
	bzero((caddr_t)&midi.st, sizeof midi.st);
	ACIA_CTRL = CR_IDLE;			/* TOS's receive interrupt off */
	if (ACIA_CTRL & S_RDRF)
		b = ACIA_DATA;
	if (!midi.hooked) {
		mfp_intrreq(ACIA_LINE, 0, midiint, 0, 1, 0, ACIA_IPL);
		midi.hooked = 1;
	}
	midi.open = 1;
	timeout(midi_tick, (caddr_t)0, 1);
	splx(s);
	return 0;
}

/* ARGSUSED */
int
midiclose(dev, flag, otyp, crp)
	dev_t dev;
	int flag, otyp;
	struct cred *crp;
{
	int s, t;

	/* let what was written go out, a few seconds at most */
	for (t = 0; midi.head != midi.tail && t < 5 * hertz; t++)
		if (sleep((caddr_t)&midi, (PZERO + 1) | PCATCH))
			break;
	s = spl6();
	midi_quiet();
	if (midi.waiting && midi.tid)
		untimeout(midi.tid);
	midi.waiting = 0;
	midi.tid = 0;
	midi.head = midi.tail = 0;
	midi.open = 0;			/* midi_tick stops itself */
	splx(s);
	return 0;
}

/* ARGSUSED */
int
midiwrite(dev, uiop, crp)
	dev_t dev;
	struct uio *uiop;
	struct cred *crp;
{
	char buf[BOUNCE];
	int n, s, i;

	while (uiop->uio_resid > 0) {
		n = uiop->uio_resid < BOUNCE ? uiop->uio_resid : BOUNCE;
		s = spl6();
		while (FREE() < n)
			if (sleep((caddr_t)&midi, (PZERO + 1) | PCATCH)) {
				splx(s);
				return EINTR;
			}
		splx(s);
		if (uiomove(buf, (long)n, UIO_WRITE, uiop))
			return EFAULT;
		s = spl6();
		for (i = 0; i < n; i++) {
			midi.ring[midi.tail] = buf[i];
			midi.tail = (midi.tail + 1) & (RING - 1);
		}
		if (!midi.sending && !midi.waiting)
			midi_kick();
		splx(s);
	}
	return 0;
}

/* ARGSUSED */
int
midiioctl(dev, cmd, arg, mode, crp, rvalp)
	dev_t dev;
	int cmd, arg, mode;
	struct cred *crp;
	int *rvalp;
{
	int s, t;

	switch (cmd) {
	case MIDI_FLUSH:
		s = spl6();
		midi.head = midi.tail;
		if (midi.waiting && midi.tid)
			untimeout(midi.tid);
		midi.waiting = 0;
		midi.tid = 0;
		midi_quiet();
		midi.start = lbolt;
		midi.due_ms = 0;
		midi.due = lbolt;
		splx(s);
		wakeup((caddr_t)&midi);
		return 0;
	case MIDI_GETQUEUED:
		*rvalp = USED();
		return 0;
	case MIDI_DRAIN:
		for (t = 0; midi.head != midi.tail && t < 30 * hertz; t++)
			if (sleep((caddr_t)&midi, (PZERO + 1) | PCATCH))
				return EINTR;
		return 0;
	case MIDI_STATS:
		midi.st.queued = USED();
		midi.st.hz = hertz;
		return copyout((caddr_t)&midi.st, (caddr_t)arg, sizeof midi.st) ? EFAULT : 0;
	case MIDI_GETRING:
		*rvalp = RING;
		return 0;
	}
	return EINVAL;
}
