/*
 * ub_io.c - the ISP1160 behind the Lightning VME or the NetUSBee (ub.h).
 */
#include "ub.h"

int ub_bus;
int ub_swap;
int ub_fifo_wswap, ub_fifo_rswap;

#ifdef _KERNEL
static volatile unsigned short *l_data = (volatile unsigned short *)0xFEFF8000;
static volatile unsigned short *l_addr = (volatile unsigned short *)0xFEFF8004;
static volatile unsigned char *rom = (volatile unsigned char *)0xFFFA0000;
extern int spl7();
extern void splx();
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
static volatile unsigned short *l_data, *l_addr;
static volatile unsigned char *rom;
#endif
static volatile unsigned short ub_sink;
static volatile int ub_spin;

/* the NetUSBee: a byte latched by a read at 0xFA0000 + (b << 1), then a
 * read at 0xFBC000 (command = register number) or 0xFB8000 + (b << 1)
 * (data: the latched byte and this one); data reads 16 bits at 0xFA8000 */
#ifdef _KERNEL
/*
 * From the kernel, which runs from ST-RAM, the card sees the 68030's next
 * instruction fetch on the address lines it samples (the nu driver's
 * finding, driver-nu/nu.c nu_acc): each access is a short loop run first
 * on a pad in ST-RAM with the same A1-A13 as the card address, which
 * brings it into the instruction cache, then with the cache frozen (CACR
 * FI) on the card, a pad store right before and right after; at spl7, so
 * nothing else fetches between the two passes.  The two halves of a
 * register access (ub_wraddr, ub_wr16) are at spl7 together, so the nu
 * driver's Ethernet polling, whose reads latch a byte too, cannot come
 * between them.
 */
static unsigned char ub_padraw[0x8000];
static volatile unsigned char *ub_pad;
#define UB_PAD(off)	(ub_pad + ((off) & 0x3fff))
static unsigned ub_nuacc();
#define NU_LSB(b)	ub_nuacc(rom + (((b) & 0xff) << 1), UB_PAD(((b) & 0xff) << 1), 0)
#define NU_CMD		ub_nuacc(rom + 0x1C000, UB_PAD(0x1C000), 0)
#define NU_DATA(b)	ub_nuacc(rom + 0x18000 + (((b) & 0xff) << 1), \
			    UB_PAD(0x18000 + (((b) & 0xff) << 1)), 0)
#define NU_RD()		ub_nuacc(rom + 0x8000, UB_PAD(0x8000), 1)

/* movec as words: the build's assembler pass turns the mnemonic into mov */
static unsigned long
ub_getcacr()
{
	register unsigned long c asm("d0");

	asm volatile (".short 0x4e7a,0x0002" : "=r" (c));	/* movec %cacr,%d0 */
	return c;
}

static void
ub_setcacr(v)
unsigned long v;
{
	register unsigned long c asm("d0") = v;

	asm volatile (".short 0x4e7b,0x0002" : : "r" (c));	/* movec %d0,%cacr */
}

/* one read of the card at a (a byte, or a word when word is set), pad
 * stores at p around it: nu_acc, with a word form */
static unsigned
ub_nuacc(a, p, word)
volatile unsigned char *a, *p;
int word;
{
	register unsigned long c = ub_getcacr();
	register unsigned long fi = c | 0x2;		/* CACR FI set */
	register unsigned long t;
	int s = spl7();

	/* in assembly, so that both passes run the very same instructions */
	if (word)
		asm volatile (
		    "\tmovel %2,%%d1\n"		/* the frozen CACR, ready */
		    "\tmoveal %3,%%a1\n"		/* x = p: the dry pass */
		    "1:\n"
		    "\tclrw %3@\n"			/* *p = 0 */
		    "\tmovew %%a1@,%0\n"		/* t = *x */
		    "\tmovew %0,%3@\n"		/* *p = t */
		    "\tnop\n"
		    "\tnop\n"
		    "\tcmpal %4,%%a1\n"
		    "\tbeqs 2f\n"
		    "\t.short 0x4e7b,0x1002\n"	/* movec %d1,%cacr: FI */
		    "\tmoveal %4,%%a1\n"		/* x = a: the real one */
		    "\tbras 1b\n"
		    "2:\n"
		    : "=d" (t) : "0" (0), "d" (fi), "a" (p), "a" (a) : "d1", "a1", "memory");
	else
		asm volatile (
		    "\tmovel %2,%%d1\n"
		    "\tmoveal %3,%%a1\n"
		    "1:\n"
		    "\tclrb %3@\n"
		    "\tmoveb %%a1@,%0\n"
		    "\tmoveb %0,%3@\n"
		    "\tnop\n"
		    "\tnop\n"
		    "\tcmpal %4,%%a1\n"
		    "\tbeqs 2f\n"
		    "\t.short 0x4e7b,0x1002\n"
		    "\tmoveal %4,%%a1\n"
		    "\tbras 1b\n"
		    "2:\n"
		    : "=d" (t) : "0" (0), "d" (fi), "a" (p), "a" (a) : "d1", "a1", "memory");
	ub_setcacr(c);
	splx(s);
	return word ? (t & 0xffff) : (t & 0xff);
}
#define NU_LOCK()	int nu_s = spl7()
#define NU_UNLOCK()	splx(nu_s)
#else
#define NU_LSB(b)	rom[((b) & 0xff) << 1]
#define NU_CMD		rom[0x1C000]
#define NU_DATA(b)	rom[0x18000 + (((b) & 0xff) << 1)]
#define NU_RD()		(*(volatile unsigned short *)(rom + 0x8000))
#define NU_LOCK()
#define NU_UNLOCK()
#endif

static void
pause()
{
	int i;

	for (i = 0; i < 8; i++)
		ub_spin = i;
}

void
ub_wraddr(reg)
unsigned reg;
{
	if (ub_bus == UB_LIGHTNING) {
		*l_addr = ub_swap ? SWAP16(reg & 0xff) : (reg & 0xff);
		return;
	}
	{
		NU_LOCK();
		ub_sink = NU_LSB(reg);
		pause();
		ub_sink = NU_CMD;
		pause();
		NU_UNLOCK();
	}
}

unsigned
ub_rd16()
{
	unsigned v;

	if (ub_bus == UB_LIGHTNING)
		v = *l_data;
	else {
		v = NU_RD();
		pause();
	}
	return ub_swap ? SWAP16(v) : (v & 0xffff);
}

void
ub_wr16(val)
unsigned val;
{
	unsigned v = ub_swap ? SWAP16(val) : (val & 0xffff);

	if (ub_bus == UB_LIGHTNING) {
		*l_data = v;
		return;
	}
	{
		NU_LOCK();
		ub_sink = NU_LSB(v & 0xff);
		pause();
		ub_sink = NU_DATA(v >> 8);
		pause();
		NU_UNLOCK();
	}
}

/* FIFO data words go through the other way round from register words,
 * both ways (measured on the Lightning VME with a SanDisk stick's device
 * descriptor; the NetUSBee's driver does the same). */
unsigned
ub_rd16plain()
{
	unsigned v = ub_rd16();

	return ub_fifo_rswap ? SWAP16(v) : v;
}

void
ub_wr16plain(val)
unsigned val;
{
	ub_wr16(ub_fifo_wswap ? SWAP16(val) : (val & 0xffff));
}

/* The chip's ID register, read with and without a swap, says which way
 * this bus hands words over: 0x61xx is the ISP1160. */
int
ub_io_init(bus)
int bus;
{
	int i;
	unsigned v;

	ub_bus = bus;
	ub_fifo_wswap = 1;
	ub_fifo_rswap = 1;
#ifdef _KERNEL
	if (ub_pad == (volatile unsigned char *)0)
		ub_pad = (volatile unsigned char *)
		    (((unsigned long)ub_padraw + 0x3fff) & ~0x3fffUL);
#else
	{
		int fd;

		if ((fd = open("/dev/mem", O_RDWR)) < 0) {
			perror("/dev/mem");
			return -1;
		}
		if (bus == UB_LIGHTNING) {
			volatile unsigned char *w = (volatile unsigned char *)mmap((caddr_t)0,
			    0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0xfeff8000);
			if (w == (volatile unsigned char *)-1) {
				perror("mmap VME");
				return -1;
			}
			l_data = (volatile unsigned short *)w;
			l_addr = (volatile unsigned short *)(w + 4);
			ub_sink = w[9];
		} else {
			rom = (volatile unsigned char *)mmap((caddr_t)0, 0x20000, PROT_READ,
			    MAP_SHARED, fd, (off_t)0xfa0000);
			if (rom == (volatile unsigned char *)-1) {
				perror("mmap cartridge");
				return -1;
			}
			/* the first touch of a page goes through the fault path
			 * and the cycle is lost: touch them all, harmlessly */
			for (i = 0; i < 0x20000; i += 0x1000)
				ub_sink = rom[i];
		}
	}
#endif
	for (ub_swap = 0; ub_swap < 2; ub_swap++) {
		ub_wraddr(0x27);
		v = ub_rd16();
		if ((v & 0xff00) == 0x6100)
			return 0;
	}
	ub_swap = 0;
	return -1;
}

#ifdef _KERNEL
/* process context (ioctl): sleep; a millisecond is a tick at most */
void
ub_udelay(us)
long us;
{
	drv_usecwait(us);
}

void
ub_mdelay(ms)
long ms;
{
	long t = (ms * HZ + 999) / 1000;

	delay(t > 0 ? t : 1);
}
#else
void
ub_udelay(us)
long us;
{
	long i;

	for (i = us * 24; i > 0; i--)
		ub_spin = i;
}

void
ub_mdelay(ms)
long ms;
{
	while (ms-- > 0)
		ub_udelay(1000L);
}
#endif

#ifdef _KERNEL
static int ub_spl;
void ub_int_off() { ub_spl = spl6(); }
void ub_int_on() { splx(ub_spl); }
#else
void ub_int_off() {}
void ub_int_on() {}
#endif
