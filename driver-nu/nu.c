/*
 * nu - the NetUSBee's Ethernet (an RTL8019AS on the cartridge port) for
 * Atari System V: a DLPI STREAMS driver that the stock TCP/IP links under
 * IP and ARP (strcf: `cenet ip /dev/nu nu 0`, the interface nu0).
 *
 * The chip core is Ash Nazag's nuchip.c (MIT, LICENSE.ashnazag), compiled
 * in below with NU_ASV.  The DLPI half is dp.c's (driver-dp), itself a
 * re-creation of the generic "engen" half of the stock /boot/LA.
 *
 * No interrupt line reaches the cartridge port, so the chip is polled: a
 * callout every tick qenable()s the read queue of one open minor, and that
 * queue's service procedure (nursrv) does all the chip work - receive,
 * transmit, reclaim.  Nothing else touches the chip once it runs: the put
 * procedure only queues a frame and qenable()s the same queue, so a frame
 * sent from a TCP timer never lands in the middle of a buffer copy.  A
 * register access takes about 2 us, so a full frame copies in about 3 ms.
 *
 * Learned on the TT: the RTL8019AS ignores what comes for a while after a
 * reset (nuchip.c waits 20 ms); a load whose value is thrown away is
 * dropped by the TT's own cc, so the chip core's writes store theirs.
 *
 *	sh ../kbuild.sh driver-nu/nu.c	(GCC 2.7.2 on the PC; see install.sh)
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/cred.h>
#include <sys/cmn_err.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/dlpi.h>
#include <sys/socket.h>
#include <net/if.h>

#define NU_ASV
#include "nu.h"
#include "nustat.h"

/* netinet/in.h is NOT included: under _KERNEL it declares an array of the
 * incomplete struct protosw. All we need from if_ether.h is this: */
typedef unsigned char ether_addr_t[6];

extern clock_t lbolt;

int nudevflag = 0;
volatile unsigned char nu_sink;

static struct nu_softc nu_sc;
static int nu_up;			/* found and set going: polled */
static int nu_tid;			/* timeout id */
static queue_t *nu_pollq;		/* the read queue whose service polls */
static clock_t nu_retry;		/* lbolt for the next re-init of a dead chip */
static unsigned long nu_txdrop;		/* frames too short or too long to send */

#define NU_NMINOR	8

typedef struct {
	queue_t	*min_rdq;
	int	min_state;
	u_long	min_sap;
} enminor_t;

typedef struct {			/* LA's en_info_t, same offsets */
	unsigned char	if_flags;
	ether_addr_t	if_enaddr;		/* +1 */
	struct ifstats	if_stats;		/* +8 */
	unsigned int	if_wqcnt;		/* +48 */
	unsigned int	if_nextmin;		/* +52 */
} en_info_t;
#define ENF_RUNNING	1

static enminor_t en_min[NU_NMINOR];
static en_info_t en_if;
static unsigned char nu_bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

extern struct ifstats *ifstats;
static void en_rproc();

#include "nuchip.c"

/* ------------------------------------------------------------- chip half */

/*
 * NU_DELAY: at least us microseconds of MFP reads (ST-bus cycles), as many
 * a millisecond as nu_calib counted; before that, 4 a microsecond (an MFP
 * read takes longer than that, so the wait only comes out long).
 */
static long nu_perms;			/* MFP reads a millisecond */

void
nu_spin(us)
int us;
{
	register long n;

	n = nu_perms ? (us * nu_perms + 999) / 1000 : us * 4L;
	for (; n > 0; n--)
		nu_sink = *(volatile unsigned char *)0xFFFFFA01;
}

/*
 * Count MFP reads across two clock ticks.  Process context (first open);
 * should the clock not advance there, the bound ends it and nu_perms
 * stays 0.  The counting loop also reads lbolt, so nu_spin's loop is the
 * faster one: a quarter is added.
 */
static void
nu_calib()
{
	register volatile clock_t *lb = (volatile clock_t *)&lbolt;
	register clock_t t;
	register long n, bound = 4000000L;

	t = *lb;
	while (*lb == t)
		if (--bound <= 0)
			return;
	t = *lb;
	for (n = 0; *lb - t < 2; n++) {
		nu_sink = *(volatile unsigned char *)0xFFFFFA01;
		if (--bound <= 0)
			return;
	}
	nu_perms = n * HZ / 2000 + n * HZ / 8000;
}

/* how the card is reached (NUIOC_MODE; struct nu_mode in nustat.h) */
static unsigned long nu_rbase = NU_RDBASE, nu_wbase = NU_WRBASE;
static int nu_dly;			/* spin turns before each access */
static int nu_nocache;			/* data cache off around chip work */
static volatile int nu_spinv;

/*
 * The NetUSBee samples the register address (A9-A13) and the written value
 * (A1-A8) while the ST-bus address lines are still settling: a line that
 * the preceding ST-bus cycle drove high can still read high (seen: kernel
 * writes to CR landing in DCR, DCR 48 read back as 4c).  Kernel code and
 * data are in ST-RAM, so the preceding cycle is a load of nu_wbase or a
 * stack push; a user program's data is in TT-RAM, which leaves the lines
 * as the last card access set them, which is why nuuser works.  The pad:
 * a 16 KB aligned block in ST-RAM.  A store to pad[offset] right before
 * (and after) the card access puts the very same A1-A13 on the bus, so no
 * line has to change between the two cycles.  nu_padmode: 1 before, 2
 * after, 4 the same for reads.
 */
static unsigned char nu_padraw[0x8000];
static volatile unsigned char *nu_pad;
static int nu_padmode = 15;		/* 8: nu_acc, the instruction cache frozen */
#define NU_BARRIER()	asm volatile ("" : : : "memory")
static unsigned char nu_acc();
extern int spl7();

static void
nu_padinit()
{
	if (nu_pad == (volatile unsigned char *)0)
		nu_pad = (volatile unsigned char *)
		    (((unsigned long)nu_padraw + 0x3fff) & ~0x3fffUL);
}

/* NU_RD: register r */
int
nu_rd(r)
register int r;
{
	register int i;

	register unsigned long o = (unsigned long)r << NU_REGSHIFT;
	register volatile unsigned char *a = (volatile unsigned char *)(nu_rbase + o);
	register volatile unsigned char *p = nu_pad + o;
	register unsigned char t;
	register int padrd = nu_padmode & 4;

	for (i = nu_dly; i > 0; i--)
		nu_spinv++;
	if (nu_padmode & 8)
		return nu_acc(a, p);
	if (padrd) {
		NU_BARRIER();
		*p = 0;
		NU_BARRIER();
		t = *a;
		NU_BARRIER();
		*p = t;
		NU_BARRIER();
		return t;
	}
	return *a;
}

/* NU_WR: the read in the write block that writes v to register r */
void
nu_wr(r, v)
register int r, v;
{
	register int i;

	register unsigned long o = ((unsigned long)r << NU_REGSHIFT) +
	    ((unsigned long)v << 1);
	register volatile unsigned char *a = (volatile unsigned char *)(nu_wbase + o);
	register volatile unsigned char *p = nu_pad + o;
	register unsigned char t;
	register int pre = nu_padmode & 1, post = nu_padmode & 2;

	for (i = nu_dly; i > 0; i--)
		nu_spinv++;
	if (nu_padmode & 8) {
		(void)nu_acc(a, p);
		return;
	}
	/* nothing but the pad store may touch memory next to the card access */
	NU_BARRIER();
	if (pre)
		*p = 0;
	NU_BARRIER();
	t = *a;
	NU_BARRIER();
	if (post)
		*p = t;
	else
		nu_sink = t;
	NU_BARRIER();
}

/*
 * With nocache: the data cache off (CACR ED) and emptied (CD) while the
 * chip is worked, so the cached TT0 block reaches the card a load at a
 * time; then as it was, emptied again.  0 when nocache is off.
 */
/* movec as words: the build's assembler pass turns the mnemonic into mov */
static unsigned long
nu_getcacr()
{
	register unsigned long c asm("d0");

	asm volatile (".short 0x4e7a,0x0002" : "=r" (c));	/* movec %cacr,%d0 */
	return c;
}

static void
nu_setcacr(v)
unsigned long v;
{
	register unsigned long c asm("d0") = v;

	asm volatile (".short 0x4e7b,0x0002" : : "r" (c));	/* movec %d0,%cacr */
}

/*
 * One card access at a, pad stores at p around it, with no instruction
 * fetch on the ST bus next to the card cycle: the kernel's code is in
 * ST-RAM, and the 68030's prefetch of the next instructions puts their
 * address on the lines the NetUSBee samples (seen: register-0 writes
 * landing in register 0xe, A10-A12 of the code address).  The loop body
 * runs twice: first on the pad alone, which brings its code into the
 * instruction cache, then frozen (CACR FI) on the card, every fetch a
 * cache hit.  The body touches memory only at p and x.
 */
static unsigned char
nu_acc(a, p)
volatile unsigned char *a, *p;
{
	register unsigned long c = nu_getcacr();
	register unsigned long fi = c | 0x2;		/* CACR FI set */
	register unsigned long t;
	int s = spl7();		/* an interrupt between the passes would
				   evict the loop before it is frozen */

	/* in assembly, so that both passes run the very same instructions
	   (the compiler unrolled the C loop into two copies) */
	asm volatile (
	    "\tmovel %2,%%d1\n"		/* the frozen CACR, ready */
	    "\tmoveal %3,%%a1\n"		/* x = p: the dry pass */
	    "1:\n"
	    "\tclrb %3@\n"			/* *p = 0 */
	    "\tmoveb %%a1@,%0\n"		/* t = *x */
	    "\tmoveb %0,%3@\n"		/* *p = t */
	    "\tnop\n"
	    "\tnop\n"
	    "\tcmpal %4,%%a1\n"
	    "\tbeqs 2f\n"
	    "\t.short 0x4e7b,0x1002\n"	/* movec %d1,%cacr: FI */
	    "\tmoveal %4,%%a1\n"		/* x = a: the real one */
	    "\tbras 1b\n"
	    "2:\n"
	    : "=d" (t) : "0" (0), "d" (fi), "a" (p), "a" (a) : "d1", "a1", "memory");
	nu_setcacr(c);
	splx(s);
	return t & 0xff;
}

static unsigned long
nu_dcoff()
{
	unsigned long c;

	if (!nu_nocache)
		return 0;
	c = nu_getcacr();
	nu_setcacr((c & ~0x100) | 0x800);
	return c;
}

static void
nu_dcon(c)
unsigned long c;
{
	if (!nu_nocache)
		return;
	nu_setcacr(c | 0x800);
}

/* a frame from the chip, without its CRC */
void
nu_input(sc, f, len)
struct nu_softc *sc;
unsigned char *f;
int len;
{
	mblk_t *mp;

	if (len < 14)
		return;
	if ((mp = allocb(len, BPRI_MED)) == (mblk_t *)0) {
		sc->st.nomem++;
		return;
	}
	bcopy((caddr_t)f, (caddr_t)mp->b_wptr, len);
	mp->b_wptr += len;
	en_rproc(mp);
}

/* a transmit buffer came free: nursrv sends what waits */
void
nu_txdone(sc)
struct nu_softc *sc;
{
}

static void
nu_xmit(mp)
mblk_t *mp;
{
	mblk_t *bp;
	int n, len;

	len = 0;
	for (bp = mp; bp != (mblk_t *)0; bp = bp->b_cont) {
		n = bp->b_wptr - bp->b_rptr;
		if (n <= 0)
			continue;
		if (len + n > NU_MAXFRAME) {
			len = -1;
			break;
		}
		bcopy((caddr_t)bp->b_rptr, (caddr_t)(nu_sc.txbuf + len), n);
		len += n;
	}
	freemsg(mp);
	if (len < 14) {
		nu_txdrop++;
		return;
	}
	nu_txstart(&nu_sc, len);
}

/* send what the minors queued, round robin, while the chip has room */
static int
nu_send()
{
	queue_t *wq;
	mblk_t *mp;
	int s, tries, sent;

	sent = 0;
	if (!nu_sc.running)		/* frames wait for the re-init */
		return 0;
	s = splstr();
	for (tries = 0; en_if.if_wqcnt && tries < 2 * NU_NMINOR; tries++) {
		if (++en_if.if_nextmin >= NU_NMINOR)
			en_if.if_nextmin = 0;
		if (en_min[en_if.if_nextmin].min_rdq == (queue_t *)0)
			continue;
		wq = WR(en_min[en_if.if_nextmin].min_rdq);
		if (wq->q_first == (mblk_t *)0)
			continue;
		if (!nu_txroom(&nu_sc))
			break;
		if ((mp = getq(wq)) == (mblk_t *)0)
			continue;
		en_if.if_wqcnt--;
		splx(s);
		nu_xmit(mp);
		sent = 1;
		s = splstr();
	}
	splx(s);
	return sent;
}

static void
nu_ifstats()
{
	struct nu_stats *st = &nu_sc.st;

	en_if.if_stats.ifs_ipackets = st->ipackets;
	en_if.if_stats.ifs_ierrors = st->ierrors + st->rxbad + st->ovw +
	    st->nomem;
	en_if.if_stats.ifs_opackets = st->opackets;
	en_if.if_stats.ifs_oerrors = st->oerrors + st->txtimeout + nu_txdrop;
	en_if.if_stats.ifs_collisions = st->collisions;
}

/* callout: the work itself is nursrv's, outside interrupt context */
static void
nu_tick()
{
	if (nu_up == 0)
		return;
	if (nu_pollq != (queue_t *)0)
		qenable(nu_pollq);
	nu_tid = timeout(nu_tick, (caddr_t)0, 1);
}

/*
 * An init that failed: a chip stuck in reset, or one that did not keep
 * what it was given (nu_verify).  Said once; nursrv tries again each
 * second, and nustat shows the count.
 */
static void
nu_initfail()
{
	static int said;

	if (said++)
		return;
	if (nu_sc.st.initbad)
		cmn_err(CE_CONT, "nu0: the chip did not keep its setup: page %d "
		    "register %x reads %x, not %x; retrying each second\n",
		    nu_sc.badreg >> 8, nu_sc.badreg & 0xff, nu_sc.badgot,
		    nu_sc.badwant);
	else
		cmn_err(CE_CONT, "nu0: the chip does not come out of reset; "
		    "retrying each second\n");
}

/* EN_LINIT: find the card and set it going. Process context (first open). */
static void
nu_linit()
{
	int i;
	unsigned long c;
	char mac[18];			/* xx:xx:xx:xx:xx:xx */

	if (nu_up)
		return;
	nu_calib();
	c = nu_dcoff();
	if (nu_probe(&nu_sc) < 0) {
		nu_dcon(c);
		cmn_err(CE_CONT, "nu0: no NetUSBee on the cartridge port\n");
		return;
	}
	for (i = 0; i < 6; i++)
		en_if.if_enaddr[i] = nu_sc.ea[i] = nu_sc.prom[i];
	nu_sc.rcr = RCR_AB;
	nu_sc.imr = 0;
	if (nu_init(&nu_sc) < 0)
		nu_initfail();
	nu_dcon(c);
	/* two digits a byte: the kernel's cmn_err has no field widths */
	for (i = 0; i < 6; i++) {
		mac[3 * i] = "0123456789abcdef"[(en_if.if_enaddr[i] >> 4) & 15];
		mac[3 * i + 1] = "0123456789abcdef"[en_if.if_enaddr[i] & 15];
		mac[3 * i + 2] = i < 5 ? ':' : '\0';
	}
	cmn_err(CE_CONT, "nu0: NetUSBee (%s), %dK buffer, %s, %d MFP reads/ms\n",
	    nu_sc.rtl ? "RTL8019AS" : "8390", (nu_sc.memhi - nu_sc.memlo) / 4,
	    mac, (int)nu_perms);
	en_if.if_flags |= ENF_RUNNING;
	nu_up = 1;
	nu_retry = lbolt;
	nu_tid = timeout(nu_tick, (caddr_t)0, 1);
}

/* ------------------------------------------------------------- DLPI half */

/* make mp's first block a fresh M_PCPROTO of `size` bytes, reusing it if it
 * is unshared and big enough (LA does the same), else allocating. */
static mblk_t *
en_reuse(mp, size)
mblk_t *mp;
int size;
{
	mblk_t *np;

	if (mp->b_datap->db_ref == 1 &&
	    (mp->b_datap->db_lim - mp->b_datap->db_base) >= size) {
		mp->b_rptr = mp->b_datap->db_base;
		mp->b_wptr = mp->b_rptr;
		if (mp->b_cont) {
			freemsg(mp->b_cont);
			mp->b_cont = (mblk_t *)0;
		}
		np = mp;
	} else {
		np = allocb(size, BPRI_MED);
		if (np == (mblk_t *)0)
			return (mblk_t *)0;
		freemsg(mp);
	}
	np->b_datap->db_type = M_PCPROTO;
	return np;
}

static void
en_error_ack(q, mp, prim, dlerr, unixerr)
queue_t *q;
mblk_t *mp;
long prim, dlerr, unixerr;
{
	mblk_t *np;
	dl_error_ack_t *ea;

	np = en_reuse(mp, sizeof(dl_error_ack_t));
	if (np == (mblk_t *)0) {
		freemsg(mp);
		return;
	}
	ea = (dl_error_ack_t *)np->b_wptr;
	ea->dl_primitive = DL_ERROR_ACK;
	ea->dl_error_primitive = prim;
	ea->dl_errno = dlerr;
	ea->dl_unix_errno = unixerr;
	np->b_wptr += sizeof(dl_error_ack_t);
	putnext(q, np);
}

static void
en_uderror_ind(q, mp, err)
queue_t *q;
mblk_t *mp;
long err;
{
	dl_uderror_ind_t *ue;

	mp->b_rptr = mp->b_datap->db_base;
	ue = (dl_uderror_ind_t *)mp->b_rptr;
	bzero((caddr_t)ue, sizeof(dl_uderror_ind_t));
	ue->dl_primitive = DL_UDERROR_IND;
	ue->dl_errno = err;
	mp->b_wptr = mp->b_rptr + sizeof(dl_uderror_ind_t);
	mp->b_datap->db_type = M_PCPROTO;
	putnext(q, mp);
}

static void
en_send_up(q, mp)
queue_t *q;
mblk_t *mp;
{
	int s;

	if (canput(q->q_next)) {
		s = splstr();
		putnext(q, mp);
		splx(s);
	} else
		freemsg(mp);
}

/* prepend a DL_UNITDATA_IND; dst/src point at 6-byte addresses */
static mblk_t *
en_mkind(data, dst, src)
mblk_t *data;
unsigned char *dst, *src;
{
	mblk_t *ip;
	dl_unitdata_ind_t *ui;

	ip = allocb(36, BPRI_HI);
	if (ip == (mblk_t *)0)
		return (mblk_t *)0;
	ui = (dl_unitdata_ind_t *)ip->b_wptr;
	ui->dl_primitive = DL_UNITDATA_IND;
	ui->dl_dest_addr_length = 6;
	ui->dl_dest_addr_offset = 24;
	ui->dl_src_addr_length = 6;
	ui->dl_src_addr_offset = 30;
	ui->dl_reserved = 0;
	bcopy((caddr_t)dst, (caddr_t)(ip->b_wptr + 24), 6);
	bcopy((caddr_t)src, (caddr_t)(ip->b_wptr + 30), 6);
	ip->b_wptr += 36;
	ip->b_datap->db_type = M_PROTO;
	ip->b_cont = data;
	return ip;
}

/* a received frame: route by ethertype to the minor bound to it; frames
 * nobody is bound to (IPv6, ...) are dropped quietly */
static void
en_rproc(mp)
mblk_t *mp;
{
	unsigned char *f;
	u_long type;
	int i;
	mblk_t *ip;

	f = mp->b_rptr;
	type = (f[12] << 8) | f[13];
	for (i = 0; i < NU_NMINOR; i++)
		if (en_min[i].min_sap == type && en_min[i].min_state == DL_IDLE)
			break;
	if (i == NU_NMINOR || en_min[i].min_rdq == (queue_t *)0) {
		freemsg(mp);
		return;
	}
	ip = en_mkind(mp, f, f + 6);
	if (ip == (mblk_t *)0) {
		freemsg(mp);
		return;
	}
	mp->b_rptr += 14;
	en_send_up(en_min[i].min_rdq, ip);
}

static void
en_proto(q, mp)
queue_t *q;
mblk_t *mp;
{
	enminor_t *min;
	union DL_primitives *p;
	mblk_t *np, *cp, *hp;
	unsigned char *dst, hdr[14];
	u_long sap;
	int i, s;

	min = (enminor_t *)q->q_ptr;
	p = (union DL_primitives *)mp->b_rptr;
	if ((en_if.if_flags & ENF_RUNNING) == 0) {
		en_error_ack(min->min_rdq, mp, (long)p->dl_primitive, (long)DL_NOTINIT, 0L);
		return;
	}
	switch (p->dl_primitive) {
	case DL_BIND_REQ:
		if (min->min_state != DL_UNBOUND) {
			en_error_ack(min->min_rdq, mp, (long)DL_BIND_REQ, (long)DL_OUTSTATE, 0L);
			return;
		}
		sap = p->bind_req.dl_sap;
		for (i = 0; i < NU_NMINOR; i++)
			if (en_min[i].min_state == DL_IDLE && en_min[i].min_sap == sap) {
				en_error_ack(min->min_rdq, mp, (long)DL_BIND_REQ, 0L, 0L);
				return;
			}
		min->min_state = DL_IDLE;
		min->min_sap = sap;
		np = en_reuse(mp, 30);
		if (np == (mblk_t *)0) {
			en_error_ack(min->min_rdq, mp, (long)DL_BIND_REQ, (long)DL_SYSERR, (long)ENOMEM);
			return;
		}
		{
			dl_bind_ack_t *ba = (dl_bind_ack_t *)np->b_wptr;
			ba->dl_primitive = DL_BIND_ACK;
			ba->dl_sap = sap;
			ba->dl_addr_length = 6;
			ba->dl_addr_offset = 24;
			ba->dl_max_conind = 0;
			ba->dl_growth = 0;
		}
		bcopy((caddr_t)en_if.if_enaddr, (caddr_t)(np->b_wptr + 24), 6);
		np->b_wptr += 30;
		putnext(min->min_rdq, np);
		return;

	case DL_UNBIND_REQ:
		if (min->min_state != DL_IDLE) {
			en_error_ack(min->min_rdq, mp, (long)DL_UNBIND_REQ, (long)DL_OUTSTATE, 0L);
			return;
		}
		min->min_state = DL_UNBOUND;
		min->min_sap = 0;
		np = en_reuse(mp, 8);
		if (np == (mblk_t *)0) {
			en_error_ack(min->min_rdq, mp, (long)DL_UNBIND_REQ, (long)DL_SYSERR, (long)ENOMEM);
			return;
		}
		{
			dl_ok_ack_t *oa = (dl_ok_ack_t *)np->b_wptr;
			oa->dl_primitive = DL_OK_ACK;
			oa->dl_correct_primitive = DL_UNBIND_REQ;
		}
		np->b_wptr += 8;
		putnext(min->min_rdq, np);
		return;

	case DL_INFO_REQ:
		np = en_reuse(mp, 70);
		if (np == (mblk_t *)0) {
			en_error_ack(min->min_rdq, mp, (long)DL_INFO_REQ, (long)DL_SYSERR, (long)ENOMEM);
			return;
		}
		{
			dl_info_ack_t *ia = (dl_info_ack_t *)np->b_wptr;
			bzero((caddr_t)ia, 64);
			ia->dl_primitive = DL_INFO_ACK;
			ia->dl_max_sdu = 1500;
			ia->dl_min_sdu = 1;
			ia->dl_mac_type = DL_ETHER;
			ia->dl_current_state = min->min_state;
			ia->dl_max_idu = 1500;
			ia->dl_service_mode = DL_CLDLS;
			ia->dl_provider_style = DL_STYLE1;
			ia->dl_addr_offset = 64;
			np->b_wptr += 64;
			if (min->min_state != DL_UNBOUND) {
				ia->dl_addr_length = 6;
				bcopy((caddr_t)en_if.if_enaddr, (caddr_t)np->b_wptr, 6);
				np->b_wptr += 6;
			}
		}
		putnext(min->min_rdq, np);
		return;

	case DL_UNITDATA_REQ:
		if (min->min_state != DL_IDLE) {
			en_uderror_ind(min->min_rdq, mp, (long)DL_OUTSTATE);
			return;
		}
		dst = mp->b_rptr + p->unitdata_req.dl_dest_addr_offset;
		if (bcmp((caddr_t)nu_bcast, (caddr_t)dst, 6) == 0) {
			/* broadcasts are also heard locally, as LA does */
			cp = copymsg(mp);
			if (cp != (mblk_t *)0 && cp->b_cont != (mblk_t *)0) {
				np = en_mkind(cp->b_cont, dst, en_if.if_enaddr);
				if (np != (mblk_t *)0) {
					freeb(cp);
					en_send_up(min->min_rdq, np);
				} else
					freemsg(cp);
			} else if (cp != (mblk_t *)0)
				freemsg(cp);
		}
		bcopy((caddr_t)dst, (caddr_t)hdr, 6);
		bcopy((caddr_t)en_if.if_enaddr, (caddr_t)(hdr + 6), 6);
		hdr[12] = (unsigned char)(min->min_sap >> 8);
		hdr[13] = (unsigned char)min->min_sap;
		hp = allocb(14, BPRI_MED);
		if (hp == (mblk_t *)0) {
			en_uderror_ind(min->min_rdq, mp, (long)DL_UNDELIVERABLE);
			return;
		}
		bcopy((caddr_t)hdr, (caddr_t)hp->b_wptr, 14);
		hp->b_wptr += 14;
		hp->b_cont = mp->b_cont;
		mp->b_cont = (mblk_t *)0;
		freeb(mp);
		/* only queued here: nursrv puts it on the wire */
		s = splstr();
		putq(q, hp);
		en_if.if_wqcnt++;
		splx(s);
		if (nu_pollq != (queue_t *)0)
			qenable(nu_pollq);
		return;

	default:
		freemsg(mp);
		return;
	}
}

#define ENIOC_GETADDR	(('E' << 8) | 1)

/*
 * NUIOC_REGS: pages 0-2 as the kernel reads them, for nustat -r.  Page 0's
 * tally counters clear when read; the chip keeps running or stopped as it
 * was (TXP is not written back).  The service procedure does not run in
 * the middle of this put procedure.
 */
static void
nu_snap(r)
register struct nu_regs *r;
{
	register int p, i, cr, base;
	int s;
	unsigned long c;

	bzero((caddr_t)r, sizeof *r);
	r->running = nu_sc.running;
	r->badreg = nu_sc.badreg;
	r->badwant = nu_sc.badwant;
	r->badgot = nu_sc.badgot;
	r->perms = nu_perms;
	s = splstr();
	c = nu_dcoff();
	cr = NU_RD(NU_CR);
	base = (cr & (CR_STA | CR_STP)) | CR_NODMA;
	for (p = 0; p < 3; p++) {
		NU_WR(NU_CR, base | (p << 6));
		r->page[p][0] = NU_RD(NU_CR);
		for (i = 1; i < 16; i++)
			r->page[p][i] = NU_RD(i);
	}
	NU_WR(NU_CR, base);
	nu_dcon(c);
	r->cr = cr;
	splx(s);
}

/* NUIOC_MODE: set (when m->set) and answer how the card is reached */
static int
nu_setmode(m)
register struct nu_mode *m;
{
	if (m->set) {
		if (!((m->rbase == 0xFFFA0000 && m->wbase == 0xFFFB0000) ||
		    (m->rbase == 0x00FA0000 && m->wbase == 0x00FB0000 &&
		    m->nocache)) || m->dly < 0 || m->dly > 1000 ||
		    m->pad < 0 || m->pad > 15)
			return EINVAL;
		nu_rbase = m->rbase;
		nu_wbase = m->wbase;
		nu_dly = m->dly;
		nu_nocache = m->nocache != 0;
		nu_padmode = m->pad;
	}
	m->rbase = nu_rbase;
	m->wbase = nu_wbase;
	m->dly = nu_dly;
	m->nocache = nu_nocache;
	m->pad = nu_padmode;
	m->set = 0;
	return 0;
}

/*
 * NUIOC_TEST: probe and init the chip now, in the current mode; what came
 * of it and the registers afterwards.  A chip that inits runs from here
 * on; one that does not is tried again each second (nursrv).
 */
static void
nu_test(t)
register struct nu_test *t;
{
	unsigned long c;
	int s;

	bzero((caddr_t)t, sizeof *t);
	s = splstr();
	nu_sc.running = 0;
	c = nu_dcoff();
	t->probe = nu_probe(&nu_sc);
	if (t->probe == 0 && nu_up == 0) {	/* as nu_linit would set it up */
		bcopy((caddr_t)nu_sc.prom, (caddr_t)nu_sc.ea, 6);
		nu_sc.rcr = RCR_AB;
		nu_sc.imr = 0;
	}
	t->init = t->probe < 0 ? -1 : nu_init(&nu_sc);
	nu_dcon(c);
	t->badreg = nu_sc.badreg;
	t->badwant = nu_sc.badwant;
	t->badgot = nu_sc.badgot;
	t->initbad = nu_sc.st.initbad;
	t->crmiss = nu_crmiss;
	nu_retry = lbolt + HZ;
	splx(s);
	nu_snap(&t->regs);
}

/*
 * NUIOC_RAW: the accesses asked for, back to back at splstr in the current
 * mode, the reads' values written back.  Independent of probe and init:
 * this is how the kernel's own bus cycles are looked at from a program.
 */
static struct nu_raw nu_rawbuf;

static int
nu_raw(rw)
register struct nu_raw *rw;
{
	register struct nu_rawop *o;
	register int i;
	unsigned long c;
	int s;

	if (rw->n < 0 || rw->n > NU_RAWMAX)
		return EINVAL;
	for (i = 0; i < rw->n; i++)
		if (rw->a[i].op > NU_RAW_SPIN || rw->a[i].reg > 0x1f)
			return EINVAL;
	s = splstr();
	c = nu_dcoff();
	for (i = 0, o = rw->a; i < rw->n; i++, o++) {
		if (o->op == NU_RAW_RD)
			o->val = nu_rd(o->reg);
		else if (o->op == NU_RAW_WR)
			nu_wr(o->reg, o->val);
		else
			nu_spin(o->val * 10);
	}
	nu_dcon(c);
	splx(s);
	return 0;
}

static void
en_ioctl(q, mp)
queue_t *q;
mblk_t *mp;
{
	struct iocblk *ioc;
	mblk_t *dp;
	int n;

	ioc = (struct iocblk *)mp->b_rptr;
	mp->b_datap->db_type = M_IOCNAK;
	if ((mp->b_wptr - mp->b_rptr) < sizeof(struct iocblk) ||
	    ioc->ioc_count == TRANSPARENT) {
		qreply(q, mp);
		return;
	}
	switch (ioc->ioc_cmd) {
	case ENIOC_GETADDR:
		n = 6;
		break;
	case NUIOC_STATS:
		n = sizeof(struct nu_stats);
		break;
	case NUIOC_REGS:
		n = sizeof(struct nu_regs);
		break;
	case NUIOC_MODE:
	case NUIOC_RAW:
		n = ioc->ioc_cmd == NUIOC_MODE ? sizeof(struct nu_mode) :
		    sizeof(struct nu_raw);
		if (mp->b_cont == (mblk_t *)0 || ioc->ioc_count < n ||
		    (mp->b_cont->b_wptr - mp->b_cont->b_rptr) < n) {
			ioc->ioc_error = EINVAL;
			qreply(q, mp);
			return;
		}
		break;
	case NUIOC_TEST:
		n = sizeof(struct nu_test);
		break;
	default:
		qreply(q, mp);
		return;
	}
	if (ioc->ioc_cmd == NUIOC_MODE || ioc->ioc_cmd == NUIOC_RAW) {
		/* the answer goes where the question was */
		struct nu_mode m;
		int e;

		if (ioc->ioc_cmd == NUIOC_RAW) {
			bcopy((caddr_t)mp->b_cont->b_rptr, (caddr_t)&nu_rawbuf, n);
			e = nu_raw(&nu_rawbuf);
		} else {
			bcopy((caddr_t)mp->b_cont->b_rptr, (caddr_t)&m, n);
			e = nu_setmode(&m);
		}
		if (e != 0) {
			ioc->ioc_error = e;
			qreply(q, mp);
			return;
		}
		bcopy(ioc->ioc_cmd == NUIOC_RAW ? (caddr_t)&nu_rawbuf : (caddr_t)&m,
		    (caddr_t)mp->b_cont->b_rptr, n);
		mp->b_cont->b_wptr = mp->b_cont->b_rptr + n;
		ioc->ioc_count = n;
		ioc->ioc_error = 0;
		mp->b_datap->db_type = M_IOCACK;
		qreply(q, mp);
		return;
	}
	dp = mp->b_cont;
	if (dp == (mblk_t *)0 || (dp->b_datap->db_lim - dp->b_datap->db_base) < n) {
		if (dp)
			freemsg(dp);
		dp = allocb(n, BPRI_MED);
		mp->b_cont = dp;
	}
	if (dp == (mblk_t *)0) {
		ioc->ioc_error = ENOMEM;
		qreply(q, mp);
		return;
	}
	dp->b_rptr = dp->b_datap->db_base;
	if (ioc->ioc_cmd == ENIOC_GETADDR)
		bcopy((caddr_t)en_if.if_enaddr, (caddr_t)dp->b_rptr, 6);
	else if (ioc->ioc_cmd == NUIOC_REGS)
		nu_snap((struct nu_regs *)dp->b_rptr);
	else if (ioc->ioc_cmd == NUIOC_TEST)
		nu_test((struct nu_test *)dp->b_rptr);
	else
		bcopy((caddr_t)&nu_sc.st, (caddr_t)dp->b_rptr, n);
	dp->b_wptr = dp->b_rptr + n;
	ioc->ioc_count = n;
	ioc->ioc_error = 0;
	mp->b_datap->db_type = M_IOCACK;
	qreply(q, mp);
}

static int
nuwput(q, mp)
queue_t *q;
mblk_t *mp;
{
	switch (mp->b_datap->db_type) {
	case M_PROTO:
	case M_PCPROTO:
		en_proto(q, mp);
		break;
	case M_IOCTL:
		en_ioctl(q, mp);
		break;
	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW) {
			int s = splstr();
			mblk_t *fp;

			while ((fp = getq(q)) != (mblk_t *)0) {
				freemsg(fp);
				if (en_if.if_wqcnt)
					en_if.if_wqcnt--;
			}
			splx(s);
			*mp->b_rptr &= ~FLUSHW;
		}
		if (*mp->b_rptr & FLUSHR) {
			flushq(RD(q), FLUSHDATA);
			qreply(q, mp);
		} else
			freemsg(mp);
		break;
	default:
		freemsg(mp);
		break;
	}
	return 0;
}

/* read-side service procedure: all the chip's work, outside interrupt context */
static int
nursrv(q)
queue_t *q;
{
	int i, work;
	unsigned long c;

	if (nu_up == 0)
		return 0;
	c = nu_dcoff();
	if (!nu_sc.running && lbolt - nu_retry >= 0) {
		/* the chip stopped answering: try again once a second */
		nu_retry = lbolt + HZ;
		if (nu_init(&nu_sc) < 0)
			nu_initfail();
	}
	for (i = 0; i < 4; i++) {
		work = nu_poll(&nu_sc);
		work |= nu_send();
		if (!work)
			break;
	}
	nu_dcon(c);
	nu_ifstats();
	return 0;
}

static int
nuopen(q, devp, flag, sflag, crp)
queue_t *q;
dev_t *devp;
int flag, sflag;
struct cred *crp;
{
	int m;

	if (sflag == MODOPEN)
		return EINVAL;
	if (q->q_ptr)
		return 0;				/* already open */
	if (sflag == CLONEOPEN) {
		for (m = 0; m < NU_NMINOR; m++)
			if (en_min[m].min_rdq == (queue_t *)0)
				break;
		if (m == NU_NMINOR)
			return ENOSPC;
		*devp = makedevice(itoemajor(getmajor(*devp), -1), m);
	} else {
		m = getminor(*devp);
		if (m < 0 || m >= NU_NMINOR)
			return EINVAL;
	}
	/*
	 * The first open sets the card going.  Without one the open still
	 * succeeds (and DLPI answers DL_NOTINIT): a failed open would make
	 * slink at boot take down every interface, en0 too.
	 */
	nu_padinit();
	nu_linit();
	q->q_ptr = (caddr_t)&en_min[m];
	WR(q)->q_ptr = (caddr_t)&en_min[m];
	en_min[m].min_rdq = q;
	en_min[m].min_state = DL_UNBOUND;
	en_min[m].min_sap = 0;
	if (nu_pollq == (queue_t *)0)
		nu_pollq = q;
	return 0;
}

static int
nuclose(q, flag, crp)
queue_t *q;
int flag;
struct cred *crp;
{
	enminor_t *min;
	mblk_t *mp;
	int s, i;

	min = (enminor_t *)q->q_ptr;
	s = splstr();
	while ((mp = getq(WR(q))) != (mblk_t *)0) {	/* frames never sent */
		freemsg(mp);
		if (en_if.if_wqcnt)
			en_if.if_wqcnt--;
	}
	q->q_ptr = (caddr_t)0;
	WR(q)->q_ptr = (caddr_t)0;
	min->min_rdq = (queue_t *)0;
	min->min_state = DL_UNBOUND;
	min->min_sap = 0;
	if (nu_pollq == q) {		/* hand the poll to another open minor */
		nu_pollq = (queue_t *)0;
		for (i = 0; i < NU_NMINOR; i++)
			if (en_min[i].min_rdq != (queue_t *)0) {
				nu_pollq = en_min[i].min_rdq;
				break;
			}
	}
	splx(s);
	return 0;
}

static struct module_info nu_minfo = { 0x4e55, "nu", 1, 1500, 4800, 2000 };
static struct qinit nurinit = { 0, nursrv, nuopen, nuclose, 0, &nu_minfo, 0 };
static struct qinit nuwinit = { nuwput, 0, 0, 0, 0, &nu_minfo, 0 };
struct streamtab nuinfo = { &nurinit, &nuwinit, 0, 0 };

/* run once at boot: register the interface for netstat -i. The card is
 * not touched until the first open. */
void
nustart()
{
	en_if.if_stats.ifs_name = "nu";
	en_if.if_stats.ifs_unit = 0;
	en_if.if_stats.ifs_active = 1;
	en_if.if_stats.ifs_mtu = 1500;
	en_if.if_stats.ifs_next = ifstats;
	ifstats = &en_if.if_stats;
}
