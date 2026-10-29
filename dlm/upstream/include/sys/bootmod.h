/*
 * Boot modules: module files a boot loader read into memory, linked
 * before the root mounts.  A loader hands over one table; addresses
 * are physical, names are module names (NUL-padded).
 *
 *	+0  BM_MAGIC
 *	+4  count
 *	+8  count entries: address, size, name[BM_NAMELEN]
 */

#ifndef _SYS_BOOTMOD_H
#define _SYS_BOOTMOD_H

#define	BM_MAGIC	0x424D4F44	/* 'BMOD' */
#define	BM_MAX		16
#define	BM_NAMELEN	16
#define	BM_ENTSZ	(8 + BM_NAMELEN)

struct bootmod {
	unsigned long	bm_addr;
	unsigned long	bm_size;
	char		bm_name[BM_NAMELEN];
};

#ifdef _KERNEL
extern void	dlm_bootadd();		/* (addr, size, name) at config time */
extern void	dlm_boottab();		/* (table) at config time */
extern unsigned long dlm_bootpack();	/* (lo, hi, copy): new top of memory */
extern int	sd_attach();		/* (open, queue, c, addr, name) */
#endif

#endif
