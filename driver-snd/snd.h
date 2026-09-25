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

#endif
