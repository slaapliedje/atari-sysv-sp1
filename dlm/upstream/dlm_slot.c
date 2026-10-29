/*
 * dlm_slot.c -- exec-format, hook and character-driver linkages.
 *
 * execsw is replaced by a larger table; dlm_slot_init copies the stock
 * rows in from __amix_execsw.  A registered magic gets a row whose
 * exec_func is the trampoline of its slot, bound to the slot rather
 * than the row, so rows can move.  The trampoline loads the module on
 * first use and holds it across the call.  An EXF_FIRST slot sits
 * before the rows it shadows and falls through to the next row with
 * the same magic when its module declines (ENOEXEC) or cannot be
 * loaded:
 *
 *	execsw:	[0x150 slot 0, EXF_FIRST] -> auxexec, else
 *		[0x150 coffexec] [elf] [#!] ...
 *
 * Hooks: each entry of a module's hook data sets one hooksw pointer,
 * which the static shims call through.  Removal restores the default.
 *
 * Character drivers: a registered major gets a placeholder row whose
 * d_open and d_close are the loader's trampolines.  The first open
 * loads the module, which fills the other entries.  The module is held
 * once per (dev, otyp) it has open, as its driver sees them; layered
 * opens hold per call.
 *
 * K&R C.
 */

#include "dlm.h"
#ifndef DLM_HOST
#include "sys/exec.h"
#include "sys/conf.h"
#include "sys/open.h"
#endif

#define	EXEC_STATIC	3
#define	EXEC_RESERVE	8
#define	NEXECSW		(EXEC_STATIC + EXEC_RESERVE)
#define	EXDSZ		12		/* struct mod_exec_data */
#define	HKDSZ		8		/* struct mod_hook_data */

struct execsw	execsw[NEXECSW] = { { 0 } };	/* in .data, as the stock table */
extern struct execsw __amix_execsw[];
extern struct hooksw hooksw[];

struct dlm_xslot {
	char		xs_name[MODMAXNAMELEN];
	short		xs_magic;	/* the row's exec_magic points here */
	short		xs_flags;
	int		(*xs_tramp)();
	struct dlm_mod	*xs_mod;	/* installed module */
	unsigned long	xs_func;	/* its exec function */
};

static int xcall();

#define	XBODY(n) \
	long a[8]; \
	a[0] = a0; a[1] = a1; a[2] = a2; a[3] = a3; \
	a[4] = a4; a[5] = a5; a[6] = a6; a[7] = a7; \
	return xcall(&dlm_xslot[n], a)
#define	XARGS	a0, a1, a2, a3, a4, a5, a6, a7
#define	XDECL	long a0, a1, a2, a3, a4, a5, a6, a7;

static int x0(), x1(), x2(), x3(), x4(), x5(), x6(), x7();

struct dlm_xslot dlm_xslot[EXEC_RESERVE] = {
	{ "", 0, 0, x0 }, { "", 0, 0, x1 }, { "", 0, 0, x2 }, { "", 0, 0, x3 },
	{ "", 0, 0, x4 }, { "", 0, 0, x5 }, { "", 0, 0, x6 }, { "", 0, 0, x7 },
};

static int x0(XARGS) XDECL { XBODY(0); }
static int x1(XARGS) XDECL { XBODY(1); }
static int x2(XARGS) XDECL { XBODY(2); }
static int x3(XARGS) XDECL { XBODY(3); }
static int x4(XARGS) XDECL { XBODY(4); }
static int x5(XARGS) XDECL { XBODY(5); }
static int x6(XARGS) XDECL { XBODY(6); }
static int x7(XARGS) XDECL { XBODY(7); }

/* at boot, before the first exec, whether or not the DLM comes up */
void
dlm_slot_init()
{
	int i;

	if (nexectype > NEXECSW)
		nexectype = NEXECSW;
	for (i = 0; i < nexectype; i++)
		execsw[i] = __amix_execsw[i];
}

static int
inimg(m, a, n)
	struct dlm_mod *m;
	unsigned long a, n;
{
	return a >= m->m_run && n <= m->m_size && a - m->m_run <= m->m_size - n;
}

static int
ismagic(i, mg)
	int i, mg;
{
	return execsw[i].exec_magic && (*execsw[i].exec_magic & 0xffff) == mg;
}

static int
rowof(f)
	int (*f)();
{
	int i;

	for (i = 0; i < nexectype; i++)
		if (execsw[i].exec_func == f)
			return i;
	return -1;
}

/* the first row after the slot's with the same magic */
static int
xnext(xs, a)
	struct dlm_xslot *xs;
	long *a;
{
	int i, mg = xs->xs_magic & 0xffff;

	for (i = rowof(xs->xs_tramp) + 1; i > 0 && i < nexectype; i++)
		if (ismagic(i, mg))
			return (*execsw[i].exec_func)(a[0], a[1], a[2], a[3],
			    a[4], a[5], a[6], a[7]);
	return ENOEXEC;
}

static int
xcall(xs, a)
	struct dlm_xslot *xs;
	long *a;
{
	struct dlm_guard g;
	struct { struct dlm_mod *m; } h;	/* in memory: read after a longjmp */
	label_t save;
	struct dlm_mod *m;
	int e;

	bzero((caddr_t)&g, sizeof g);
	bzero((caddr_t)&h, sizeof h);
	bcopy((caddr_t)&u.u_qsav, (caddr_t)&save, sizeof (label_t));
	if (DLM_SETJMP(&u.u_qsav)) {
		bcopy((caddr_t)&save, (caddr_t)&u.u_qsav, sizeof (label_t));
		dlm_abort(&g);
		if (h.m)
			dlm_rele(h.m);
		DLM_LONGJMP(&u.u_qsav);
	}
	e = 0;
	if (xs->xs_mod == 0 &&
	    ((e = dlm_load(xs->xs_name, DL_SYS, 1, &g, &m)) != 0 || xs->xs_mod == 0))
		e = ENOEXEC;
	if (e == 0) {
		h.m = m = xs->xs_mod;
		dlm_hold(m);
		e = DLM_CALL8(m, xs->xs_func, a);
		h.m = 0;
		dlm_rele(m);
	}
	bcopy((caddr_t)&save, (caddr_t)&u.u_qsav, sizeof (label_t));
	if (e == ENOEXEC && (xs->xs_flags & EXF_FIRST))
		e = xnext(xs, a);
	return e;
}

/*
 * modadm(MOD_TY_EXEC): a slot for (name, magic).  EXF_FIRST puts its
 * row before the first row with that magic; otherwise it is appended,
 * which a static row of the same magic would shadow (EEXIST).
 */
int
dlm_xreg(name, er)
	char *name;
	struct mod_execreg *er;
{
	struct dlm_xslot *xs, *fr = 0;
	int (*core)();
	int i, k, mg = er->er_magic & 0xffff;

	for (k = 0; k < EXEC_RESERVE; k++) {
		xs = &dlm_xslot[k];
		if (xs->xs_name[0] == 0) {
			if (fr == 0)
				fr = xs;
		} else if ((xs->xs_magic & 0xffff) == mg)
			return strcmp(xs->xs_name, name) == 0 ? 0 : EEXIST;
	}
	for (i = 0; i < nexectype && !ismagic(i, mg); i++)
		;
	if (i < nexectype && !(er->er_flags & EXF_FIRST))
		return EEXIST;
	if (fr == 0 || nexectype >= NEXECSW)
		return ECONFIG;
	core = i < nexectype ? execsw[i].exec_core : (int (*)())nodev;
	for (k = nexectype; k > i; k--)
		execsw[k] = execsw[k - 1];
	strcpy(fr->xs_name, name);
	fr->xs_magic = er->er_magic;
	fr->xs_flags = er->er_flags & EXF_FIRST;
	execsw[i].exec_magic = &fr->xs_magic;
	execsw[i].exec_func = fr->xs_tramp;
	execsw[i].exec_core = core;
	nexectype++;
	return 0;
}

/* ---- exec linkage ---- */

static struct dlm_xslot *
xfind(m, mg)
	struct dlm_mod *m;
	int mg;
{
	int k;

	for (k = 0; k < EXEC_RESERVE; k++)
		if ((dlm_xslot[k].xs_magic & 0xffff) == mg &&
		    strcmp(dlm_xslot[k].xs_name, m->m_name) == 0)
			return &dlm_xslot[k];
	return 0;
}

/* td: run address of the mod_type_data; its mtd_pdata is the array */
static int
exec_install(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	struct dlm_xslot *xs;
	unsigned long d, d0, f;

	if (!inimg(m, td, 8L))
		return ERELOC;
	d0 = G32(DLM_RP(m, td) + 4);
	for (d = d0;; d += EXDSZ) {
		if (!inimg(m, d, (unsigned long)EXDSZ))
			return ERELOC;
		if ((f = G32(DLM_RP(m, d) + 4)) == 0)
			break;
		if (!inimg(m, f, 2L))
			return ERELOC;
		xs = xfind(m, (int)G16(DLM_RP(m, d)));
		if (xs == 0 || (xs->xs_mod && xs->xs_mod != m))
			return EINVAL;
	}
	if (d == d0)
		return EINVAL;
	for (d = d0; (f = G32(DLM_RP(m, d) + 4)) != 0; d += EXDSZ) {
		xs = xfind(m, (int)G16(DLM_RP(m, d)));
		xs->xs_mod = m;
		xs->xs_func = f;
	}
	return 0;
}

static int
exec_remove(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	int k;

	for (k = 0; k < EXEC_RESERVE; k++)
		if (dlm_xslot[k].xs_mod == m) {
			dlm_xslot[k].xs_mod = 0;
			dlm_xslot[k].xs_func = 0;
		}
	return 0;
}

static void
exec_info(m, td, st)
	struct dlm_mod *m;
	unsigned long td;
	struct modspecific_stat *st;
{
	int k;

	st->mss_type = MOD_TY_EXEC;
	st->mss_p0[0] = st->mss_p0[1] = st->mss_p1[0] = st->mss_p1[1] = -1;
	for (k = 0; k < EXEC_RESERVE; k++)
		if (dlm_xslot[k].xs_mod == m) {
			st->mss_p0[0] = dlm_xslot[k].xs_magic & 0xffff;
			st->mss_p0[1] = rowof(dlm_xslot[k].xs_tramp);
			break;
		}
}

struct mod_operations mod_execops = { exec_install, exec_remove, exec_info };

/* ---- hook linkage ---- */

/* the hooksw row named by the module string at run address a */
static struct hooksw *
hkfind(m, a)
	struct dlm_mod *m;
	unsigned long a;
{
	struct hooksw *h;
	char buf[32];
	int i;

	for (i = 0; i < sizeof buf - 1 && inimg(m, a + i, 1L); i++)
		if ((buf[i] = *DLM_RP(m, a + i)) == 0)
			break;
	if (i == 0 || i == sizeof buf - 1 || buf[i] != 0)
		return 0;
	for (h = hooksw; h->hk_name; h++)
		if (strcmp(h->hk_name, buf) == 0)
			return h;
	return 0;
}

static int
hook_install(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	struct hooksw *h;
	unsigned long d, d0, n, f;

	if (!inimg(m, td, 8L))
		return ERELOC;
	d0 = G32(DLM_RP(m, td) + 4);
	for (d = d0;; d += HKDSZ) {
		if (!inimg(m, d, (unsigned long)HKDSZ))
			return ERELOC;
		if ((n = G32(DLM_RP(m, d))) == 0)
			break;
		f = G32(DLM_RP(m, d) + 4);
		if (!inimg(m, f, 2L))
			return ERELOC;
		if ((h = hkfind(m, n)) == 0)
			return EINVAL;
		if (h->hk_owner && h->hk_owner != (char *)m)
			return EEXIST;
	}
	for (d = d0; (n = G32(DLM_RP(m, d))) != 0; d += HKDSZ) {
		h = hkfind(m, n);
		h->hk_owner = (char *)m;
		*h->hk_ptr = (char *)G32(DLM_RP(m, d) + 4);
	}
	return 0;
}

static int
hook_remove(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	struct hooksw *h;

	for (h = hooksw; h->hk_name; h++)
		if (h->hk_owner == (char *)m) {
			*h->hk_ptr = h->hk_dflt;
			h->hk_owner = 0;
		}
	return 0;
}

static void
hook_info(m, td, st)
	struct dlm_mod *m;
	unsigned long td;
	struct modspecific_stat *st;
{
	st->mss_type = MOD_TY_MISC;
	st->mss_p0[0] = st->mss_p0[1] = st->mss_p1[0] = st->mss_p1[1] = -1;
}

struct mod_operations mod_hookops = { hook_install, hook_remove, hook_info };

/* ---- character-driver linkage ---- */

#define	CDEV_NOPEN	32
#define	SDEV_MAX	4		/* majors of one STREAMS driver */
#define	DRVSZ		100		/* struct mod_drv_data */
#define	DRV_BCOUNT	36
#define	DRV_CDEVSW	40
#define	DRV_CMAJOR	92
#define	DRV_CCOUNT	96
#define	CD_NFN		10		/* d_open .. d_xhalt */
#define	CD_TTYS		40
#define	CD_STR		44
#define	CD_FLAG		48

struct dlm_cslot {
	struct dlm_cslot *cs_next;
	char		cs_name[MODMAXNAMELEN];
	int		cs_major;
	struct dlm_mod	*cs_mod;		/* installed module */
	unsigned long	cs_open, cs_close;	/* its entries */
	int		cs_nopen;
	struct {
		dev_t	o_dev;
		int	o_otyp;
	} cs_set[CDEV_NOPEN];
};

static struct dlm_cslot *dlm_cslot;
int dlm_cflag[1];		/* the placeholder's d_flag */
int dlm_stale_close;		/* closes of keys never opened */

#ifdef DLM_HOST
#define	dlm_splhi()	0
#define	dlm_splx(s)
#else
static int
dlm_splhi()
{
	int s;

	__asm__ __volatile__("mov.w %%sr,%0" : "=d" (s) : : "memory");
	__asm__ __volatile__("mov.w %0,%%sr" : : "d" (s | 0x700) : "memory");
	return s;
}

static void
dlm_splx(s)
	int s;
{
	__asm__ __volatile__("mov.w %0,%%sr" : : "d" (s) : "memory");
}
#endif

static struct dlm_cslot *
cslot(mj)
	int mj;
{
	struct dlm_cslot *cs;

	for (cs = dlm_cslot; cs; cs = cs->cs_next)
		if (cs->cs_major == mj)
			return cs;
	return 0;
}

/* the ten routine entries, d_open first */
#define	cfn(cp)	((int (**)())&(cp)->d_open)

static int
cempty(cp)
	struct cdevsw *cp;
{
	int k;

	for (k = 0; k < CD_NFN; k++)
		if (cfn(cp)[k] != nodev)
			return 0;
	return cp->d_ttys == 0 && cp->d_str == 0 && (cp->d_flag == 0 || *cp->d_flag == 0);
}

int dlm_cdev_open(), dlm_cdev_close();
extern int dlm_sdev_install();
extern void dlm_sdev_remove(), dlm_sdev_info();

int
dlm_cempty(cp)
	struct cdevsw *cp;
{
	return cempty(cp);
}

/* the placeholder: trampolines, everything else nodev */
static void
cplace(cp)
	struct cdevsw *cp;
{
	int k;

	for (k = 0; k < CD_NFN; k++)
		cfn(cp)[k] = nodev;
	cp->d_open = dlm_cdev_open;
	cp->d_close = dlm_cdev_close;
	cp->d_ttys = 0;
	cp->d_str = 0;
	cp->d_flag = dlm_cflag;
}

/* modadm(MOD_TY_CDEV): a slot for (name, major) over an empty row */
int
dlm_creg(name, mj)
	char *name;
	int mj;
{
	struct dlm_cslot *cs, *fr;
	int s, error;

	if (mj < 0 || mj >= cdevcnt)
		return ECONFIG;
	if ((cs = cslot(mj)) != 0)
		return strcmp(cs->cs_name, name) == 0 ? 0 : EEXIST;
	if (!cempty(&cdevsw[mj]))
		return EEXIST;
	fr = (struct dlm_cslot *)dlm_zalloc((long)sizeof *fr);
	if (!fr)
		return ENOMEM;
	strcpy(fr->cs_name, name);
	fr->cs_major = mj;
	/* Allocation may sleep while another caller registers this major. */
	s = dlm_splhi();
	if (mj < 0 || mj >= cdevcnt)
		error = ECONFIG;
	else if ((cs = cslot(mj)) != 0)
		error = strcmp(cs->cs_name, name) == 0 ? 0 : EEXIST;
	else if (!cempty(&cdevsw[mj]))
		error = EEXIST;
	else {
		fr->cs_next = dlm_cslot;
		dlm_cslot = fr;
		cplace(&cdevsw[mj]);
		dlm_splx(s);
		return 0;
	}
	dlm_splx(s);
	dlm_free((char *)fr, (long)sizeof *fr);
	return error;
}

static int
cfind(cs, dev, otyp)
	struct dlm_cslot *cs;
	dev_t dev;
	int otyp;
{
	int i;

	for (i = 0; i < cs->cs_nopen; i++)
		if (cs->cs_set[i].o_dev == dev && cs->cs_set[i].o_otyp == otyp)
			return i;
	return -1;
}

/*
 * cdevsw d_open of a registered major: load if needed, then the
 * driver's open under a hold that stays while the key is open.
 */
int
dlm_cdev_open(devp, flag, otyp, cr)
	dev_t *devp;
	int flag, otyp;
	struct cred *cr;
{
	struct dlm_cslot *cs = cslot((int)getmajor(*devp));
	struct dlm_guard g;
	struct { struct dlm_mod *m; } h;	/* in memory: read after a longjmp */
	label_t save;
	struct dlm_mod *m;
	long a[8];
	int e;

	if (cs == 0)
		return ENXIO;
	bzero((caddr_t)&g, sizeof g);
	bzero((caddr_t)&h, sizeof h);
	bcopy((caddr_t)&u.u_qsav, (caddr_t)&save, sizeof (label_t));
	if (DLM_SETJMP(&u.u_qsav)) {
		bcopy((caddr_t)&save, (caddr_t)&u.u_qsav, sizeof (label_t));
		dlm_abort(&g);
		if (h.m)
			dlm_rele(h.m);
		DLM_LONGJMP(&u.u_qsav);
	}
	e = 0;
	if (cs->cs_mod == 0 &&
	    ((e = dlm_load(cs->cs_name, DL_SYS, 1, &g, &m)) != 0 || cs->cs_mod == 0))
		e = ENXIO;
	if (e == 0) {
		h.m = m = cs->cs_mod;
		dlm_hold(m);
		a[0] = (long)devp;
		a[1] = flag;
		a[2] = otyp;
		a[3] = (long)cr;
		a[4] = a[5] = a[6] = a[7] = 0;
		e = DLM_CALL8(m, cs->cs_open, a);
		if (e == 0 && otyp != OTYP_LYR && cfind(cs, *devp, otyp) < 0) {
			if (cs->cs_nopen < CDEV_NOPEN) {
				cs->cs_set[cs->cs_nopen].o_dev = *devp;
				cs->cs_set[cs->cs_nopen++].o_otyp = otyp;
			}			/* else the hold stays for good */
		} else if (e != 0 || otyp != OTYP_LYR)
			dlm_rele(m);
		h.m = 0;
	}
	bcopy((caddr_t)&save, (caddr_t)&u.u_qsav, sizeof (label_t));
	return e;
}

/* Loads module name for a static driver that calls through a pointer it sets. */
int
dlm_loadname(name)
	char *name;
{
	struct dlm_guard g;
	label_t save;
	struct dlm_mod *m;
	int e;

	bzero((caddr_t)&g, sizeof g);
	bcopy((caddr_t)&u.u_qsav, (caddr_t)&save, sizeof (label_t));
	if (DLM_SETJMP(&u.u_qsav)) {
		bcopy((caddr_t)&save, (caddr_t)&u.u_qsav, sizeof (label_t));
		dlm_abort(&g);
		DLM_LONGJMP(&u.u_qsav);
	}
	e = dlm_load(name, DL_SYS, 1, &g, &m);
	bcopy((caddr_t)&save, (caddr_t)&u.u_qsav, sizeof (label_t));
	return e;
}

/* cdevsw d_close: the driver's close, then the key's hold goes */
int
dlm_cdev_close(dev, flag, otyp, cr)
	dev_t dev;
	int flag, otyp;
	struct cred *cr;
{
	struct dlm_cslot *cs = cslot((int)getmajor(dev));
	struct dlm_mod *m;
	long a[8];
	int e, i;

	if (cs == 0 || (m = cs->cs_mod) == 0) {
		dlm_stale_close++;
		return 0;
	}
	a[0] = (long)dev;
	a[1] = flag;
	a[2] = otyp;
	a[3] = (long)cr;
	a[4] = a[5] = a[6] = a[7] = 0;
	m->m_incall++;
	e = DLM_CALL8(m, cs->cs_close, a);
	m->m_incall--;
	if (otyp == OTYP_LYR)
		dlm_rele(m);
	else if ((i = cfind(cs, dev, otyp)) >= 0) {
		cs->cs_set[i] = cs->cs_set[--cs->cs_nopen];
		dlm_rele(m);
	} else
		dlm_stale_close++;
	return e;
}

static int
drv_install(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	struct dlm_cslot *cs;
	struct cdevsw *cp;
	unsigned long d, c, fl, f;
	char *r;
	int mj, n, i, k, s;

	if (!inimg(m, td, 8L))
		return ERELOC;
	d = G32(DLM_RP(m, td) + 4);
	if (!inimg(m, d, (unsigned long)DRVSZ))
		return ERELOC;
	r = DLM_RP(m, d);
	c = d + DRV_CDEVSW;
	mj = (int)S32(G32(r + DRV_CMAJOR));
	n = (int)S32(G32(r + DRV_CCOUNT));
	if (G32(r + DRV_BCOUNT) != 0 || mj < 0 || mj >= cdevcnt ||
	    n < 1 || n > cdevcnt - mj)
		return EINVAL;
	if (G32(DLM_RP(m, c) + CD_TTYS))
		return EINVAL;
	if ((fl = G32(DLM_RP(m, c) + CD_FLAG)) != 0) {
		if (!inimg(m, fl, 4L))
			return ERELOC;
		if (G32(DLM_RP(m, fl)) & D_OLD)
			return EINVAL;
	}
	if (G32(DLM_RP(m, c) + CD_STR))
#ifdef DLM_HOST
		return EINVAL;
#else
		return n > SDEV_MAX ? EINVAL : dlm_sdev_install(m, mj, n,
		    G32(DLM_RP(m, c) + CD_STR), fl ? (int *)DLM_RP(m, fl) : (int *)0);
#endif
	for (k = 0; k < CD_NFN; k++) {
		f = G32(DLM_RP(m, c) + 4 * k);
		if (f && (f < DLM_KLO || f >= DLM_KHI) && !inimg(m, f, 2L))
			return ERELOC;
	}
	for (i = 0; i < n; i++) {
		cs = cslot(mj + i);
		if (cs == 0 || strcmp(cs->cs_name, m->m_name) != 0 ||
		    (cs->cs_mod && cs->cs_mod != m))
			return EINVAL;
	}
	for (i = 0; i < n; i++) {
		cs = cslot(mj + i);
		cp = &cdevsw[mj + i];
		s = dlm_splhi();
		for (k = 2; k < CD_NFN; k++) {
			f = G32(DLM_RP(m, c) + 4 * k);
			cfn(cp)[k] = f ? (int (*)())f : nodev;
		}
		cp->d_flag = fl ? (int *)DLM_RP(m, fl) : dlm_cflag;
		cs->cs_open = G32(DLM_RP(m, c));
		cs->cs_close = G32(DLM_RP(m, c) + 4);
		cs->cs_mod = m;
		dlm_splx(s);
	}
	return 0;
}

static int
drv_remove(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	struct dlm_cslot *cs;
	int s;

#ifndef DLM_HOST
	dlm_sdev_remove(m);
#endif
	for (cs = dlm_cslot; cs; cs = cs->cs_next)
		if (cs->cs_mod == m) {
			s = dlm_splhi();
			cplace(&cdevsw[cs->cs_major]);
			cs->cs_mod = 0;
			cs->cs_open = cs->cs_close = 0;
			cs->cs_nopen = 0;
			dlm_splx(s);
		}
	return 0;
}

static void
drv_info(m, td, st)
	struct dlm_mod *m;
	unsigned long td;
	struct modspecific_stat *st;
{
	struct dlm_cslot *cs;

	st->mss_type = MOD_TY_CDEV;
	st->mss_p0[0] = st->mss_p0[1] = -1;
	st->mss_p1[0] = -1;
	st->mss_p1[1] = 0;
	for (cs = dlm_cslot; cs; cs = cs->cs_next)
		if (cs->cs_mod == m) {
			if (st->mss_p1[0] < 0 || cs->cs_major < st->mss_p1[0])
				st->mss_p1[0] = cs->cs_major;
			st->mss_p1[1]++;
		}
#ifndef DLM_HOST
	dlm_sdev_info(m, &st->mss_p1[0], &st->mss_p1[1]);
#endif
}

struct mod_operations mod_drvops = { drv_install, drv_remove, drv_info };
