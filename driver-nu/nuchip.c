/*
 * From Ash Nazag (github.com/kdedon/ashnazag, kernel/atari/netusbee,
 * 5ae4923), MIT licence: see LICENSE.ashnazag.
 *
 * DP8390 core for the NetUSBee's RTL8019AS in 8-bit mode, polled.
 *
 * Buffer memory (256-byte pages):
 *
 *   memlo          memlo+12                                   memhi
 *   | tx 0 | tx 1 |  receive ring (rxstart .. rxstop)          |
 *
 * Two transmit buffers: one on the wire while the next is copied in.
 * Receive: the chip writes frames at CURR; the host reads from rxnext
 * and keeps BNRY one page behind it.
 */
#include "nu.h"

#ifdef NU_WORDS
#define NU_DCRMODE	(DCR_LS | DCR_FT1 | 0x01)	/* WTS: 16-bit DMA transfers */
#else
#define NU_DCRMODE	(DCR_LS | DCR_FT1)
#endif

/*
 * A CR write, read back for its page: on the TT a page select has been
 * seen not to take, and what followed it then went to the wrong page's
 * registers (a page-0 BNRY into PAR2, a page-0 ISR clear into CURR).  CR
 * reads the same on every page.
 */
static unsigned long nu_crmiss;		/* page selects written again */

static void
nu_setcr(v)
register int v;
{
	register int i;

	for (i = 0; i < 4; i++) {
		NU_WR(NU_CR, v);
		if (((NU_RD(NU_CR) ^ v) & (CR_PAGE1 | CR_PAGE2)) == 0)
			return;
		nu_crmiss++;
	}
}

static void
nu_page0()
{
	nu_setcr(CR_NODMA | CR_STA);
}

/* Remote DMA: n bytes of buffer memory at a, to or from buf. */
static void
nu_dma(a, n, cmd)
int a, n, cmd;
{
	nu_setcr(CR_NODMA | CR_STA);
	NU_WR(NU_ISR, ISR_RDC);
	NU_WR(NU_RBCR0, n);
	NU_WR(NU_RBCR1, n >> 8);
	NU_WR(NU_RSAR0, a);
	NU_WR(NU_RSAR1, a >> 8);
	NU_WR(NU_CR, cmd | CR_STA);
}

static void
nu_dmadone()
{
	register int i;

	for (i = 0; i < 1000 && !(NU_RD(NU_ISR) & ISR_RDC); i++)
		;
	NU_WR(NU_ISR, ISR_RDC);
}

static void
nu_rdmem(a, buf, n)
int a, n;
register unsigned char *buf;
{
	register int i;

	if (n <= 0)
		return;
#ifdef NU_WORDS
	nu_dma(a, n, CR_RREAD);
	for (i = 0; i + 1 < n; i += 2) {
		register unsigned short w = NU_RD16();
		*buf++ = w >> 8;
		*buf++ = w;
	}
	if (i < n)
		*buf = NU_RD16() >> 8;
	nu_dmadone();
#else
	nu_dma(a, n, CR_RREAD);
	for (i = 0; i < n; i++) {
		if ((i & 127) == 127)
			NU_YIELD();
		*buf++ = NU_RD(NU_DATA);
	}
	nu_dmadone();
#endif
}

static void
nu_wrmem(a, buf, n)
int a, n;
register unsigned char *buf;
{
	register int i;

	if (n <= 0)
		return;
#ifdef NU_WORDS
	nu_dma(a, (n + 1) & ~1, CR_RWRITE);
	for (i = 0; i + 1 < n; i += 2, buf += 2)
		NU_WR16((buf[0] << 8) | buf[1]);
	if (i < n)
		NU_WR16(buf[0] << 8);
	nu_dmadone();
#else
	nu_dma(a, n, CR_RWRITE);
	for (i = 0; i < n; i++) {
		if ((i & 127) == 127)
			NU_YIELD();
		NU_WR(NU_DATA, *buf++);
	}
	nu_dmadone();
#endif
}

/* Ring read of n bytes at page p, offset off, wrapping at rxstop. */
static void
nu_rdring(sc, p, off, buf, n)
struct nu_softc *sc;
int p, off, n;
unsigned char *buf;
{
	int a = (p << 8) + off, top = sc->rxstop << 8, k;

	if (a >= top)
		a -= top - (sc->rxstart << 8);
	k = top - a;
	if (k > n)
		k = n;
	nu_rdmem(a, buf, k);
	nu_rdmem(sc->rxstart << 8, buf + k, n - k);
}

static int
nu_reset()
{
	register int i, x;

	x = NU_RD(NU_RESET);
	NU_WR(NU_RESET, x);
	/*
	 * The RTL8019AS reloads its 9346 EEPROM; what is written meanwhile
	 * is lost (seen on a NetUSBee).  ISR_RST cannot tell: a stopped chip
	 * shows it already.
	 */
	NU_DELAY(20000);
	for (i = 0; i < 20; i++) {
		if (NU_RD(NU_ISR) & ISR_RST)
			break;
		NU_DELAY(1000);
	}
	NU_WR(NU_ISR, 0xff);
	return i < 20 ? 0 : -1;
}

/* Memory test: write a pattern at page p and read it back. */
static int
nu_memok(p)
int p;
{
	unsigned char w[8], r[8];
	register int i;

	for (i = 0; i < 8; i++)
		w[i] = (p + i * 37) ^ 0xa5;
	nu_wrmem(p << 8, w, 8);
	nu_rdmem(p << 8, r, 8);
	for (i = 0; i < 8; i++)
		if (r[i] != w[i])
			return 0;
	return 1;
}

/*
 * Page b is page a again (the NetUSBee's RTL8019AS in 8-bit mode answers
 * for 16 KB but holds 8: 0x60 is 0x40, so a 16 KB layout puts the ring's
 * upper half on the transmit buffers and the lower half).  A pattern at
 * a, its complement at b, and a read back at a says which.
 */
static int
nu_memalias(a, b)
int a, b;
{
	unsigned char w[8], r[8];
	register int i;

	for (i = 0; i < 8; i++)
		w[i] = (a + i * 37) ^ 0xa5;
	nu_wrmem(a << 8, w, 8);
	for (i = 0; i < 8; i++)
		w[i] ^= 0xff;
	nu_wrmem(b << 8, w, 8);
	nu_rdmem(a << 8, r, 8);
	for (i = 0; i < 8; i++)
		if (r[i] != w[i])
			return 0;
	return 1;
}

/*
 * Find an 8390 behind the port.  Without a card the window reads
 * whatever the bus floats or a cartridge ROM, so it takes a reset that
 * reports itself, CR at its reset value and a counter that clears on
 * read.  Then size the memory and read the station address PROM.
 */
int
nu_probe(sc)
register struct nu_softc *sc;
{
	unsigned char pr[32];
	register int i, dbl;
#if !defined(NU_PCI) && !defined(NU_XSURF)
	int m;
#endif

	if (NU_RD(NU_CR) == 0xff)
		return -1;
	if (nu_reset() < 0)
		return -1;
#if !defined(NU_PCI) && !defined(NU_XSURF)
	/* on PCI the device ID already said what it is */
	if ((NU_RD(NU_CR) & 0x3f) != (CR_NODMA | CR_STP))
		return -1;
	nu_setcr(CR_NODMA | CR_STP | CR_PAGE1);
	m = NU_RD(NU_MAR0 + 5);
	NU_WR(NU_MAR0 + 5, 0xff);
	nu_setcr(CR_NODMA | CR_STP);
	(void)NU_RD(NU_CNTR0);
	if (NU_RD(NU_CNTR0) != 0) {
		nu_setcr(CR_NODMA | CR_STP | CR_PAGE1);
		NU_WR(NU_MAR0 + 5, m);
		nu_setcr(CR_NODMA | CR_STP);
		return -1;
	}
#else
	nu_setcr(CR_NODMA | CR_STP);
#endif
	sc->rtl = NU_RD(NU_ID0) == 'P' && (NU_RD(NU_ID1) == 'p' || NU_RD(NU_ID1) == 'C');

	NU_WR(NU_DCR, NU_DCRMODE);
	NU_WR(NU_RCR, RCR_MON);
	NU_WR(NU_TCR, TCR_LOOP);
	NU_WR(NU_IMR, 0);
	NU_WR(NU_ISR, 0xff);
	if (nu_memok(0x40) && nu_memok(0x7f)) {
		sc->memlo = 0x40, sc->memhi = 0x80;
		/*
		 * The RTL8019AS in 8-bit mode holds 8 KB (its data sheet:
		 * PSTOP at most 0x60), and says so with 0x50 0x70 in the
		 * remote byte count registers, which are write-only on an
		 * 8390 (Linux ne.c does the same check).  The alias test
		 * catches it without the signature too.
		 */
		if ((NU_RD(NU_RBCR0) == 0x50 && NU_RD(NU_RBCR1) == 0x70) ||
		    nu_memalias(0x40, 0x60))
			sc->memhi = 0x60;
	} else if (nu_memok(0x20) && nu_memok(0x3f))
		sc->memlo = 0x20, sc->memhi = 0x40;
	else
		return -1;

	/* 8-bit NE2000 PROMs repeat each byte; NE1000 ones do not */
	nu_rdmem(0, pr, 32);
	for (dbl = 1, i = 0; i < 32; i += 2)
		if (pr[i] != pr[i + 1])
			dbl = 0;
	for (i = 0; i < 6; i++)
		sc->prom[i] = pr[dbl ? 2 * i : i];
	/* whether the counts include the CRC shows in the first short frame */
	sc->crcprobe = 1;
	sc->nocrc = 0;
	return 0;
}

/* Station address, receive mode and multicast hash. */
int
nu_filter(sc)
register struct nu_softc *sc;
{
	register int i;

	nu_setcr(CR_NODMA | (sc->running ? CR_STA : CR_STP) | CR_PAGE1);
	for (i = 0; i < 6; i++)
		NU_WR(NU_PAR0 + i, sc->ea[i]);
	for (i = 0; i < 8; i++)
		NU_WR(NU_MAR0 + i, sc->mar[i]);
	nu_setcr(CR_NODMA | (sc->running ? CR_STA : CR_STP));
	NU_WR(NU_RCR, sc->rcr);
	return 0;
}

/*
 * Read back, the chip stopped, what nu_init gave it: page 2 holds page 0's
 * ring and mode registers, page 1 the station address and CURR.  0, or the
 * number that differ; the last of those is kept in sc->bad*.
 */
static int
nu_verify(sc)
register struct nu_softc *sc;
{
	static unsigned char p2reg[] = { NU_P2PSTART, NU_P2PSTOP, NU_P2RCR,
	    NU_P2TCR, NU_P2DCR, NU_P2IMR };
	static unsigned char p2mask[] = { 0xff, 0xff, 0x3f, 0x1f, 0x7f, 0x7f };
	unsigned char want[6];
	register int i, bad = 0, got;

	want[0] = sc->rxstart;
	want[1] = sc->rxstop;
	want[2] = sc->rcr;
	want[3] = TCR_LOOP;
	want[4] = NU_DCRMODE;
	want[5] = 0;
	nu_setcr(CR_NODMA | CR_STP | CR_PAGE2);
	for (i = 0; i < 6; i++)
		if ((got = NU_RD(p2reg[i]) & p2mask[i]) != (want[i] & p2mask[i])) {
			bad++;
			sc->badreg = 0x200 | p2reg[i];
			sc->badwant = want[i] & p2mask[i];
			sc->badgot = got;
		}
	nu_setcr(CR_NODMA | CR_STP | CR_PAGE1);
	for (i = 0; i < 6; i++)
		if ((got = NU_RD(NU_PAR0 + i)) != sc->ea[i]) {
			bad++;
			sc->badreg = 0x100 | (NU_PAR0 + i);
			sc->badwant = sc->ea[i];
			sc->badgot = got;
		}
	if ((got = NU_RD(NU_CURR)) != sc->rxstart) {
		bad++;
		sc->badreg = 0x100 | NU_CURR;
		sc->badwant = sc->rxstart;
		sc->badgot = got;
	}
	nu_setcr(CR_NODMA | CR_STP);
	if ((got = NU_RD(NU_BNRY)) != sc->rxstop - 1) {
		bad++;
		sc->badreg = NU_BNRY;
		sc->badwant = sc->rxstop - 1;
		sc->badgot = got;
	}
	return bad;
}

int
nu_init(sc)
register struct nu_softc *sc;
{
	register int i;

	sc->running = 0;
	if (nu_reset() < 0)
		return -1;
	nu_setcr(CR_NODMA | CR_STP);
	NU_WR(NU_DCR, NU_DCRMODE);
	NU_WR(NU_RBCR0, 0);
	NU_WR(NU_RBCR1, 0);
	NU_WR(NU_RCR, RCR_MON);
	NU_WR(NU_TCR, TCR_LOOP);
	sc->rxstart = sc->memlo + NU_NTX * NU_TXPAGES;
	sc->rxstop = sc->memhi;
	NU_WR(NU_PSTART, sc->rxstart);
	NU_WR(NU_PSTOP, sc->rxstop);
	NU_WR(NU_BNRY, sc->rxstop - 1);
	NU_WR(NU_ISR, 0xff);
	NU_WR(NU_IMR, 0);
	nu_setcr(CR_NODMA | CR_STP | CR_PAGE1);
	NU_WR(NU_CURR, sc->rxstart);
	sc->rxnext = sc->rxstart;
	for (i = 0; i < NU_NTX; i++)
		sc->txlen[i] = 0;
	sc->txcur = -1;
	sc->txfill = 0;
	sc->txage = 0;
	(void)nu_filter(sc);
	if (nu_verify(sc)) {
		sc->st.initbad++;
		nu_setcr(CR_NODMA | CR_STP);
		return -1;
	}
	nu_setcr(CR_NODMA | CR_STA);
	NU_WR(NU_TCR, 0);
	NU_WR(NU_IMR, sc->imr);
	sc->running = 1;
	return 0;
}

void
nu_stop(sc)
struct nu_softc *sc;
{
	sc->running = 0;
	nu_setcr(CR_NODMA | CR_STP);
	NU_WR(NU_IMR, 0);
	NU_WR(NU_ISR, 0xff);
}

/* ------------------------------------------------------------ transmit */

static void
nu_kick(sc, b)
register struct nu_softc *sc;
int b;
{
	int n = sc->txlen[b];

	sc->txcur = b;
	sc->txage = 0;
	nu_page0();
	NU_WR(NU_TPSR, sc->memlo + b * NU_TXPAGES);
	NU_WR(NU_TBCR0, n);
	NU_WR(NU_TBCR1, n >> 8);
	NU_WR(NU_CR, CR_NODMA | CR_STA | CR_TXP);
}

/* The frame on the wire finished: account, free its buffer, start the next. */
static int
nu_txreclaim(sc, isr)
register struct nu_softc *sc;
int isr;
{
	register int t, b;

	if (sc->txcur < 0 || !(isr & (ISR_PTX | ISR_TXE)))
		return 0;
	NU_WR(NU_ISR, isr & (ISR_PTX | ISR_TXE));
	t = NU_RD(NU_TSR);
	if (t & TSR_PTX)
		sc->st.opackets++;
	else
		sc->st.oerrors++;
	if (t & TSR_COL)
		sc->st.collisions++;
	if (t & TSR_ABT)
		sc->st.txabt++;
	if (t & TSR_CRS)
		sc->st.txcarrier++;
	if (t & TSR_FU)
		sc->st.txfu++;
	if (t & TSR_OWC)
		sc->st.txowc++;
	b = sc->txcur;
	sc->txlen[b] = 0;
	sc->txcur = -1;
	b = (b + 1) % NU_NTX;
	if (sc->txlen[b])
		nu_kick(sc, b);
	return 1;
}

int
nu_txroom(sc)
register struct nu_softc *sc;
{
	if (sc->txlen[sc->txfill])
		(void)nu_txreclaim(sc, NU_RD(NU_ISR));
	return sc->txlen[sc->txfill] == 0;
}

/* Copy sc->txbuf (len bytes) into the next buffer and queue it. */
void
nu_txstart(sc, len)
register struct nu_softc *sc;
int len;
{
	register int b = sc->txfill;

	while (len < NU_MINFRAME)
		sc->txbuf[len++] = 0;
	nu_wrmem((sc->memlo + b * NU_TXPAGES) << 8, sc->txbuf, len);
	sc->txlen[b] = len;
	sc->txfill = (b + 1) % NU_NTX;
	if (sc->txcur < 0)
		nu_kick(sc, b);
}

/* ------------------------------------------------------------- receive */

static int
nu_curr()
{
	register int c;

	nu_setcr(CR_NODMA | CR_STA | CR_PAGE1);
	c = NU_RD(NU_CURR);
	nu_page0();
	return c;
}

static void
nu_setbnry(sc)
register struct nu_softc *sc;
{
	NU_WR(NU_BNRY, (sc->rxnext == sc->rxstart ? sc->rxstop : sc->rxnext) - 1);
}

/*
 * Each frame starts with a 4-byte header: status, next page, byte count
 * (header and CRC included).  The next page must lie as many pages on as
 * the count needs (some chips skip one more); otherwise the ring is lost
 * and everything up to CURR is dropped.
 */
static int
nu_rxdrain(sc)
register struct nu_softc *sc;
{
	unsigned char h[4];
	register int n, cnt, nxt, dist, pages, ring, curr, crc;

	ring = sc->rxstop - sc->rxstart;
	for (n = 0; n < NU_RXBURST; n++) {
		if (n > 0 && NU_CLKDUE())
			break;
		curr = nu_curr();
		if (curr < sc->rxstart || curr >= sc->rxstop)
			return -1;
		if (curr == sc->rxnext)
			break;
		nu_rdring(sc, sc->rxnext, 0, h, 4);
		nxt = h[1];
		cnt = h[2] | h[3] << 8;
		pages = (cnt + 255) >> 8;
		dist = (nxt - sc->rxnext + ring) % ring;
		if (nxt < sc->rxstart || nxt >= sc->rxstop || pages == 0 ||
		    (dist != pages && dist != pages + 1)) {
			sc->st.rxbad++;
			sc->rxnext = curr;
			nu_setbnry(sc);
			break;
		}
		/*
		 * The count includes the CRC.  Where some clones leave it out
		 * (crcprobe), a count too short for it says so, from then on.
		 */
		if (sc->crcprobe && cnt >= 4 + NU_MINFRAME && cnt < 4 + NU_MINFRAME + 4)
			sc->nocrc = 1;
		crc = sc->nocrc ? 0 : 4;
		if (cnt < 4 + NU_MINFRAME + crc || cnt > 4 + NU_MAXFRAME + crc)
			sc->st.ierrors++;
		else if (h[0] & RSR_PRX) {
			nu_rdring(sc, sc->rxnext, 4, sc->rxbuf, cnt - 4);
			sc->st.ipackets++;
			nu_input(sc, sc->rxbuf, cnt - 4 - crc);
		} else
			sc->st.ierrors++;
		sc->rxnext = nxt;
		nu_setbnry(sc);
	}
	return n;
}

/*
 * Ring overflow, the datasheet's recovery: stop, note a transmit cut
 * short, drain in loopback, restart, resend.  -1: the ring is lost too.
 */
static int
nu_overflow(sc)
register struct nu_softc *sc;
{
	int txp, resend = 0, r;

	sc->st.ovw++;
	txp = NU_RD(NU_CR) & CR_TXP;
	nu_setcr(CR_NODMA | CR_STP);
	NU_DELAY(2000);
	NU_WR(NU_RBCR0, 0);
	NU_WR(NU_RBCR1, 0);
	if (txp && !(NU_RD(NU_ISR) & (ISR_PTX | ISR_TXE)))
		resend = 1;
	NU_WR(NU_TCR, TCR_LOOP);
	nu_setcr(CR_NODMA | CR_STA);
	if ((r = nu_rxdrain(sc)) < 0) {
		sc->st.rxbad++;
		return -1;
	}
	NU_WR(NU_ISR, ISR_OVW);
	NU_WR(NU_TCR, 0);
	if (resend)
		NU_WR(NU_CR, CR_NODMA | CR_STA | CR_TXP);
	return r;
}

/*
 * Once a tick.  A transmit that has not finished within 2 seconds, a
 * ring that makes no sense or a stopped chip gets a full re-init.
 */
int
nu_poll(sc)
register struct nu_softc *sc;
{
	register int isr, work = 0, r;

	if (!sc->running)
		return 0;
	sc->st.polls++;
	sc->st.crmiss = nu_crmiss;
	/* started, and on page 0: a select that went astray shows here */
	if ((NU_RD(NU_CR) & (CR_PAGE1 | CR_PAGE2 | CR_STP | CR_STA)) != CR_STA)
		goto reinit;
	isr = NU_RD(NU_ISR);
	if (isr & ISR_OVW) {
		if (nu_overflow(sc) < 0)
			goto reinit;
		work = 1;
	} else {
		if (isr & (ISR_PRX | ISR_RXE))
			NU_WR(NU_ISR, isr & (ISR_PRX | ISR_RXE));
		if ((r = nu_rxdrain(sc)) < 0)
			goto reinit;
		work |= r > 0;
	}
	if (isr & ISR_CNT) {
		sc->st.fae += NU_RD(NU_CNTR0);
		sc->st.crc += NU_RD(NU_CNTR1);
		sc->st.missed += NU_RD(NU_CNTR2);
		NU_WR(NU_ISR, ISR_CNT);
	}
	if (nu_txreclaim(sc, NU_RD(NU_ISR))) {
		work = 1;
		nu_txdone(sc);
	} else if (sc->txcur >= 0 && ++sc->txage > 120) {
		sc->st.txtimeout++;
		goto reinit;
	}
	return work;
reinit:
	/*
	 * The reset waits 20 ms for the EEPROM, spinning: the caller does it
	 * (nu_init while !sc->running), at most so often.
	 */
	sc->st.reinit++;
	sc->running = 0;
	nu_txdone(sc);
	return 0;
}

/* ----------------------------------------------------------- multicast */

/* Hash: the top 6 bits of the Ethernet CRC of the address. */
static int
nu_hash(a)
unsigned char *a;
{
	register unsigned long crc = 0xffffffff;
	register int i, j, b, carry;

	for (i = 0; i < 6; i++)
		for (b = a[i], j = 0; j < 8; j++, b >>= 1) {
			carry = ((crc >> 31) & 1) ^ (b & 1);
			crc <<= 1;
			if (carry)
				crc ^= 0x04c11db7;
		}
	return (crc >> 26) & 0x3f;
}

static void
nu_mcload(sc)
register struct nu_softc *sc;
{
	register int i, h;

	for (i = 0; i < 8; i++)
		sc->mar[i] = 0;
	for (i = 0; i < sc->nmc; i++) {
		h = nu_hash(sc->mc[i]);
		sc->mar[h >> 3] |= 1 << (h & 7);
	}
	if (sc->nmc)
		sc->rcr |= RCR_AM;
	else
		sc->rcr &= ~RCR_AM;
	(void)nu_filter(sc);
}

static int
nu_mcfind(sc, a)
register struct nu_softc *sc;
register unsigned char *a;
{
	register int i, j;

	for (i = 0; i < sc->nmc; i++) {
		for (j = 0; j < 6 && sc->mc[i][j] == a[j]; j++)
			;
		if (j == 6)
			return i;
	}
	return -1;
}

int
nu_addmc(sc, a)
register struct nu_softc *sc;
unsigned char *a;
{
	register int i;

	if ((i = nu_mcfind(sc, a)) >= 0) {
		sc->mcref[i]++;
		return 0;
	}
	if (sc->nmc == NU_NMC)
		return -1;
	for (i = 0; i < 6; i++)
		sc->mc[sc->nmc][i] = a[i];
	sc->mcref[sc->nmc++] = 1;
	nu_mcload(sc);
	return 0;
}

int
nu_delmc(sc, a)
register struct nu_softc *sc;
unsigned char *a;
{
	register int i, j;

	if ((i = nu_mcfind(sc, a)) < 0)
		return -1;
	if (--sc->mcref[i] > 0)
		return 0;
	for (sc->nmc--; i < sc->nmc; i++) {
		for (j = 0; j < 6; j++)
			sc->mc[i][j] = sc->mc[i + 1][j];
		sc->mcref[i] = sc->mcref[i + 1];
	}
	nu_mcload(sc);
	return 0;
}
