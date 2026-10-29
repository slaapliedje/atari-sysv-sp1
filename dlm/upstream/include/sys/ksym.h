/*
 * Kernel and module symbol tables, and getksym().
 *
 * A table is one block, big-endian, offsets from the block start:
 *
 *	ksymhdr			KSYMHDRSZ bytes
 *	Elf32_Sym[kh_nsym]	entry 0 is null
 *	strings			kh_strsize bytes
 *	hash			nbucket, nchain, bucket[nbucket], chain[nchain]
 *
 * The static kernel's block lives in dlm_ksym (KSYM_SPACE bytes of
 * .data), filled after the final link by mkksym.  Each loaded module
 * has one in the same format.
 */

#ifndef _SYS_KSYM_H
#define _SYS_KSYM_H

#define	MAXSYMNMLEN	256		/* name incl. NUL */

#define	KSYM_MAGIC	0x4b53594dL	/* 'KSYM' */
#define	KSYM_VERSION	1
#ifndef KSYM_SPACE
#define	KSYM_SPACE	(160 * 1024)
#endif

struct ksymhdr {
	long	kh_magic;
	long	kh_version;
	long	kh_nsym;	/* Elf32_Sym entries, [0] is null */
	long	kh_symoff;
	long	kh_stroff, kh_strsize;
	long	kh_hashoff;	/* nbucket, nchain, bucket[], chain[] */
	long	kh_lo, kh_hi;	/* image range */
	long	kh_size;	/* bytes used */
};

/* byte offsets of the header words */
#define	KH_MAGIC	0
#define	KH_VERSION	4
#define	KH_NSYM		8
#define	KH_SYMOFF	12
#define	KH_STROFF	16
#define	KH_STRSIZE	20
#define	KH_HASHOFF	24
#define	KH_LO		28
#define	KH_HI		32
#define	KH_SIZE		36
#define	KSYMHDRSZ	40

#if !defined(_KERNEL) && !defined(DLM_HOST)
extern int getksym();
#endif

#endif	/* _SYS_KSYM_H */
