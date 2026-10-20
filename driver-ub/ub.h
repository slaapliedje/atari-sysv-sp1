/*
 * ub.h - what FreeMiNT's USB stack (U-Boot's, GPL-2) needs from Atari
 * System V: the bus to the ISP1160 (the Lightning VME's registers on the
 * VME bus, the NetUSBee's through the cartridge port's read-only window),
 * delays, printing, byte order.  The same sources build as the kernel
 * driver (-D_KERNEL) and as the user-space harness ubuser (through
 * /dev/mem, which runs from TT-RAM and so sees clean bus cycles).
 */
#ifndef UB_H
#define UB_H

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#define ub_printf	ub_kprintf
void ub_kprintf();
#define memset(p, c, n)	ub_memset((char *)(p), (c), (n))
#define memcpy(d, s, n)	bcopy((caddr_t)(s), (caddr_t)(d), (n))
void ub_memset();
char *strcat();
int sprintf();
extern void delay();
extern void drv_usecwait();
extern int spl6();
extern void splx();
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
/* the harness prints through write(2), unbuffered, so a log shows where
 * it is while it runs (and no FILE is touched: see ubtool.c) */
void ub_uprintf(char *fmt, ...);
#define ub_printf	ub_uprintf
#endif

#ifndef NULL
#define NULL	0
#endif
#define E_OK	0
#ifndef ENOSYS
#define ENOSYS	89
#endif
#ifndef MAX
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#endif
#define _cdecl
#define MAINUSB
#define UNUSED(x)	(void)(x)
#define INLINE	static
#define inline

/* printing: ALERT always, DEBUG with -DUB_DEBUG */
#define ALERT(x)	do { ub_printf x; ub_printf("\n"); } while (0)
#define FORCE(x)	ALERT(x)
#ifdef UB_DEBUG
#define DEBUG(x)	ALERT(x)
#else
#define DEBUG(x)
#endif
#define TRACE(x)
#define ASSERT(x)

/* byte order: the chip and the bus are little-endian, the 68030 is not */
#define SWAP16(x)	((unsigned short)((((x) & 0xff) << 8) | (((x) >> 8) & 0xff)))
#define SWAP32(x)	((unsigned long)(((x) >> 24) | (((x) >> 8) & 0xff00) | \
			 (((x) << 8) & 0xff0000) | ((x) << 24)))
#define cpu2le16(x)	SWAP16(x)
#define le2cpu16(x)	SWAP16(x)
#define cpu2le32(x)	SWAP32(x)
#define le2cpu32(x)	SWAP32(x)

/* time */
void ub_udelay();		/* (us) */
void ub_mdelay();		/* (ms) */
#define udelay(u)	ub_udelay((long)(u))
#define mdelay(m)	ub_mdelay((long)(m))

/* interrupts around a chip transaction (the kernel: spl) */
void ub_int_off();
void ub_int_on();
#define MINT_INT_OFF	ub_int_off()
#define MINT_INT_ON	ub_int_on()
#define TOS_INT_OFF
#define TOS_INT_ON
#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif
#define FS_INFO		('U' << 8 | 99)	/* the MiNT version query, unused */

/* the cartridge port has no interrupt line and ASV polls the chip: the
 * MiNT driver's IKBD-polling hack has nothing to do here */
#define ikbd_int_pending()	0
#define fake_ikbd_int()

/*
 * The bus (ub_io.c).  Register words come "raw" (as the chip orders
 * them, settled at init by reading HcChipID both ways); FIFO data words
 * come the other way round, as in the MiNT drivers.
 */
#define UB_LIGHTNING	1
#define UB_NETUSBEE	2
extern int ub_bus;
extern int ub_swap;		/* raw words are byte-swapped on the bus */
extern int ub_fifo_wswap, ub_fifo_rswap;	/* FIFO data words written/read the other way round from raw */
int ub_io_init();		/* (bus) 0, or -1 when the chip is not there */
void ub_wraddr();		/* (reg) */
unsigned ub_rd16();		/* () raw */
void ub_wr16();			/* (val) raw */
unsigned ub_rd16plain();	/* () a FIFO data word */
void ub_wr16plain();		/* (val) */

#endif /* UB_H */
