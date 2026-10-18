/*
 * nustat.h - the nu driver's ioctls, sent down an open /dev/nu0 with
 * I_STR: NUIOC_STATS answers the chip core's struct nu_stats (nu.h),
 * NUIOC_REGS the chip's register pages as the kernel reads them.
 */
#ifndef NUSTAT_H
#define NUSTAT_H

#define NUIOC_STATS	(('N' << 8) | 1)
#define NUIOC_REGS	(('N' << 8) | 2)
#define NUIOC_MODE	(('N' << 8) | 3)	/* struct nu_mode in and out */
#define NUIOC_TEST	(('N' << 8) | 4)	/* probe + init now: struct nu_test */
#define NUIOC_RAW	(('N' << 8) | 5)	/* struct nu_raw in and out */

/*
 * NUIOC_RAW: register accesses done back to back in the kernel, in the
 * current mode, with nothing else in between (splstr): NU_RAW_RD answers
 * the byte read in val, NU_RAW_WR writes val, NU_RAW_SPIN waits val*10 us.
 * It needs no probe to have succeeded: the kernel's bus cycles, measured.
 */
#define NU_RAW_RD	0
#define NU_RAW_WR	1
#define NU_RAW_SPIN	2
#define NU_RAWMAX	64

struct nu_rawop {
	unsigned char	op, reg, val, pad;
};
struct nu_raw {
	int		n;
	struct nu_rawop	a[NU_RAWMAX];
};

struct nu_regs {
	unsigned char	page[3][16];	/* pages 0-2, registers 0-15 */
	int		cr;		/* CR before the snapshot */
	int		running;
	int		badreg, badwant, badgot;	/* last init read-back */
	long		perms;		/* MFP reads a millisecond */
};

/*
 * How the driver reaches the card.  The address pairs: 0xFFFA0000 and
 * 0xFFFB0000 (the 0xFF mirror, through TT1, not cached) or 0x00FA0000 and
 * 0x00FB0000 (through TT0, cached: only with nocache).
 */
struct nu_mode {
	unsigned long	rbase, wbase;
	int		dly;		/* spin turns before each access, 0-1000 */
	int		nocache;	/* the data cache off while the chip is worked */
	int		pad;		/* same-address ST-bus cycle: 1 before, 2 after
					   each write, 4 reads too (nu.c) */
	int		set;		/* in: 1 sets the above, 0 only reads them */
};

struct nu_test {
	int		probe, init;	/* 0 or -1 */
	int		badreg, badwant, badgot;	/* init's last read-back */
	unsigned long	initbad, crmiss;
	struct nu_regs	regs;		/* afterwards */
};

#endif
