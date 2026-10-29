/*
 * Loadable kernel modules: system calls, registration and status.
 * Needs <sys/types.h>.
 */

#ifndef _SYS_MOD_H
#define _SYS_MOD_H

/* system call numbers */
#define	SYS_modload	64
#define	SYS_moduload	65
#define	SYS_modpath	66
#define	SYS_modstat	67
#define	SYS_modadm	68
#define	SYS_getksym	69

/* errno values: free in AMIX (max 151) and ASV (uses 152-163) */
#ifndef ENOLOAD
#define	ENOLOAD		164	/* a required module could not be loaded */
#define	ERELOC		165	/* object-file, symbol or relocation error */
#define	ENOMATCH	166	/* symbol not found */
#define	EBADVER		167	/* wrapper revision mismatch */
#define	ECONFIG		168	/* configured kernel resource exhausted */
#endif

/* modadm types */
#define	MOD_TY_NONE	0
#define	MOD_TY_CDEV	1
#define	MOD_TY_BDEV	2
#define	MOD_TY_STR	3
#define	MOD_TY_FS	4
#define	MOD_TY_SDEV	5
#define	MOD_TY_MISC	6
#define	MOD_TY_EXEC	7
#define	MOD_TY_SYS	8
#define	MOD_TY_MAX	8

/* modadm commands */
#define	MOD_C_MREG	1
#define	MOD_C_AUTOUNLD	100

#define	EXF_FIRST	1		/* mod_execreg er_flags */

#define	MODMAXNAMELEN	15		/* 14 + NUL */
#define	MODMAXLINK	4
#define	MODMAXLINKINFOLEN 32

#ifndef MAXPATHLEN
#define	MAXPATHLEN	1024
#endif

struct mod_mreg {
	char	md_modname[MODMAXNAMELEN];
	caddr_t	md_typedata;
};

struct mod_execreg {
	short	er_magic;
	short	er_flags;
};

struct mod_sysreg {
	int	sr_num;
	char	sr_narg;
	char	sr_flags;
};

struct modspecific_stat {
	char	mss_linkinfo[MODMAXLINKINFOLEN];
	int	mss_type;
	int	mss_p0[2];
	int	mss_p1[2];
};

struct modstatus {
	int	ms_id;
	caddr_t	ms_base;
	u_int	ms_size;
	int	ms_rev;
	char	ms_path[MAXPATHLEN];
	time_t	ms_unload_delay;	/* seconds */
	int	ms_refcnt;
	int	ms_depcnt;
	struct modspecific_stat ms_msinfo[MODMAXLINK];
	char	ms_name[MODMAXNAMELEN];
	int	ms_flags;
};

/* ms_flags */
#define	MS_DEMAND	1
#define	MS_LOCKED	2
#define	MS_CAND		4

#if !defined(_KERNEL) && !defined(DLM_HOST)
extern int modload();
extern int moduload();
extern int modpath();
extern int modstat();
extern int modadm();
#endif

#endif	/* _SYS_MOD_H */
