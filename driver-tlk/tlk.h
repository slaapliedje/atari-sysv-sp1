/*
 * tlk.h - the ATW800/2's transputer links as character devices.
 *
 *   /dev/link0  the C011 link: a physical transputer in TRAM slot 1
 *   /dev/link1  the FPGA link: the T425 inside the card's FPGA
 *
 * read() and write() move bytes over the link. ioctl() controls the
 * transputer behind it.
 */
#ifndef _TLK_H
#define _TLK_H

#define TLK_IOC		('L' << 8)	/* not 'T': TCGETA is ('T'<<8)|1, and isatty() sends it */
#define TLK_RESET	(TLK_IOC | 1)	/* reset (analyse off); drains the input */
#define TLK_ANALYSE	(TLK_IOC | 2)	/* reset with analyse asserted */
#define TLK_STATUS	(TLK_IOC | 3)	/* returns TLK_ST_* bits */
#define TLK_TIMEOUT	(TLK_IOC | 4)	/* arg = ms a read/write may wait; 0 = no limit */
#define TLK_PACE	(TLK_IOC | 6)	/* arg = CPU loops after a not-ready poll (root); returns the old */
#define TLK_DIAG	(TLK_IOC | 5)	/* arg = struct tlk_diag *: link timing (root) */

#define TLK_ST_IN	1		/* a byte is waiting to be read */
#define TLK_ST_OUT	2		/* the link takes a byte */
#define TLK_ST_ERROR	4		/* the transputer's error flag */


/*
 * TLK_DIAG: time the link from inside the kernel. mode 1 = input: for up
 * to 64 bytes, the input-status polls each one took to arrive; mode 2 =
 * output: POKE commands (to 0x80000100) byte by byte, the output-status
 * polls before each. nspoll = the measured cost of one status poll, so
 * polls * nspoll is the wait in ns. mode 3 = read one input byte, then
 * sample the input status 64 times back to back into data[]; mode 4 =
 * the same for one POKE command byte and the output status. mode 5 =
 * mode 1 with a pause of n CPU loops (no bus access) between polls.
 * polls * nspoll is the wait in ns. The run stops after TLK_DIAG_LIMIT
 * polls in all (about two seconds; n = bytes timed).
 */
#define TLK_DIAG_LIMIT	2000000
struct tlk_diag {
	int	mode;
	int	n;
	int	nspoll;
	int	polls[64];
	unsigned char data[64];		/* mode 1: the bytes read; modes 3/4: raw status */
};

#endif
