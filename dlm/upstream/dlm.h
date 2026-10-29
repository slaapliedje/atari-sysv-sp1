/*
 * dlm.h -- private declarations of the loadable-module core.
 *
 * dlm_sym.c and dlm_ld.c build three ways: in the kernel (AMIX cc
 * flags), on the host for the test harnesses (-DDLM_HOST), and inside
 * mkksym (-DDLM_TOOL, table code only).  ELF data and symbol tables are
 * read and written by byte offset, never by struct overlay, so the host
 * builds work on LP64.  Addresses are unsigned long; only the low 32
 * bits are meaningful.
 */

#ifndef _DLM_H
#define _DLM_H

#if defined(DLM_HOST) || defined(DLM_TOOL)
#include "test/dlmhost.h"
#else
#include "sys/types.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/immu.h"
#include "sys/signal.h"
#include "sys/fs/s5dir.h"
#include "sys/psw.h"
#include "sys/pcb.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/cred.h"
#include "sys/systm.h"
#include "sys/vnode.h"
#include "sys/uio.h"
#include "sys/file.h"
#include "sys/errno.h"
#include "sys/cmn_err.h"
#include "sys/kmem.h"
extern char stext[], end[];
extern char *strcpy(), *strncpy();
extern int strcmp(), strlen();
extern void bcopy(), bzero();
#endif

#include "sys/ksym.h"
#include "sys/mod.h"
#include "sys/moddefs.h"

/* big-endian access */
#define	G8(p)	((unsigned long)((unsigned char *)(p))[0])
#define	G16(p)	(G8(p) << 8 | G8((char *)(p) + 1))
#define	G32(p)	(G16(p) << 16 | G16((char *)(p) + 2))
#define	P8(p, v)  (((unsigned char *)(p))[0] = (v) & 0xff)
#define	P16(p, v) (P8(p, (v) >> 8), P8((char *)(p) + 1, v))
#define	P32(p, v) (P16(p, (v) >> 16), P16((char *)(p) + 2, v))
/* low 32 bits as a signed value */
#define	S32(x)	((x) & 0x80000000L ? -(long)((~(x) & 0x7fffffffL) + 1) \
			: (long)((x) & 0x7fffffffL))
#define	M32(x)	((x) & 0xffffffffL)

/* ELF32 */
#define	EHSZ		52
#define	SHSZ		40
#define	SYMSZ		16
#define	RELASZ		12
#define	ELFCLASS32	1
#define	ELFDATA2MSB	2
#define	ET_REL		1
#define	EM_68K		4
#define	SHT_PROGBITS	1
#define	SHT_SYMTAB	2
#define	SHT_STRTAB	3
#define	SHT_RELA	4
#define	SHT_NOBITS	8
#define	SHT_REL		9
#define	SHT_DLMMOD	13
#define	SHF_WRITE	0x1
#define	SHF_ALLOC	0x2
#define	SHN_UNDEF	0
#define	SHN_LORESERVE	0xff00
#define	SHN_ABS		0xfff1
#define	SHN_COMMON	0xfff2
#define	STB_LOCAL	0
#define	STB_GLOBAL	1
#define	STB_WEAK	2
#define	STT_NOTYPE	0
#define	STT_OBJECT	1
#define	STT_FUNC	2
#define	STT_SECTION	3
#define	STT_FILE	4
#define	ST_BIND(i)	((i) >> 4)
#define	ST_TYPE(i)	((i) & 0xf)

/* section header fields */
#define	SH_NAME		0
#define	SH_TYPE		4
#define	SH_FLAGS	8
#define	SH_ADDR		12
#define	SH_OFFSET	16
#define	SH_SIZE		20
#define	SH_LINK		24
#define	SH_INFO		28
#define	SH_ALIGN	32
#define	SH_ENTSIZE	36

/* symbol fields */
#define	ST_NAME		0
#define	ST_VALUE	4
#define	ST_SIZE		8
#define	ST_INFO		12
#define	ST_OTHER	13
#define	ST_SHNDX	14

/* relocation types */
#define	R_68K_NONE	0
#define	R_68K_32	1
#define	R_68K_16	2
#define	R_68K_8		3
#define	R_68K_PC32	4
#define	R_68K_PC16	5
#define	R_68K_PC8	6

#define	DLM_MODBUCKETS	101	/* module table hash */
#define	DLM_MAXDEPTH	8	/* dependency recursion */
#define	DLM_ESHORT	(-1)	/* read callback: end of file */

/* one resolved symbol of the file being loaded */
struct dlm_lsym {
	unsigned long	ls_val;		/* offset in image, or absolute */
	char		ls_kind;
	char		ls_tab;		/* goes into the module table */
};
#define	LS_NONE		0	/* not resolved: using it is ERELOC */
#define	LS_REL		1	/* image offset */
#define	LS_ABS		2	/* absolute */

/* loader state for one file */
struct dlm_ld {
	struct dlm_ld	*ld_next;	/* guard chain, innermost first */
	int		(*ld_read)();	/* (ld, off, buf, len): errno,
					 * DLM_ESHORT or 0 */
	char		*ld_rh;		/* read handle */
	char		*ld_name;	/* for messages */
	/* section headers */
	int		ld_shnum;
	char		*ld_sh;
	unsigned long	*ld_secoff;	/* image offset, or NOSEC */
	int		ld_symsec, ld_modsec;
	/* symbols */
	long		ld_nsym;
	char		*ld_syms;
	long		ld_strsz;
	char		*ld_str;
	struct dlm_lsym	*ld_ls;
	int		ld_nundef;
	/* type-13 contents as read from the file */
	char		*ld_mod;
	long		ld_modsz;
	/* resolution: dependency tables, searched last first */
	char		**ld_deptab;
	int		ld_ndep;
	char		*ld_ktab;	/* static kernel */
	long		(*ld_comhook)(); /* test only: common placement */
	/* image */
	unsigned long	ld_comoff;	/* start of commons */
	unsigned long	ld_imgsz;	/* incl. commons */
	char		*ld_img;	/* where the bytes are written */
	unsigned long	ld_base;	/* run address of ld_img[0] */
	/* module table */
	char		*ld_tab;
	long		ld_tabsz;
	char		*ld_rbuf;	/* relocation chunk */
};
#define	NOSEC		0xffffffffL

/* a loaded module */
struct dlm_mod {
	struct dlm_mod	*m_next;	/* load order */
	struct dlm_mod	*m_cnext, *m_cprev; /* candidate list */
	int		m_id;
	int		m_flags;
	char		*m_owner;	/* loading/unloading process */
	int		m_refs, m_deps, m_incall;
	long		m_stamp;	/* lbolt when it became a candidate */
	long		m_delay;	/* unload delay, ticks */
	char		m_name[MODMAXNAMELEN];
	char		*m_path;
	int		m_pathsz;
	char		*m_alloc;	/* kmem block of the image */
	unsigned long	m_allocsz;
	char		*m_base;	/* image, 16-aligned */
	unsigned long	m_run;		/* its run address */
	unsigned long	m_size;
	char		*m_tab;		/* symbol table block */
	long		m_tabsz;
	unsigned long	m_wrapper;	/* run address */
	struct dlm_mod	**m_dep;	/* direct loadable dependencies */
	int		m_ndep, m_maxdep;
	int		m_nlink;	/* linkages installed */
};

/* m_flags */
#define	DM_LOADING	0x01
#define	DM_UNLOADING	0x02
#define	DM_DEMAND	0x04
#define	DM_CAND		0x08
#define	DM_LOCKED	0x10	/* profiler */
#define	DM_SYMOK	0x20	/* table usable */
#define	DM_WANTED	0x40	/* someone sleeps on the record */
#define	DM_TRANS	(DM_LOADING | DM_UNLOADING)

/* dlm_load() flags */
#define	DL_SYS		0x01	/* system credentials and root */
#define	DL_DEMAND	0x02	/* set the demand mark */

/* dlm_unload() callers */
#define	DU_DEMAND	0
#define	DU_AUTO		1

/* per system call guard */
struct dlm_guard {
	struct dlm_lf	*g_lf;		/* active loads, innermost first */
	struct dlm_mod	*g_unl;		/* module being unloaded */
	int		g_unlnl;	/* its linkages removed so far */
};

/*
 * Image access.  In the kernel the image runs where it lies; the host
 * harnesses give it a separate 32-bit run address.
 */
#ifndef DLM_RP
#define	DLM_RP(m, a)	((char *)(a))		/* run address -> bytes */
#define	DLM_RUNADDR(m)	((unsigned long)(m)->m_base)
#define	DLM_CALL(m, f, a, b)	(*(int (*)())(f))(a, b)
#define	DLM_KADDR(p, name)	((unsigned long)(p))
#define	DLM_SETJMP(l)	setjmp(l)
#define	DLM_LONGJMP(l)	longjmp(l)
#define	DLM_CALL8(m, f, a) \
	(*(int (*)())(f))(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7])
#define	DLM_KLO		((unsigned long)stext)
#define	DLM_KHI		((unsigned long)end)
#endif

/* wrapper and linkage words */
#define	MW_REV		0
#define	MW_LOAD		4
#define	MW_UNLOAD	8
#define	MW_HALT		12
#define	MW_CONF		16
#define	MW_MODLINK	20
#define	MWSZ		24
#define	MLSZ		8

/* system call arguments */
struct modloada { char *path; };
struct moduloada { int id; };
struct modpatha { char *path; };
struct modstata { int id; struct modstatus *st; int next; };
struct modadma { int type; int cmd; caddr_t arg; };
struct getksyma { char *name; unsigned long *value; unsigned long *info; };

/* dlm_sym.c */
extern unsigned long dlm_elfhash();
extern long dlm_blksize();
extern void dlm_blkinit();
extern void dlm_blkhash();
extern int dlm_blkcheck();
extern int dlm_blklookup();
extern int dlm_blkaddr();
extern int dlm_prime();

/* dlm_core.c */
extern int dlm_load();
extern int dlm_unload();
extern void dlm_abort();
extern void dlm_hold();
extern void dlm_rele();
extern void dlm_depadd();
extern void dlm_deprele();
extern struct dlm_mod *dlm_list;
extern struct dlm_mod *dlm_cand;
extern int dlm_nextid;
extern int dlm_inited;
extern char *dlm_ktab;
extern void dlm_init();

/* dlm_ld.c */
extern int dlm_ld_hdr();
extern int dlm_ld_moddata();
extern int dlm_ld_dep();
extern int dlm_ld_syms();
extern int dlm_ld_image();
extern int dlm_ld_reloc();
extern int dlm_ld_table();
extern void dlm_ld_free();

/* services the loader needs from its environment */
extern char *dlm_zalloc();
extern void dlm_free();
extern void dlm_undef();

/* dlm_slot.c */
extern void dlm_slot_init();
extern int dlm_xreg();
extern int dlm_creg(), dlm_sreg();
extern int dlm_autoload();
extern int dlm_loadname();
extern struct bootmod *dlm_bootfind();

/* dlm_str.c */
extern void dlm_str_init();

/* dlm_fs.c */
extern void dlm_fs_init();
extern int dlm_fsbusy();

/* dlmconf.c */
extern long dlm_maximage;
extern int dlm_verbose;
extern int dlm_def_unload_delay;
extern int dlm_unload_wake;
extern char *dlm_static[];

#endif	/* _DLM_H */
