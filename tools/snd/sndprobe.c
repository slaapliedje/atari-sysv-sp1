/*
 * sndprobe - what the DMA sound hardware is doing (root).
 *
 *   sndprobe          a snapshot while a loud 1 kHz square wave plays
 *   sndprobe beep     1 s of 440 Hz on the YM2149 instead
 *
 * The snapshot (SND_DIAG) shows the DMA sound registers, the MICROWIRE
 * mask and data, the ring's physical address and the ring bytes at the
 * play position: the square wave's +-100 (0x64/0x9c) if the hardware is
 * playing what was written.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "../../driver-snd/snd.h"

int main(int argc, char **argv)
{
	static signed char sq[25033];
	struct snd_diag d;
	int fd, i;

	setvbuf(stdout, 0, _IONBF, 0);
	if ((fd = open("/dev/audio", O_WRONLY)) < 0) { printf("/dev/audio: errno %d\n", errno); return 1; }
	if (argc > 1 && strcmp(argv[1], "beep") == 0) {
		printf("YM2149: 440 Hz for 1 s\n");
		if (ioctl(fd, SND_BEEP, 1000) < 0) printf("SND_BEEP: errno %d\n", errno);
		return 0;
	}
	for (i = 0; i < (int)sizeof sq; i++)		/* 1 kHz at 25 kHz */
		sq[i] = (i / 12) & 1 ? 100 : -100;
	ioctl(fd, SND_SETRATE, 25033);
	ioctl(fd, SND_SETSTEREO, 0);
	write(fd, sq, 8000);
	if (ioctl(fd, SND_DIAG, &d) < 0) { printf("SND_DIAG: errno %d\n", errno); return 1; }
	printf("ring at %06lx; DMA control %02x mode %02x base %06lx end %06lx counter %06lx\n",
	    d.phys, d.ctrl, d.mode, d.base, d.end, d.count);
	printf("MICROWIRE mask %04x data %04x\n", d.mwmask, d.mwdata);
	printf("ring at the play position:");
	for (i = 0; i < 32; i++)
		printf(" %02x", d.at[i] & 0xFF);
	printf("\n");
	write(fd, sq, sizeof sq - 8000);
	ioctl(fd, SND_DRAIN, 0);
	return 0;
}
