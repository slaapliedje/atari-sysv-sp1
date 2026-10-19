/*
 * From Ash Nazag (github.com/kdedon/ashnazag, kernel/atari/netusbee,
 * 5ae4923), MIT licence: see LICENSE.ashnazag.
 *
 * NetUSBee Ethernet chip core: probe, ring, transmit, receive.  No
 * interrupt line reaches the cartridge port, so the caller polls nu_poll.
 * STREAMS-free; builds on the host against a simulated card (NU_HOST).
 */
#ifndef NU_H
#define NU_H

#include "nureg.h"

#define NU_NTX		2		/* transmit buffers, 6 pages each */
#define NU_TXPAGES	6
#define NU_MINFRAME	60		/* without CRC */
#define NU_MAXFRAME	1514		/* without CRC */
#define NU_RXBURST	8		/* frames per poll */
#define NU_NMC		16		/* multicast addresses */

struct nu_stats {
	unsigned long	ipackets, ierrors, opackets, oerrors, collisions;
	unsigned long	crc, fae, missed, ovw, rxbad;
	unsigned long	txfu, txabt, txcarrier, txowc, txtimeout, reinit, nomem;
	unsigned long	polls;
	unsigned long	initbad;	/* inits whose read-back differed */
	unsigned long	crmiss;		/* page selects that had to be redone */
	unsigned long	recover;	/* full re-probes after failed inits */
	unsigned long	cfgfix;		/* RTL8019AS config registers put back */
	unsigned long	txdead;		/* frames dropped while the chip was down */
};

struct nu_softc {
	int		memlo, memhi;	/* buffer memory pages [lo, hi) */
	int		rxstart, rxstop;
	int		rxnext;		/* next page to read */
	int		txlen[NU_NTX];	/* 0 free, else queued length */
	int		txcur;		/* buffer on the wire, -1 none */
	int		txfill;		/* next buffer to fill */
	int		txage;		/* polls the current frame has taken */
	int		rtl;		/* RTL8019 ID seen */
	unsigned char	ea[6];
	unsigned char	prom[6];
	unsigned char	rcr;		/* RCR_AB, RCR_AM, RCR_PRO */
	unsigned char	imr;		/* interrupts enabled while running */
	unsigned char	crcprobe;	/* the counts may leave out the CRC */
	unsigned char	nocrc;		/* they do */
	unsigned char	mar[8];
	int		nmc;
	unsigned char	mc[NU_NMC][6];
	int		mcref[NU_NMC];
	int		running;
	int		failrun;	/* failed inits in a row */
	unsigned char	cfg[3];		/* CONFIG1-3 as first probed (RTL8019AS) */
	unsigned char	cfgok;		/* cfg holds them */
	/* the last init read-back that differed: page << 8 | reg, want, got */
	int		badreg, badwant, badgot;
	unsigned char	txbuf[NU_MAXFRAME];
	unsigned char	rxbuf[NU_MAXFRAME + 4];
	struct nu_stats	st;
};

int	nu_probe();		/* (sc) 0 found, -1 none */
int	nu_init();		/* (sc) 0 or -1 */
int	nu_recover();		/* (sc) probe again, CONFIG1-3 back, init: 0 or -1 */
void	nu_stop();		/* (sc) */
int	nu_txroom();		/* (sc) */
void	nu_txstart();		/* (sc, len) send sc->txbuf */
int	nu_poll();		/* (sc) once a tick; 1 if any work was done */
int	nu_filter();		/* (sc) load station address, RCR and hash */
int	nu_addmc();		/* (sc, addr) 0, or -1 when full */
int	nu_delmc();		/* (sc, addr) 0, or -1 when absent */

/* supplied by the caller */
void	nu_input();		/* (sc, frame, len without CRC) */
void	nu_txdone();		/* (sc) a transmit buffer became free */

#ifdef NU_ASV
/*
 * The Atari System V kernel (nu.c), built with GCC 2.7.2 (sp1 kbuild.sh):
 * the cartridge port through the kernel's mapping of the 0xFF block, as
 * the MFP and the ACIAs are reached.  All chip work runs below the clock
 * and keyboard (ipl 6), so neither needs looking after.
 */
extern volatile unsigned char nu_sink;
/*
 * Every access goes through nu_rd / nu_wr (nu.c), a call each: inline
 * writes left the chip without its setup on the TT, in the kernel and in
 * nuuser -DNU_INLINE alike (why is not known), and how the card is reached
 * - which address block, a pause before each access, the data cache off -
 * can be changed while running (NUIOC_MODE, nustat -m) to find what works.
 */
int		nu_rd();	/* (reg) */
void		nu_wr();	/* (reg, value) */
#define NU_RD(r)	nu_rd(r)
#define NU_WR(r, v)	nu_wr(r, (v) & 0xff)
void		nu_spin();	/* (microseconds) */
#define NU_DELAY(n)	nu_spin(n)
#define NU_YIELD()
#define NU_CLKDUE()	0
#elif defined(NU_DEVMEM)
/*
 * A user program reaching the card through /dev/mem (nuuser.c).  With
 * NU_INLINE the accesses are inline loads from the mapping, as close
 * together as the kernel's.
 */
int		nu_devrd();	/* (reg) */
void		nu_devwr();	/* (reg, val) */
void		nu_devdelay();	/* (microseconds) */
#ifdef NU_INLINE
extern __volatile__ unsigned char *nu_win, nu_sink;	/* (the TT cc wants this spelling here) */
void		nu_pause();	/* before each access: nuuser -g */
int		nu_rdlog();	/* (reg, value read): logs it for nuuser -T */
int		nu_wrlog();	/* (reg, value): logs it, returns value & 0xff */
/* each argument once: callers pass *buf++.  NU_FNRD / NU_FNWR: that half
   through the functions instead (to bisect) */
#ifdef NU_FNRD
#define NU_RD(r)	nu_devrd(r)
#else
#define NU_RD(r)	nu_rdlog(r, (nu_pause(), nu_win[(r) << NU_REGSHIFT]))
#endif
#ifdef NU_FNWR
#define NU_WR(r, v)	nu_devwr(r, (v) & 0xff)
#else
#define NU_WR(r, v)	(nu_pause(), nu_sink = nu_win[0x10000 + \
			    ((r) << NU_REGSHIFT) + (nu_wrlog(r, v) << 1)])
#endif
#else
#define NU_RD(r)	nu_devrd(r)
#define NU_WR(r, v)	nu_devwr(r, (v) & 0xff)
#endif
#define NU_DELAY(n)	nu_devdelay(n)
#define NU_YIELD()
#define NU_CLKDUE()	0
#elif defined(NU_HOST)
unsigned char	sim_bus();
#define NU_RD(r)	sim_bus((unsigned long)NU_RDBASE + ((r) << NU_REGSHIFT))
#define NU_WR(r, v)	((void)sim_bus((unsigned long)NU_WRBASE + \
			    ((r) << NU_REGSHIFT) + (((v) & 0xff) << 1)))
#define NU_DELAY(n)
#define NU_YIELD()
extern int sim_clkdue;
#define NU_CLKDUE()	sim_clkdue
#elif defined(NU_PCI)
/* an NE2000 in PCI I/O space, byte-wide, with its interrupt */
extern __volatile__ unsigned char *nu_io;
#define NU_RD(r)	(nu_io[r])
#define NU_WR(r, v)	(nu_io[r] = (v))
extern int delayus();
#define NU_DELAY(n)	delayus(n)
#define NU_YIELD()
#define NU_CLKDUE()	0
#elif defined(NU_XSURF)
/*
 * The X-Surf 100's AX88796 in Zorro space: the 8390 registers sit 4 bytes
 * apart from board offset 0x800, the data port is 16 bits wide, the
 * board's interrupt-pending word is at offset 0x40 (bits 0xAAAA).
 */
extern __volatile__ unsigned char *nu_io;
#define NU_RD(r)	(nu_io[(r) << 2])
#define NU_WR(r, v)	(nu_io[(r) << 2] = (v))
#define NU_RD16()	(*(__volatile__ unsigned short *)(nu_io + (0x10 << 2)))
#define NU_WR16(v)	(*(__volatile__ unsigned short *)(nu_io + (0x10 << 2)) = (v))
#define NU_WORDS	1
extern int delayus();
#define NU_DELAY(n)	delayus(n)
#define NU_YIELD()
#define NU_CLKDUE()	0
#else
#define VOL		__volatile__
#define NU_RD(r)	(*(VOL unsigned char *)(NU_RDBASE + ((r) << NU_REGSHIFT)))
/* the read is the write: it must reach the bus */
#define NU_WR(r, v)	__asm__ __volatile__("tstb %0@" : : "a" \
			    (NU_WRBASE + ((r) << NU_REGSHIFT) + (((v) & 0xff) << 1)) \
			    : "memory")
extern int delayus();
#define NU_DELAY(n)	delayus(n)
/*
 * Copies run at IPL 6 and take up to 2 ms a frame; the keyboard ACIA
 * holds one byte (1.3 ms at 7812 baud), so drain it between chunks.
 */
extern void ikbd_intr();
#define NU_YIELD()	do { if (*(VOL unsigned char *)0xFFFFFC00 & 0x80) \
				ikbd_intr(); } while (0)
/* the 240 Hz clock is pending: holding it off one more period loses a tick */
#define NU_CLKDUE()	(*(VOL unsigned char *)0xFFFFFA0B & 0x20)
#endif

#endif
