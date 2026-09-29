/*
 * snd.h - the TT's DMA sound as /dev/audio.
 *
 * write() takes 8-bit SIGNED samples, interleaved left/right in stereo
 * (left first). Rates are the hardware's: 6258, 12517, 25033, 50066 Hz.
 */
#ifndef _SND_H
#define _SND_H

#define SND_IOC		('A' << 8)
#define SND_SETRATE	(SND_IOC | 1)	/* arg = Hz (nearest of the four); returns Hz */
#define SND_SETSTEREO	(SND_IOC | 2)	/* arg = 0 mono, 1 stereo */
#define SND_DRAIN	(SND_IOC | 3)	/* wait until everything written has played */
#define SND_GETSPACE	(SND_IOC | 4)	/* returns bytes write() takes without waiting */
#define SND_GETDELAY	(SND_IOC | 5)	/* returns bytes before a new write plays */
#define SND_GETRING	(SND_IOC | 6)	/* returns the ring size in bytes */
#define SND_GETUNDERRUNS (SND_IOC | 7)	/* times the queue ran dry since open: a
					 * starved writer, and also every end of
					 * data (it cannot tell the two apart) */
#define SND_SETVOLUME	(SND_IOC | 8)	/* arg = master volume 0..40 (40 = 0 dB, 2 dB steps) */
#define SND_DIAG	(SND_IOC | 9)	/* arg = struct snd_diag *: a snapshot (root) */
#define SND_BEEP	(SND_IOC | 10)	/* arg = ms: 440 Hz on the YM2149 (root) */
#define SND_STATS	(SND_IOC | 11)	/* arg = struct snd_stats *: the frame interrupt */

/* The DMA plays the ring as a chain of blocks, one frame each; the end of
 * every frame is an MFP Timer A interrupt that queues the block after
 * next. Counted since open. */
struct snd_stats {
	unsigned long	intrs;		/* frame interrupts */
	unsigned long	late;		/* ... that came while the last one still worked */
	unsigned long	skips;		/* ... that found the DMA outside the expected block */
	unsigned long	fallbacks;	/* no interrupt for SND_QUIET ticks: back to one frame */
	unsigned long	block;		/* bytes per block */
	long		chained;	/* 1 while the blocks chain */
};

struct snd_diag {
	unsigned long	phys;		/* the ring's physical address */
	unsigned long	base, end, count;	/* as the DMA registers read */
	unsigned short	mwmask, mwdata;
	unsigned char	ctrl, mode;
	char		at[32];		/* ring bytes at the counter (no "signed": the TT's cc is pre-ANSI) */
};

#endif
