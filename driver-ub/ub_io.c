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
static volatile unsigned char *rom = (volatile unsigned char *)0xFA0000;
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
#define NU_LSB(b)	rom[((b) & 0xff) << 1]
#define NU_CMD		rom[0x1C000]
#define NU_DATA(b)	rom[0x18000 + (((b) & 0xff) << 1)]
#define NU_RD()		(*(volatile unsigned short *)(rom + 0x8000))

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
	ub_sink = NU_LSB(reg);
	pause();
	ub_sink = NU_CMD;
	pause();
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
	ub_sink = NU_LSB(v & 0xff);
	pause();
	ub_sink = NU_DATA(v >> 8);
	pause();
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
#ifndef _KERNEL
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

#ifdef _KERNEL
static int ub_spl;
void ub_int_off() { ub_spl = spl6(); }
void ub_int_on() { splx(ub_spl); }
#else
void ub_int_off() {}
void ub_int_on() {}
#endif
