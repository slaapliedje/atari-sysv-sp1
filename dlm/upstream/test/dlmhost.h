/*
 * dlmhost.h -- host (LP64) environment for dlm_sym.c, dlm_ld.c and
 * dlm_core.c: the test harnesses (-DDLM_HOST) and mkksym (-DDLM_TOOL).
 * The kernel services dlm_core.c uses are simulated in hostk.c.
 */

#ifndef _DLMHOST_H
#define _DLMHOST_H

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <setjmp.h>
#include <errno.h>

#define	_VOID		void

#define	bcopy(s, d, n)	memmove((d), (s), (n))
#define	bzero(p, n)	memset((p), 0, (n))

#ifdef DLM_HOST
/* AMIX errno values the core returns */
#undef	ENOSYS
#undef	ERESTART
#undef	ENAMETOOLONG
#define	ENAMETOOLONG	78
#define	ENOSYS		89

typedef struct { jmp_buf jb; } label_t;
typedef struct { int r_val1, r_val2; } rval_t;

struct cred {
	unsigned short	cr_ref, cr_ngroups;
	long		cr_uid, cr_gid, cr_ruid, cr_rgid, cr_suid, cr_sgid;
};
struct proc { struct cred *p_cred; };
struct vnode { FILE *v_fp; };
struct user {
	struct proc	*u_procp;
	struct vnode	*u_rdir;
	label_t		u_qsav;
};
#define	u_cred	u_procp->p_cred
extern struct user u;

struct sysent {
	char	sy_narg, sy_flags;
	int	(*sy_call)();
};
extern struct sysent sysent[];
extern unsigned sysentsize;
#define	SETJUMP	1

enum uio_rw { UIO_READ, UIO_WRITE };
enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE };
enum create { CRCREAT };
#define	FREAD		1
#define	PZERO		25
#define	HZ		60
#define	KM_SLEEP	0
#define	CE_CONT		0
#define	CE_NOTE		1
#define	CE_WARN		2
#define	CE_PANIC	3

#define	VOP_CLOSE(vp, f, c, o, cr)	host_close(vp)
#define	VN_RELE(vp)

extern long lbolt;
extern char *kmem_zalloc();
extern void kmem_free();
extern int copyin(), copyout(), copyinstr(), suser();
extern int vn_open(), vn_rdwr(), host_close();
extern int sleep();
extern void wakeup(), cmn_err();
extern struct cred *crget();
extern int nosys(), nodev();

struct execsw {
	short	*exec_magic;
	int	(*exec_func)();
	int	(*exec_core)();
};
extern int nexectype;

struct cdevsw {
	int	(*d_open)();
	int	(*d_close)();
	int	(*d_read)();
	int	(*d_write)();
	int	(*d_ioctl)();
	int	(*d_mmap)();
	int	(*d_segmap)();
	int	(*d_poll)();
	int	(*d_xpoll)();
	int	(*d_xhalt)();
	struct tty *d_ttys;
	struct streamtab *d_str;
	int	*d_flag;
};
extern struct cdevsw cdevsw[];
extern int cdevcnt;
#define	D_OLD		0x01
#define	OTYP_CHR	2
#define	OTYP_LYR	4
#define	getmajor(d)	((int)(((unsigned long)(d) >> 18) & 0x3fff))
#define	getminor(d)	((int)((unsigned long)(d) & 0x3ffff))
#define	makedevice(j, n) ((dev_t)(((unsigned long)(j) << 18) | (n)))

/* image and kernel addresses: see hostk.c */
extern char *host_rp();
extern unsigned long host_runaddr();
extern int host_call();
extern unsigned long host_kaddr();
extern unsigned long host_klo, host_khi;
#define	DLM_RP(m, a)		host_rp(m, (unsigned long)(a))
#define	DLM_RUNADDR(m)		host_runaddr(m)
#define	DLM_CALL(m, f, a, b)	host_call(m, (unsigned long)(f), (char *)(a), (long)(b))
#define	DLM_KADDR(p, name)	host_kaddr((char *)(p), name)
#define	DLM_SETJMP(l)		setjmp((l)->jb)
#define	DLM_LONGJMP(l)		longjmp((l)->jb, 1)
#define	DLM_CALL8(m, f, a)	host_call(m, (unsigned long)(f), (char *)(a)[0], (long)(a)[1])
#define	DLM_KLO			host_klo
#define	DLM_KHI			host_khi
#endif

#endif	/* _DLMHOST_H */
