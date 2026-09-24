/*
 * dp - DaynaPORT SCSI/Link driver for Atari System V.
 *
 * WORKING 2026-09-21: TCP/IP over the ZuluSCSI Blaster's DaynaPORT emulation
 * (telnet/ftp/ping both ways). This is the cleaned-up build: no debug logging.
 *
 * Stage 3 = a real network interface: a DLPI STREAMS driver that the stock
 * TCP/IP stack links under IP and ARP (slink's `cenet ip /dev/en en 0`).
 *
 * The DLPI half is a re-creation, function for function, of the generic
 * "engen" half inside the stock /boot/LA (enopen enclose enwput en_proto
 * en_wproc en_rproc en_ethr_to_ind en_req_to_ind en_send_up en_error_ack
 * en_uderror_ind en_set_ifstats + the 'E' ioctls), read from its disassembly,
 * so the stack sees exactly the primitives and sizes it was tested against.
 *
 * The SCSI half is asynchronous (STREAMS put procedures and timeouts cannot
 * sleep): ONE command in flight, a staged transmit frame has priority, and
 * receive is polled from timeout(). Interrupt-level rules on this kernel:
 *	- spl levels are ABSOLUTE (MFP masks), never "raise only";
 *	- SCSI completions arrive at spl4, STREAMS code runs at splstr = spl5,
 *	  so neither can preempt the other's critical sections that matter;
 *	- clock() runs callouts only when it interrupted IPL 0, but a callout
 *	  must NOT start SCSI work (measured: the command never completes);
 *	  it qenable()s a service procedure instead.
 *
 * Stage 2 (CONFIRMED on hardware 2026-09-21, both directions) = a RAW FRAME device, so the protocol can be exercised from user
 * space (dptest.c) without a kernel rebuild per experiment:
 *	open	find + claim the DaynaPORT, read the MAC, enable the interface
 *	write	one ethernet frame out (cmd 0x0A, ctl 0)
 *	read	one frame in, or 0 bytes if none is queued (cmd 0x08, ctl 0x80)
 *	ioctl	DPGETMAC copies out the 6-byte MAC
 *	close	disable the interface
 * Stage 1 (CONFIRMED on hardware 2026-09-21: MAC matched the ZuluSCSI log):
 * nothing happens at boot. Opening /dev/dp
 *	1. finds a processor-type SCSI target whose INQUIRY vendor is "Dayna",
 *	2. claims it (replaces the core's sj_badstart with dpSTART),
 *	3. sends cmd 0x09 (get MAC + stats, 18 bytes in) through the kernel's
 *	   SCSI job interface, exactly the way /boot/TP drives a tape,
 *	4. prints the result on the console.
 *
 * The job/queue protocol below is lifted from the disassembly of /boot/TP
 * (tpopen, tpstrategy, tpSTART) and sj_op in the kernel:
 *	- sj_op(bp, strategy, dev, blk, resid, addr, count, cmd, flags) fills a
 *	  private buf, calls strategy(bp) and biowait()s; returns B_ERROR or 0.
 *	- strategy queues the buf on sj_tab[job->sj_index] (72-byte entries:
 *	  head +12, tail +16, active +22, errcnt +23) and, if the queue was idle,
 *	  calls START(job, 1) at spl4.
 *	- START(job, 1) builds the CDB for the head buf and calls sj_jobentry().
 *	  On completion the CORE dequeues + biodone()s the buf, then calls
 *	  START(job, 1) for the next one (see the comment at dpSTART).
 *
 * sys/scsi.h and sys/iobuf.h are deliberately NOT included: scsi.h declares
 * arrays of incomplete types, and buf.h #defines b_actf/b_active over
 * iobuf.h's field names. The structures are restated here; their offsets are
 * checked against the TP disassembly (dtype 31, index 40, bytesdone 64,
 * flags 88, startf 144).
 *
 *	cc -O -D_KERNEL -c dp.c
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/cred.h>
#include <sys/cmn_err.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/dlpi.h>
#include <sys/socket.h>
#include <net/if.h>

/* netinet/in.h is NOT included: under _KERNEL it declares an array of the
 * incomplete struct protosw. All we need from if_ether.h is this: */
typedef unsigned char ether_addr_t[6];

#define MAXSENSE	20
#define INQSIZE		36
#define PROC		0x03		/* sj_dtype: processor device */
#define BERROR		0x02		/* sj_iflags: job in error */

struct scsijoblock {
	unsigned char	sj_cb[10];
	unsigned char	sj_cmdlength;
	unsigned char	sj_sdata[MAXSENSE];
	unsigned char	sj_dtype;		/* 31 */
	unsigned char	sj_dtypeq;
	unsigned char	sj_target;
	unsigned char	sj_lun;
	unsigned char	sj_status;
	struct buf	*sj_bp;			/* 36 */
	int		sj_index;		/* 40 */
	unsigned long	sj_blk;
	unsigned long	sj_blkcount;
	unsigned long	sj_jobcount;		/* 52 */
	unsigned long	sj_transcount;		/* 56 */
	unsigned long	sj_phasecount;
	unsigned long	sj_bytesdone;		/* 64 */
	unsigned char	*sj_jobp;		/* 68 */
	unsigned char	*sj_transp;		/* 72 */
	unsigned char	*sj_phasep;
	unsigned char	*sj_savep;
	unsigned long	sj_phase;
	unsigned long	sj_flags;		/* 88 */
	unsigned long	sj_iflags;		/* 92 */
	unsigned long	sj_pblock;
	unsigned long	sj_spc;
	unsigned long	sj_maxblock;
	unsigned char	sj_inqdata[INQSIZE];	/* 108 */
	void		(*sj_startf)();		/* 144 */
	char		*sj_pridata;
	struct scsijoblock *sj_jb;
};

struct dpqueue {			/* one sj_tab[] entry, 72 bytes */
	long		q_pad0[3];
	struct buf	*q_head;		/* 12 */
	struct buf	*q_tail;		/* 16 */
	short		q_dev;
	char		q_active;		/* 22 */
	char		q_errcnt;		/* 23 */
	long		q_pad1[12];
};

extern char sj_tab[];
extern int scsiinitdone;
extern clock_t lbolt;
extern void sj_badstart();
extern struct scsijoblock *sj_selectjob();

int dpdevflag = 0;

static struct scsijoblock *dpjob;
static struct buf dpbuf;
static unsigned char dpdata[64];
static unsigned char dptx[1600], dprx[3100];
static struct buf dpabuf;		/* the one ASYNC command */
static int dp_busy;			/* dpabuf is queued or in flight */
static int dp_up;			/* interface enabled, poll running */
static int dp_txlen;			/* a frame is staged in dptx */
static int dp_rxwant;			/* a receive poll is due */
static int dp_idle;			/* consecutive empty polls */
static int dp_reinit;			/* async re-bring-up: 2 = disable due, 1 = enable due */
static int dp_errs;			/* consecutive failed async commands */
static clock_t dp_settle;		/* lbolt until which errors are the post-enable window */

#define DP_SETTLE	(HZ / 2)	/* real hardware refuses data commands ~500 ms after ENABLE */
#define DP_MAXERRS	3		/* consecutive failures that mean the adapter reset */
static int dp_tid;			/* timeout id */

/*
 * Receive in multi-packet ("blind") mode, READ(6) ctl 0xC0: the adapter
 * returns up to two frames per command, each behind a 6-byte header whose
 * last byte has 0x10 set when another frame follows. (0x80, single-packet
 * mode, is for Macs whose VM pager must get at the bus between frames.)
 */
#define DP_RXASK	3072		/* room for two headers + full frames */
#define DP_RXMODE	0xC0
#define DP_RXMORE	0x10		/* header[5]: another frame follows */
#define DP_NMINOR	8
#define DPCMD(op, ctl)	(((op) << 8) | (ctl))	/* rides in b_blkno */

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

static enminor_t en_min[DP_NMINOR];
static en_info_t en_if;
static unsigned char dp_bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

extern struct ifstats *ifstats;
static void dp_async_done();
static void en_wproc();
static void en_rproc();

#define DPQ(job) ((struct dpqueue *)(sj_tab + 72 * (job)->sj_index))

/*
 * The driver's START callback. CORRECTED 2026-09-21 from sj_terminate's
 * disassembly - my first reading of the protocol had the flag backwards:
 *
 *	sj_terminate() ITSELF dequeues the finished buf (tab->b_actf =
 *	bp->av_forw), adjusts b_resid and calls biodone(bp). Only THEN does it
 *	call START(job, 1) = "start whatever is at the head now".
 *	START(job, 0) means "same buf again" (a retry, or a NOWAIT/multi-part
 *	continuation) - the head has NOT been dequeued.
 *
 * So START never finishes a buf. The async command's completion is seen here
 * as B_DONE on dpabuf (biodone sets it; there is no sleeper to wake).
 */
void
dpSTART(job, flag)
struct scsijoblock *job;
int flag;
{
	struct dpqueue *q;
	struct buf *bp;

	q = DPQ(job);
	if (dp_busy && (dpabuf.b_flags & B_DONE))
		dp_async_done(dpabuf.b_flags & B_ERROR);	/* may re-queue dpabuf */
	bp = q->q_head;
	if (bp == (struct buf *)0) {
		q->q_active = 0;
		return;
	}
	q->q_active = 1;
	job->sj_cb[0] = (unsigned char)(bp->b_blkno >> 8);
	job->sj_cb[1] = (unsigned char)((job->sj_lun & 7) << 5);
	job->sj_cb[2] = 0;
	job->sj_cb[3] = (unsigned char)(bp->b_bcount >> 8);
	job->sj_cb[4] = (unsigned char)bp->b_bcount;
	job->sj_cb[5] = (unsigned char)bp->b_blkno;
	job->sj_cmdlength = 6;
	job->sj_flags = 0;		/* PIO, as TP does for small data */
	job->sj_jobcount = bp->b_bcount;
	job->sj_transcount = bp->b_bcount;
	job->sj_jobp = (unsigned char *)vtop(bp->b_un.b_addr, bp->b_proc);
	job->sj_transp = job->sj_jobp;
	/*
	 * sj_terminate counts retries of the head buf in q_errcnt and fails it
	 * after five; zeroing it on a retry (flag 0) made a command the target
	 * keeps refusing retry forever - a real DaynaPORT, which answers CHECK
	 * CONDITION while disabled, hung the boot this way.
	 */
	if (flag)
		q->q_errcnt = 0;
	job->sj_bp = bp;
	sj_jobentry(job);
}

void
dpstrategy(bp)
struct buf *bp;
{
	struct dpqueue *q;
	int s;

	q = DPQ(dpjob);
	bp->b_resid = bp->b_bcount;
	s = spl4();
	if (q->q_head == (struct buf *)0)
		q->q_head = bp;
	else
		q->q_tail->av_forw = bp;
	q->q_tail = bp;
	bp->av_forw = (struct buf *)0;
	if (q->q_active == 0)
		dpSTART(dpjob, 1);
	splx(s);
}

/* ------------------------------------------------------------------ SCSI half */

/* one SYNCHRONOUS command (process context only: open / last close) */
static int
dpcmd(dev, op, ctl, addr, count, rw)
dev_t dev;
int op, ctl, count, rw;
unsigned char *addr;
{
	return sj_op(&dpbuf, dpstrategy, dev, DPCMD(op, ctl), 0, addr, count, 0, rw);
}

/*
 * Queue the next ASYNC command if one is due. `start` = also start the queue
 * when it is idle; dpSTART passes 0 because its own loop starts the head.
 * Callers are at spl4 (completion), splstr (STREAMS) or in a callout.
 */
static void
dp_kick(start)
int start;
{
	struct dpqueue *q;
	struct buf *bp;

	if (dp_busy || dp_up == 0)
		return;
	bp = &dpabuf;
	if (dp_reinit) {			/* disable, then enable (no data) */
		bp->b_flags = B_BUSY | B_READ;
		bp->b_blkno = DPCMD(0x0E, dp_reinit == 1 ? 0x80 : 0);
		bp->b_un.b_addr = (caddr_t)dpdata;
		bp->b_bcount = 0;
	} else if (dp_txlen) {
		bp->b_flags = B_BUSY;			/* write */
		bp->b_blkno = DPCMD(0x0A, 0);		/* one raw frame */
		bp->b_un.b_addr = (caddr_t)dptx;
		bp->b_bcount = dp_txlen;
	} else if (dp_rxwant) {
		bp->b_flags = B_BUSY | B_READ;
		bp->b_blkno = DPCMD(0x08, DP_RXMODE);
		bp->b_un.b_addr = (caddr_t)dprx;
		bp->b_bcount = DP_RXASK;
		dprx[0] = dprx[1] = 0;
	} else
		return;
	bp->b_proc = (struct proc *)0;
	bp->b_resid = bp->b_bcount;
	bp->av_forw = (struct buf *)0;
	dp_busy = 1;
	q = DPQ(dpjob);
	if (q->q_head == (struct buf *)0)
		q->q_head = bp;
	else
		q->q_tail->av_forw = bp;
	q->q_tail = bp;
	if (start && q->q_active == 0)
		dpSTART(dpjob, 1);
}

/* completion of the async command; called from dpSTART at spl4 */
static void
dp_async_done(err)
int err;
{
	int n;
	mblk_t *mp;

	dp_busy = 0;
	if ((dpabuf.b_blkno >> 8) == 0x0E) {		/* re-bring-up step */
		if (dp_reinit)
			dp_reinit--;
		if (dp_reinit == 0) {
			dp_settle = lbolt + DP_SETTLE;
			dp_errs = 0;
			cmn_err(CE_CONT, "dp0: interface reset\n");
		}
		dp_kick(0);
		return;
	}
	if (err && lbolt - dp_settle >= 0 && ++dp_errs >= DP_MAXERRS)
		dp_reinit = 2;		/* keeps failing: the adapter reset under us */
	else if (err == 0)
		dp_errs = 0;
	if ((dpabuf.b_blkno >> 8) == 0x0A) {		/* transmit finished */
		dp_txlen = 0;
		if (err)
			en_if.if_stats.ifs_oerrors++;
		else
			en_if.if_stats.ifs_opackets++;
		if (en_if.if_wqcnt)
			en_wproc();			/* stage the next frame */
		/*
		 * The answer to what was just sent (a TCP ack, an echo reply) is
		 * probably on its way: look for it now rather than at the next
		 * tick. A staged frame still goes first (dp_kick), so this reads
		 * only when the sender has nothing more to send, i.e. is waiting.
		 */
		dp_rxwant = 1;
	} else if (err == 0) {				/* receive finished */
		unsigned char *h = dprx;
		int got = 0, more;

		do {
			n = (h[0] << 8) | h[1];
			if (h[2] == 0xff && h[3] == 0xff && h[4] == 0xff &&
			    h[5] == 0xff) {
				/* the adapter dropped a packet and stays wedged
				 * until it is disabled and enabled again */
				dp_rxwant = 0;
				dp_reinit = 2;
				en_if.if_stats.ifs_ierrors++;
				break;
			}
			if (n == 0 || n > 1524)
				break;			/* no (further) frame */
			more = h[5] & DP_RXMORE;
			got++;
			if (n - 4 >= 14 && (mp = allocb(n - 4, BPRI_MED)) != (mblk_t *)0) {
				/* n - 4: the device appends the CRC */
				bcopy((caddr_t)(h + 6), (caddr_t)mp->b_wptr, n - 4);
				mp->b_wptr += n - 4;
				en_if.if_stats.ifs_ipackets++;
				en_rproc(mp);
			} else
				en_if.if_stats.ifs_ierrors++;
			h += 6 + n;
		} while (more && h + 6 <= dprx + DP_RXASK);
		if (got)
			dp_idle = 0;			/* more may be queued: go again */
		else if (dp_reinit == 0) {
			dp_rxwant = 0;			/* empty: wait for the timer */
			dp_idle++;
		}
	} else {
		dp_rxwant = 0;
		en_if.if_stats.ifs_ierrors++;
	}
	dp_kick(0);
}

/*
 * callout. MEASURED 2026-09-21: a command queued from here never completes
 * (sj_jobentry does spl4()/splx() - an ABSOLUTE level change - inside the
 * clock interrupt). So the callout touches no SCSI state at all: it only
 * qenable()s a read queue, and that queue's service procedure (enrsrv, run by
 * the STREAMS scheduler outside interrupt context) does the kick.
 */
static queue_t *dp_pollq;

static void
dp_poll()
{
	int ticks;

	if (dp_up == 0)
		return;
	if (dp_pollq != (queue_t *)0)
		qenable(dp_pollq);
	/* every tick while there is traffic; after ~100 empty polls, slower */
	ticks = (dp_idle > 100) ? HZ / 20 : 1;
	if (ticks < 1)
		ticks = 1;
	dp_tid = timeout(dp_poll, (caddr_t)0, ticks);
}

static int
dpfind()
{
	int t;
	struct scsijoblock *job;

	if (scsiinitdone == 0)
		sj_init();
	for (t = 0; t < 8; t++) {
		job = sj_selectjob(t, 0);
		if (job == (struct scsijoblock *)0 || job->sj_dtype != PROC)
			continue;
		if (job->sj_inqdata[8] != 'D' || job->sj_inqdata[9] != 'a' ||
		    job->sj_inqdata[10] != 'y')
			continue;
		if (job->sj_startf != sj_badstart && job->sj_startf != dpSTART)
			continue;
		job->sj_startf = dpSTART;
		dpjob = job;
		return t;
	}
	return -1;
}

/* EN_LINIT: bring the hardware up. Process context (first open). */
static void
dp_linit(dev)
dev_t dev;
{
	int i, t;

	if (en_if.if_flags & ENF_RUNNING)
		return;
	if (dpjob == (struct scsijoblock *)0 && (t = dpfind()) < 0) {
		cmn_err(CE_CONT, "dp0: no DaynaPORT on the SCSI bus\n");
		return;
	}
	/*
	 * Enable first: the real adapter refuses every data command while it
	 * is disabled, and for about 500 ms after ENABLE (the emulated ones
	 * accept both at once). The MAC read doubles as the settle probe -
	 * TEST UNIT READY answers GOOD throughout and cannot serve. Ask for
	 * the ROM's 22 bytes (MAC + four counters), then the emulators' 18.
	 */
	if (dpcmd(dev, 0x0E, 0x80, dpdata, 0, B_READ)) {
		cmn_err(CE_CONT, "dp0: cannot enable the interface\n");
		return;
	}
	for (t = 0; ; t++) {
		if (dpcmd(dev, 0x09, 0, dpdata, 22, B_READ) == 0 ||
		    dpcmd(dev, 0x09, 0, dpdata, 18, B_READ) == 0)
			break;
		if (t >= 100) {			/* 1 s: twice the settle window */
			cmn_err(CE_CONT, "dp0: cannot read the ethernet address\n");
			dpcmd(dev, 0x0E, 0, dpdata, 0, B_READ);
			return;
		}
		delay(HZ / 100 ? HZ / 100 : 1);
	}
	for (i = 0; i < 6; i++)
		en_if.if_enaddr[i] = dpdata[i];
	cmn_err(CE_CONT, "dp0: DaynaPORT at SCSI id %d, %x:%x:%x:%x:%x:%x\n",
	    dpjob->sj_target, en_if.if_enaddr[0], en_if.if_enaddr[1],
	    en_if.if_enaddr[2], en_if.if_enaddr[3], en_if.if_enaddr[4],
	    en_if.if_enaddr[5]);
	en_if.if_flags |= ENF_RUNNING;
	dp_idle = 0;
	dp_reinit = 0;
	dp_errs = 0;
	dp_settle = lbolt;
	dp_up = 1;
	dp_tid = timeout(dp_poll, (caddr_t)0, HZ / 10);
}

/*
 * EN_XMIT: 1 = frame taken, 0 = busy (caller queues it and en_wproc retries
 * when the transmit completes). Called at splstr. mp = 14-byte header block
 * + data blocks, exactly as LA's la_xmit receives it.
 */
static int
dp_xmit(q, mp)
queue_t *q;
mblk_t *mp;
{
	mblk_t *bp;
	int n, len;

	if (dp_txlen || dp_up == 0)
		return 0;
	len = 0;
	for (bp = mp; bp != (mblk_t *)0; bp = bp->b_cont) {
		n = bp->b_wptr - bp->b_rptr;
		if (n <= 0)
			continue;
		if (len + n > 1514) {
			len = -1;
			break;
		}
		bcopy((caddr_t)bp->b_rptr, (caddr_t)(dptx + len), n);
		len += n;
	}
	freemsg(mp);
	if (len < 14) {
		en_if.if_stats.ifs_oerrors++;
		return 1;				/* dropped, but consumed */
	}
	while (len < 60)
		dptx[len++] = 0;
	dp_txlen = len;
	dp_kick(1);
	return 1;
}

/* ------------------------------------------------------------------ DLPI half */

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

/* a received frame: route by ethertype to the minor bound to it */
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
	for (i = 0; i < DP_NMINOR; i++)
		if (en_min[i].min_sap == type && en_min[i].min_state == DL_IDLE)
			break;
	if (i == DP_NMINOR || en_min[i].min_rdq == (queue_t *)0) {
		freemsg(mp);
		en_if.if_stats.ifs_ierrors++;
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

/* transmit done: round-robin the minors' write queues (LA's en_wproc) */
static void
en_wproc()
{
	queue_t *wq;
	mblk_t *mp;
	int tries;

	for (tries = 0; en_if.if_wqcnt && tries < 2 * DP_NMINOR; tries++) {
		if (++en_if.if_nextmin >= DP_NMINOR)
			en_if.if_nextmin = 0;
		if (en_min[en_if.if_nextmin].min_rdq == (queue_t *)0)
			continue;
		wq = WR(en_min[en_if.if_nextmin].min_rdq);
		if (wq->q_first == (mblk_t *)0)
			continue;
		mp = getq(wq);
		if (mp == (mblk_t *)0)
			continue;
		if (dp_xmit(wq, mp))
			en_if.if_wqcnt--;
		else {
			putbq(wq, mp);
			return;
		}
	}
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
		cmn_err(CE_WARN, "dp0: Ethernet address not set");
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
		for (i = 0; i < DP_NMINOR; i++)
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
		if (bcmp((caddr_t)dp_bcast, (caddr_t)dst, 6) == 0) {
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
		s = splstr();
		if (en_if.if_wqcnt != 0 || dp_xmit(q, hp) == 0) {
			putq(q, hp);
			en_if.if_wqcnt++;
		}
		splx(s);
		return;

	default:
		freemsg(mp);
		return;
	}
}

#define ENIOC_GETADDR	(('E' << 8) | 1)
#define ENIOC_SETADDR	(('E' << 8) | 2)

static void
en_ioctl(q, mp)
queue_t *q;
mblk_t *mp;
{
	struct iocblk *ioc;
	mblk_t *dp;

	ioc = (struct iocblk *)mp->b_rptr;
	mp->b_datap->db_type = M_IOCNAK;
	if ((mp->b_wptr - mp->b_rptr) >= sizeof(struct iocblk) &&
	    ioc->ioc_count != TRANSPARENT && ioc->ioc_cmd == ENIOC_GETADDR) {
		dp = mp->b_cont;
		if (dp == (mblk_t *)0 || (dp->b_wptr - dp->b_rptr) != 6) {
			if (dp)
				freemsg(dp);
			dp = allocb(6, BPRI_MED);
			mp->b_cont = dp;
			if (dp)
				dp->b_wptr += 6;
		}
		if (dp) {
			bcopy((caddr_t)en_if.if_enaddr, (caddr_t)dp->b_rptr, 6);
			ioc->ioc_count = 6;
			mp->b_datap->db_type = M_IOCACK;
		} else
			ioc->ioc_error = ENOMEM;
	}
	qreply(q, mp);
}

static int
enwput(q, mp)
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
			flushq(q, FLUSHDATA);
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

/* read-side service procedure: the poll, outside interrupt context */
static int
enrsrv(q)
queue_t *q;
{
	int s;

	s = splstr();			/* blocks SCSI completions (spl4) too */
	dp_rxwant = 1;
	dp_kick(1);
	splx(s);
	return 0;
}

static int
enopen(q, devp, flag, sflag, crp)
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
		for (m = 0; m < DP_NMINOR; m++)
			if (en_min[m].min_rdq == (queue_t *)0)
				break;
		if (m == DP_NMINOR)
			return ENOSPC;
		*devp = makedevice(itoemajor(getmajor(*devp), -1), m);
	} else {
		m = getminor(*devp);
		if (m < 0 || m >= DP_NMINOR)
			return EINVAL;
	}
	dp_linit(*devp);				/* first open brings the link up */
	q->q_ptr = (caddr_t)&en_min[m];
	WR(q)->q_ptr = (caddr_t)&en_min[m];
	en_min[m].min_rdq = q;
	en_min[m].min_state = DL_UNBOUND;
	en_min[m].min_sap = 0;
	if (dp_pollq == (queue_t *)0)
		dp_pollq = q;
	return 0;
}

static int
enclose(q, flag, crp)
queue_t *q;
int flag;
struct cred *crp;
{
	enminor_t *min;
	mblk_t *mp;
	int s;

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
	if (dp_pollq == q) {		/* hand the poll to another open minor */
		int i;

		dp_pollq = (queue_t *)0;
		for (i = 0; i < DP_NMINOR; i++)
			if (en_min[i].min_rdq != (queue_t *)0) {
				dp_pollq = en_min[i].min_rdq;
				break;
			}
	}
	splx(s);
	return 0;
}

static struct module_info en_minfo = { 0x4450, "dp", 1, 1500, 4800, 2000 };
static struct qinit enrinit = { 0, enrsrv, enopen, enclose, 0, &en_minfo, 0 };
static struct qinit enwinit = { enwput, 0, 0, 0, 0, &en_minfo, 0 };
struct streamtab dpinfo = { &enrinit, &enwinit, 0, 0 };

/* run once at boot: register the interface for netstat -i. NO SCSI here. */
void
dpstart()
{
	en_if.if_stats.ifs_name = "en";	/* match the IP name so netstat -i pairs them */
	en_if.if_stats.ifs_unit = 0;
	en_if.if_stats.ifs_active = 1;
	en_if.if_stats.ifs_mtu = 1500;
	en_if.if_stats.ifs_next = ifstats;
	ifstats = &en_if.if_stats;
}
