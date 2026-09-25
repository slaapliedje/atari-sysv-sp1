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

#define TLK_ST_IN	1		/* a byte is waiting to be read */
#define TLK_ST_OUT	2		/* the link takes a byte */
#define TLK_ST_ERROR	4		/* the transputer's error flag */

#endif
