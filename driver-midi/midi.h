/*
 * midi.h - the TT's MIDI port as /dev/midi (raw) and /dev/midit (timed).
 *
 * write() takes MIDI bytes. On /dev/midit the stream may also carry
 *   0xF9 n      wait n ms (0..255) before the bytes that follow, counted
 *               from the previous wait - chain two for longer
 * so a writer can hand over a second of music at once and the kernel
 * plays it in time. 0xF9 is an undefined MIDI real-time byte and is
 * never sent to the port.
 */
#ifndef _MIDI_H
#define _MIDI_H

#define MIDI_IOC	('M' << 8)
#define MIDI_FLUSH	(MIDI_IOC | 1)	/* drop what is queued; the timeline restarts now */
#define MIDI_GETQUEUED	(MIDI_IOC | 2)	/* returns bytes not yet sent (waits included) */
#define MIDI_DRAIN	(MIDI_IOC | 3)	/* wait until everything written has gone out */
#define MIDI_STATS	(MIDI_IOC | 4)	/* arg = struct midi_stats * */
#define MIDI_GETRING	(MIDI_IOC | 5)	/* returns the ring size in bytes */

struct midi_stats {
	unsigned long	bytes;		/* sent to the port */
	unsigned long	intrs;		/* transmit interrupts taken */
	unsigned long	waits;		/* timed waits honoured */
	unsigned long	late;		/* ... that were already due (the writer was behind) */
	unsigned long	snaps;		/* timeline restarted at now (behind by > 4 ticks) */
	unsigned long	kicks;		/* the clock found the port idle with bytes waiting */
	unsigned long	queued;		/* bytes waiting now */
	unsigned long	hz;		/* the clock's ticks a second */
};

#endif
