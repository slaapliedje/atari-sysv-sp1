/*
 * miditool - exercise the midi driver from the shell.
 *   miditool raw HEX...     write bytes to /dev/midi
 *   miditool phrase         /dev/midi: C E G C, the program waits between
 *   miditool timed          /dev/midit: the same phrase handed over in ONE
 *                           write with $F9 waits; the kernel times it
 *   miditool flush          /dev/midit: queue 4 s of notes, flush after 1 s,
 *                           all notes off: tests MIDI_FLUSH
 * Each ends with the driver's statistics.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include "midi.h"

static int fd;

static void stats(void)
{
	struct midi_stats st;
	if (ioctl(fd, MIDI_STATS, &st) < 0) { perror("MIDI_STATS"); return; }
	printf("stats: bytes %lu intrs %lu waits %lu late %lu snaps %lu kicks %lu queued %lu hz %lu\n",
	       st.bytes, st.intrs, st.waits, st.late, st.snaps, st.kicks, st.queued, st.hz);
}

static void put(const unsigned char *b, int n)
{
	if (write(fd, (char *)b, n) != n) perror("write");
}

static void nap(int ms) { poll((struct pollfd *)0, 0, ms); }

int main(int argc, char **argv)
{
	static const unsigned char notes[4] = { 60, 64, 67, 72 };
	unsigned char buf[256];
	int n = 0, i, timed;
	const char *cmd = argc > 1 ? argv[1] : "phrase";

	timed = strcmp(cmd, "timed") == 0 || strcmp(cmd, "flush") == 0;
	fd = open(timed ? "/dev/midit" : "/dev/midi", O_WRONLY);
	if (fd < 0) { perror(timed ? "/dev/midit" : "/dev/midi"); return 1; }
	if (strcmp(cmd, "raw") == 0) {
		for (i = 2; i < argc && n < 256; i++) buf[n++] = (unsigned char)strtol(argv[i], 0, 16);
		put(buf, n);
	} else if (strcmp(cmd, "phrase") == 0) {
		buf[0] = 0xC0; buf[1] = 0; put(buf, 2);
		for (i = 0; i < 4; i++) {
			buf[0] = 0x90; buf[1] = notes[i]; buf[2] = 0x60; put(buf, 3);
			nap(300);
			buf[0] = 0x80; buf[1] = notes[i]; buf[2] = 0x40; put(buf, 3);
		}
	} else if (strcmp(cmd, "timed") == 0) {
		buf[n++] = 0xF9; buf[n++] = 0;
		buf[n++] = 0xC0; buf[n++] = 0;
		for (i = 0; i < 4; i++) {
			buf[n++] = 0x90; buf[n++] = notes[i]; buf[n++] = 0x60;
			buf[n++] = 0xF9; buf[n++] = 250;		/* note length */
			buf[n++] = 0x80; buf[n++] = notes[i]; buf[n++] = 0x40;
			buf[n++] = 0xF9; buf[n++] = 50;			/* gap */
		}
		for (i = 0; i < 3; i++) { buf[n++] = 0x90; buf[n++] = notes[i]; buf[n++] = 0x60; }
		buf[n++] = 0xF9; buf[n++] = 255; buf[n++] = 0xF9; buf[n++] = 245;	/* 500 ms */
		buf[n++] = 0xB0; buf[n++] = 123; buf[n++] = 0;
		put(buf, n);					/* one write: 1.7 s of music */
		printf("handed over %d bytes in one write\n", n);
		if (ioctl(fd, MIDI_DRAIN, 0) < 0) perror("MIDI_DRAIN");
	} else if (strcmp(cmd, "flush") == 0) {
		buf[n++] = 0xF9; buf[n++] = 0;
		for (i = 0; i < 16; i++) {			/* 4 s: a note every 250 ms */
			buf[n++] = 0x90; buf[n++] = notes[i & 3]; buf[n++] = 0x60;
			buf[n++] = 0xF9; buf[n++] = 200;
			buf[n++] = 0x80; buf[n++] = notes[i & 3]; buf[n++] = 0x40;
			buf[n++] = 0xF9; buf[n++] = 50;
		}
		put(buf, n);
		nap(1000);
		stats();
		if (ioctl(fd, MIDI_FLUSH, 0) < 0) perror("MIDI_FLUSH");
		printf("flushed after 1 s: only ~4 notes should have sounded\n");
		buf[0] = 0xB0; buf[1] = 123; buf[2] = 0; put(buf, 3);
	}
	stats();
	close(fd);
	return 0;
}
