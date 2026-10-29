/*
 * dlm_core.c -- loadable kernel modules: module list, load and unload,
 * reference counts, and the modload, moduload, modpath, modstat, modadm
 * and getksym system calls (sysent 64..69, written by dlm_init from
 * dmainit).
 *
 * The kernel is uniprocessor and non-preemptive.  A record marked
 * loading or unloading belongs to the process doing it; others sleep
 * on the record.  Every call runs under a guard: a longjmp through
 * u.u_qsav (signal during an interruptible sleep) unwinds the loads and
 * the unload in progress and the call returns EINTR.
 *
 * K&R C.
 */

#include "dlm.h"
#include "sys/bootmod.h"

struct dlm_lf {				/* one load in progress */
	struct dlm_lf	*lf_next;
	struct dlm_ld	lf_ld;
	struct dlm_mod	*lf_mod;
	struct vnode	*lf_vp;
	struct cred	*lf_cred;	/* saved for DL_SYS */
	struct vnode	*lf_rdir;
	int		lf_swapped;
	int		lf_stage;
	char		**lf_deptab;
	int		lf_depsz;
	char		*lf_pbuf;	/* path being tried */
	struct bootmod	*lf_bm;		/* or a boot module in memory */
};
#define	LF_OPEN		0
#define	LF_INLOAD	1	/* inside mw_load */
#define	LF_LOADED	2	/* mw_load succeeded */

int		dlm_inited;
char		*dlm_ktab;
struct dlm_mod	*dlm_list;
struct dlm_mod	*dlm_cand, *dlm_candtail;
int		dlm_nextid = 1;
int		dlm_prflock;
static char	dlm_defpath[] = "/etc/conf/mod.d";
char		dlm_path[MAXPATHLEN];
static struct cred *dlm_syscred;

extern char dlm_ksym[];
extern int nosys();
extern void dlm_cacheflush();
extern struct vnode *rootdir;

static int dlm_modload(), dlm_moduload(), dlm_modpath(), dlm_modstat();
static int dlm_modadm(), dlm_getksym();

#define	CURPROC()	((char *)u.u_procp)
#define	MLINK(m, i)	(G32(DLM_RP(m, m->m_wrapper) + MW_MODLINK) + (i) * MLSZ)

/* ---- small helpers ---- */

char *
dlm_zalloc(n)
	long n;
{
	return (char *)kmem_zalloc((size_t)(n ? n : 1), KM_SLEEP);
}

void
dlm_free(p, n)
	char *p;
	long n;
{
	kmem_free((_VOID *)p, (size_t)(n ? n : 1));
}

void
dlm_undef(ld, name)
	struct dlm_ld *ld;
	char *name;
{
	cmn_err(CE_NOTE, "!%s: undefined symbol %s", ld->ld_name, name);
	/* "!" keeps every later printf off the console until a plain cmn_err */
	cmn_err(CE_CONT, "");
	if (dlm_verbose)
		printf("%s: undefined symbol %s\n", ld->ld_name, name);
}

static char *
lastcomp(p)
	char *p;
{
	char *s = p;

	for (; *p; p++)
		if (*p == '/')
			s = p + 1;
	return s;
}

static int
isstatic(name)
	char *name;
{
	char **p;

	for (p = dlm_static; *p; p++)
		if (strcmp(*p, name) == 0)
			return 1;
	return 0;
}

static struct dlm_mod *
findname(name)
	char *name;
{
	struct dlm_mod *m;

	for (m = dlm_list; m; m = m->m_next)
		if (strcmp(m->m_name, name) == 0)
			return m;
	return 0;
}

static struct dlm_mod *
findid(id)
	int id;
{
	struct dlm_mod *m;

	for (m = dlm_list; m; m = m->m_next)
		if (m->m_id == id)
			return m;
	return 0;
}

static void
wake(m)
	struct dlm_mod *m;
{
	if (m->m_flags & DM_WANTED) {
		m->m_flags &= ~DM_WANTED;
		wakeup((caddr_t)m);
	}
}

/* ---- candidate list and counts ---- */

static void
cand_add(m)
	struct dlm_mod *m;
{
	if (m->m_flags & DM_CAND)
		return;
	m->m_flags |= DM_CAND;
	m->m_stamp = lbolt;
	m->m_cnext = 0;
	m->m_cprev = dlm_candtail;
	if (dlm_candtail)
		dlm_candtail->m_cnext = m;
	else
		dlm_cand = m;
	dlm_candtail = m;
}

static void
cand_del(m)
	struct dlm_mod *m;
{
	if (!(m->m_flags & DM_CAND))
		return;
	m->m_flags &= ~DM_CAND;
	if (m->m_cprev)
		m->m_cprev->m_cnext = m->m_cnext;
	else
		dlm_cand = m->m_cnext;
	if (m->m_cnext)
		m->m_cnext->m_cprev = m->m_cprev;
	else
		dlm_candtail = m->m_cprev;
	m->m_cnext = m->m_cprev = 0;
}

void
dlm_hold(m)
	struct dlm_mod *m;
{
	if (m->m_refs++ == 0)
		cand_del(m);
}

void
dlm_rele(m)
	struct dlm_mod *m;
{
	if (m->m_refs > 0 && --m->m_refs == 0 && m->m_deps == 0)
		cand_add(m);
}

void
dlm_depadd(m)
	struct dlm_mod *m;
{
	m->m_deps++;
	cand_del(m);
}

void
dlm_deprele(m)
	struct dlm_mod *m;
{
	if (m->m_deps > 0 && --m->m_deps == 0 && m->m_refs == 0)
		cand_add(m);
}

/* [a, a + n) lies inside the image; safe against 32-bit wrap */
static int
inimg(m, a, n)
	struct dlm_mod *m;
	unsigned long a, n;
{
	return a >= m->m_run && n <= m->m_size && a - m->m_run <= m->m_size - n;
}

static int
busy(m)
	struct dlm_mod *m;
{
	return m->m_refs || m->m_deps || m->m_incall ||
	    (m->m_flags & (DM_LOCKED | DM_TRANS))
#ifndef DLM_HOST
	    || dlm_fsbusy(m)
#endif
	    ;
}

/* ---- linkages ---- */

static struct mod_operations *
knownops(a)
	unsigned long a;
{
	if (a == 0)
		return 0;
	if (a == DLM_KADDR(&mod_miscops, "mod_miscops"))
		return &mod_miscops;
	if (a == DLM_KADDR(&mod_execops, "mod_execops"))
		return &mod_execops;
	if (a == DLM_KADDR(&mod_hookops, "mod_hookops"))
		return &mod_hookops;
	if (a == DLM_KADDR(&mod_drvops, "mod_drvops"))
		return &mod_drvops;
#ifndef DLM_HOST
	if (a == DLM_KADDR(&mod_strops, "mod_strops"))
		return &mod_strops;
	if (a == DLM_KADDR(&mod_fsops, "mod_fsops"))
		return &mod_fsops;
#endif
	return 0;
}

static int
link_install(m, i)
	struct dlm_mod *m;
	int i;
{
	char *ml = DLM_RP(m, MLINK(m, i));
	struct mod_operations *ops = knownops(G32(ml));

	if (ops == 0)
		return EINVAL;
	return (*ops->modm_install)(m, G32(ml + 4));
}

static void
link_remove(m, i)
	struct dlm_mod *m;
	int i;
{
	char *ml = DLM_RP(m, MLINK(m, i));
	struct mod_operations *ops = knownops(G32(ml));

	if (ops)
		(void)(*ops->modm_remove)(m, G32(ml + 4));
}

/* the modlink array must lie in the image; count its entries */
static int
link_count(m)
	struct dlm_mod *m;
{
	unsigned long a = G32(DLM_RP(m, m->m_wrapper) + MW_MODLINK);
	int n;

	if (a == 0)
		return 0;
	for (n = 0;; n++, a += MLSZ) {
		if (!inimg(m, a, (unsigned long)MLSZ))
			return -1;
		if (G32(DLM_RP(m, a)) == 0)
			return n;
	}
}

/* ---- misc linkage ---- */

static int
misc_install(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	return 0;
}

static int
misc_remove(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	return 0;
}

static void
misc_info(m, td, st)
	struct dlm_mod *m;
	unsigned long td;
	struct modspecific_stat *st;
{
	st->mss_type = MOD_TY_MISC;
	st->mss_p0[0] = st->mss_p0[1] = st->mss_p1[0] = st->mss_p1[1] = -1;
}

struct mod_operations mod_miscops = { misc_install, misc_remove, misc_info };

/* copy a module string (run address) into buf, bounded by the image */
static void
modstr(m, a, buf, n)
	struct dlm_mod *m;
	unsigned long a;
	char *buf;
	int n;
{
	unsigned long lim;
	int i = 0;

	if (inimg(m, a, 1L))
		for (lim = m->m_size - (a - m->m_run); i < n - 1 && i < lim; i++)
			if ((buf[i] = *DLM_RP(m, a + i)) == 0)
				break;
	buf[i] = 0;
}

/* ---- load ---- */

static int
lf_read(ld, off, buf, len)
	struct dlm_ld *ld;
	unsigned long off;
	char *buf;
	long len;
{
	struct dlm_lf *lf = (struct dlm_lf *)ld->ld_rh;
	int resid = 0, e;

	if (lf->lf_bm) {
		if (off > lf->lf_bm->bm_size || len > lf->lf_bm->bm_size - off)
			return DLM_ESHORT;
		bcopy((char *)lf->lf_bm->bm_addr + off, buf, len);
		return 0;
	}
	e = vn_rdwr(UIO_READ, lf->lf_vp, (caddr_t)buf, (int)len, (off_t)off,
	    UIO_SYSSPACE, 0, 0x7fffffffL, u.u_cred, &resid);
	if (e)
		return e;
	return resid ? DLM_ESHORT : 0;
}

static void
lf_close(lf)
	struct dlm_lf *lf;
{
	struct vnode *vp = lf->lf_vp;

	if (vp) {
		lf->lf_vp = 0;
		(void)VOP_CLOSE(vp, FREAD, 1, (off_t)0, u.u_cred);
		VN_RELE(vp);
	}
	if (lf->lf_swapped) {
		lf->lf_swapped = 0;
		u.u_procp->p_cred = lf->lf_cred;
		u.u_rdir = lf->lf_rdir;
	}
}

static void
freemod(m)
	struct dlm_mod *m;
{
	struct dlm_mod **pp;
	int i;

	m->m_flags &= ~DM_SYMOK;
	if (m->m_alloc)
		dlm_free(m->m_alloc, (long)m->m_allocsz);
	if (m->m_tab)
		dlm_free(m->m_tab, m->m_tabsz);
	m->m_alloc = m->m_tab = 0;
	for (i = 0; i < m->m_ndep; i++)
		dlm_deprele(m->m_dep[i]);
	if (m->m_dep)
		dlm_free((char *)m->m_dep, (long)m->m_maxdep *
		    (long)sizeof (struct dlm_mod *));
	cand_del(m);
	for (pp = &dlm_list; *pp; pp = &(*pp)->m_next)
		if (*pp == m) {
			*pp = m->m_next;
			break;
		}
	if (m->m_path)
		dlm_free(m->m_path, (long)m->m_pathsz);
	wake(m);
	dlm_free((char *)m, (long)sizeof (struct dlm_mod));
}

/* Undo a load in progress (the guard chain's head). */
static void
lf_fail(g, lf)
	struct dlm_guard *g;
	struct dlm_lf *lf;
{
	struct dlm_mod *m = lf->lf_mod;
	unsigned long w;

	g->g_lf = lf->lf_next;
	lf_close(lf);
	dlm_ld_free(&lf->lf_ld);
	if (lf->lf_deptab)
		dlm_free((char *)lf->lf_deptab, (long)lf->lf_depsz);
	if (lf->lf_pbuf)
		dlm_free(lf->lf_pbuf, (long)MAXPATHLEN);
	if (m) {
		while (m->m_nlink > 0)
			link_remove(m, --m->m_nlink);
		if (lf->lf_stage == LF_LOADED) {
			w = G32(DLM_RP(m, m->m_wrapper) + MW_UNLOAD);
			if (w)
				(void)DLM_CALL(m, w, m, 0);
		}
		freemod(m);
	}
	dlm_free((char *)lf, (long)sizeof (struct dlm_lf));
}

static int
lf_open(lf, path)
	struct dlm_lf *lf;
	char *path;
{
	struct vnode *vp;
	int e;

	e = vn_open(path, UIO_SYSSPACE, FREAD, 0, &vp, (enum create)0);
	if (e == 0)
		lf->lf_vp = vp;
	return e;
}

/* Open the module file: an absolute path, or each modpath element. */
static int
lf_find(lf, path, m)
	struct dlm_lf *lf;
	char *path;
	struct dlm_mod *m;
{
	char *buf, *p, *q;
	int e = ENOENT, n, k;

	/* in lf so that an interrupted open does not leak it */
	buf = lf->lf_pbuf = dlm_zalloc((long)MAXPATHLEN);
	if ((lf->lf_bm = dlm_bootfind(lastcomp(path))) != 0) {
		e = 0;
		strcpy(buf, "boot:");
		strcpy(buf + 5, lf->lf_bm->bm_name);
	} else if (rootdir == 0)
		e = ENOENT;
	else if (path[0] == '/') {
		e = lf_open(lf, path);
		strcpy(buf, path);
	} else
		for (p = dlm_path; *p; p = q) {
			while (*p == ':' || *p == ' ')
				p++;
			for (q = p; *q && *q != ':' && *q != ' '; q++)
				;
			if (q == p)
				break;
			n = q - p;
			k = strlen(path);
			if (n + 1 + k + 1 > MAXPATHLEN) {
				e = ENAMETOOLONG;
				continue;
			}
			bcopy(p, buf, n);
			buf[n] = '/';
			strcpy(buf + n + 1, path);
			if ((e = lf_open(lf, buf)) == 0)
				break;
		}
	if (e == 0) {
		m->m_pathsz = strlen(buf) + 1;
		m->m_path = dlm_zalloc((long)m->m_pathsz);
		strcpy(m->m_path, buf);
	}
	dlm_free(buf, (long)MAXPATHLEN);
	lf->lf_pbuf = 0;
	return e;
}

/*
 * Load the module named by the last component of path, or find it
 * loaded.  depth counts dependency levels from 1.
 */
int
dlm_load(path, flags, depth, g, mp)
	char *path;
	int flags, depth;
	struct dlm_guard *g;
	struct dlm_mod **mp;
{
	char *name = lastcomp(path);
	struct dlm_mod *m, *dm;
	struct dlm_lf *lf;
	struct dlm_ld *ld;
	char dep[MODMAXNAMELEN];
	long pos;
	int e, n, i;
	unsigned long w, c;

	if (!dlm_inited)
		return ENOSYS;
	if (strlen(name) > MODMAXNAMELEN - 1)
		return ENAMETOOLONG;
	if (*name == 0 || isstatic(name) || depth > DLM_MAXDEPTH)
		return EINVAL;
again:
	if ((m = findname(name)) != 0) {
		if (m->m_flags & DM_TRANS) {
			if (m->m_owner == CURPROC())
				return EINVAL;		/* cycle */
			m->m_flags |= DM_WANTED;
			(void)sleep((caddr_t)m, PZERO);
			goto again;
		}
		if (flags & DL_DEMAND)
			m->m_flags |= DM_DEMAND;
		*mp = m;
		return 0;
	}

	m = (struct dlm_mod *)dlm_zalloc((long)sizeof (struct dlm_mod));
	strcpy(m->m_name, name);
	m->m_flags = DM_LOADING;
	m->m_owner = CURPROC();
	m->m_id = dlm_nextid++;
	if (dlm_list == 0)
		dlm_list = m;
	else {
		for (dm = dlm_list; dm->m_next; dm = dm->m_next)
			;
		dm->m_next = m;
	}
	lf = (struct dlm_lf *)dlm_zalloc((long)sizeof (struct dlm_lf));
	lf->lf_mod = m;
	lf->lf_next = g->g_lf;
	g->g_lf = lf;
	ld = &lf->lf_ld;
	ld->ld_read = lf_read;
	ld->ld_rh = (char *)lf;
	ld->ld_name = m->m_name;

	if (flags & DL_SYS) {
		if (dlm_syscred == 0) {
			dlm_syscred = crget();	/* all ids 0 */
			dlm_syscred->cr_ref = 0x7fff;
		}
		lf->lf_cred = u.u_procp->p_cred;
		lf->lf_rdir = u.u_rdir;
		lf->lf_swapped = 1;
		u.u_procp->p_cred = dlm_syscred;
		u.u_rdir = 0;
	}
	if ((e = lf_find(lf, path, m)) != 0)
		goto fail;
	if ((e = dlm_ld_hdr(ld)) != 0 || (e = dlm_ld_moddata(ld)) != 0)
		goto fail;

	/* dependencies */
	for (n = 0, pos = 4; (i = dlm_ld_dep(ld, &pos, dep)) > 0; n++)
		;
	if (i < 0) {
		e = EINVAL;
		goto fail;
	}
	m->m_maxdep = n;
	if (n) {
		m->m_dep = (struct dlm_mod **)dlm_zalloc((long)n *
		    (long)sizeof (struct dlm_mod *));
		lf->lf_depsz = n * sizeof (char *);
		lf->lf_deptab = (char **)dlm_zalloc((long)lf->lf_depsz);
	}
	for (pos = 4; dlm_ld_dep(ld, &pos, dep) > 0;) {
		if (isstatic(dep))
			continue;
		if (dlm_load(dep, DL_SYS, depth + 1, g, &dm) != 0) {
			e = EINVAL;
			goto fail;
		}
		dlm_depadd(dm);
		lf->lf_deptab[m->m_ndep] = dm->m_tab;
		m->m_dep[m->m_ndep++] = dm;
	}
	ld->ld_deptab = lf->lf_deptab;
	ld->ld_ndep = m->m_ndep;
	ld->ld_ktab = dlm_ktab;

	/* symbols, image, relocation */
	if ((e = dlm_ld_syms(ld)) != 0)
		goto fail;
	if (ld->ld_imgsz > (unsigned long)dlm_maximage) {
		e = ENOMEM;
		goto fail;
	}
	m->m_size = ld->ld_imgsz;
	m->m_allocsz = m->m_size + 15;
	m->m_alloc = dlm_zalloc((long)m->m_allocsz);
	m->m_base = (char *)(((unsigned long)m->m_alloc + 15) & ~15L);
	m->m_run = DLM_RUNADDR(m);
	ld->ld_img = m->m_base;
	ld->ld_base = m->m_run;
	if ((e = dlm_ld_image(ld)) != 0 || (e = dlm_ld_reloc(ld)) != 0)
		goto fail;
	dlm_cacheflush();
	(void)dlm_ld_table(ld);
	m->m_tab = ld->ld_tab;
	m->m_tabsz = ld->ld_tabsz;
	ld->ld_tab = 0;
	m->m_flags |= DM_SYMOK;
	w = G32(m->m_base + ld->ld_secoff[ld->ld_modsec]);
	lf_close(lf);
	dlm_ld_free(ld);

	/* wrapper */
	if (!inimg(m, w, (unsigned long)MWSZ) || (w & 1)) {
		e = ERELOC;
		goto fail;
	}
	m->m_wrapper = w;
	if (G32(DLM_RP(m, w) + MW_REV) != MODREV) {
		e = EBADVER;
		goto fail;
	}
	if ((n = link_count(m)) < 0) {
		e = ERELOC;
		goto fail;
	}
	c = G32(DLM_RP(m, w) + MW_CONF);
	if (inimg(m, c, 4L))
		m->m_delay = S32(G32(DLM_RP(m, c)));
	else
		m->m_delay = dlm_def_unload_delay;
	m->m_delay *= HZ;
	if (dlm_prflock)
		m->m_flags |= DM_LOCKED;

	lf->lf_stage = LF_INLOAD;
	if ((c = G32(DLM_RP(m, w) + MW_LOAD)) != 0 &&
	    (e = DLM_CALL(m, c, 0, 0)) != 0)
		goto fail;
	lf->lf_stage = LF_LOADED;
	for (i = 0; i < n; i++) {
		if ((e = link_install(m, i)) != 0)
			goto fail;
		m->m_nlink++;
	}

	/* settle */
	g->g_lf = lf->lf_next;
	if (lf->lf_deptab)
		dlm_free((char *)lf->lf_deptab, (long)lf->lf_depsz);
	dlm_free((char *)lf, (long)sizeof (struct dlm_lf));
	m->m_flags &= ~DM_LOADING;
	m->m_owner = 0;
	if (flags & DL_DEMAND)
		m->m_flags |= DM_DEMAND;
	wake(m);
	*mp = m;
	return 0;
fail:
	lf_fail(g, lf);
	return e;
}

/* ---- unload ---- */

static void
relink(m, n)
	struct dlm_mod *m;
	int n;
{
	for (m->m_nlink = 0; m->m_nlink < n; m->m_nlink++)
		if (link_install(m, m->m_nlink) != 0)
			cmn_err(CE_PANIC, "dlm: cannot reinstall %s", m->m_name);
}

/*
 * Unload a settled module.  how: DU_DEMAND or DU_AUTO (refuses
 * demand-marked modules).
 */
int
dlm_unload(m, how, g)
	struct dlm_mod *m;
	int how;
	struct dlm_guard *g;
{
	unsigned long f;
	int e = 0;

	if (how == DU_AUTO && (m->m_flags & DM_DEMAND))
		return EBUSY;
	if (busy(m))
		return EBUSY;
	m->m_flags |= DM_UNLOADING;
	m->m_owner = CURPROC();
	g->g_unl = m;
	g->g_unlnl = m->m_nlink;
	while (m->m_nlink > 0)
		link_remove(m, --m->m_nlink);
	if ((f = G32(DLM_RP(m, m->m_wrapper) + MW_UNLOAD)) != 0)
		e = DLM_CALL(m, f, m, 0);
	g->g_unl = 0;
	if (e) {
		relink(m, g->g_unlnl);
		m->m_flags &= ~DM_UNLOADING;
		m->m_owner = 0;
		wake(m);
		return e;
	}
	freemod(m);
	return 0;
}

/* After a longjmp through the guard: unwind everything in progress. */
void
dlm_abort(g)
	struct dlm_guard *g;
{
	struct dlm_mod *m;

	while (g->g_lf)
		lf_fail(g, g->g_lf);
	if ((m = g->g_unl) != 0) {
		g->g_unl = 0;
		relink(m, g->g_unlnl);
		m->m_flags &= ~DM_UNLOADING;
		m->m_owner = 0;
		wake(m);
	}
}

/* ---- module API ---- */

static struct dlm_mod *
bywrapper(w)
	struct modwrapper *w;
{
	struct dlm_mod *m;

	for (m = dlm_list; m; m = m->m_next)
		if (m->m_wrapper && m->m_wrapper == DLM_KADDR(w, (char *)0))
			return m;
	return 0;
}

int
mod_hold(w)
	struct modwrapper *w;
{
	struct dlm_mod *m = bywrapper(w);

	if (m)
		dlm_hold(m);
	return 0;
}

void
mod_rele(w)
	struct modwrapper *w;
{
	struct dlm_mod *m = bywrapper(w);

	if (m)
		dlm_rele(m);
}

/* ---- system calls ---- */

/* arm: then "if (DLM_SETJMP(&u.u_qsav))" in the handler's own frame */
#define	GUARD(g, save) \
	bzero((caddr_t)&(g), sizeof (g)); \
	bcopy((caddr_t)&u.u_qsav, (caddr_t)&(save), sizeof (label_t))
#define	UNGUARD(save) \
	bcopy((caddr_t)&(save), (caddr_t)&u.u_qsav, sizeof (label_t))

static int
dlm_modload(uap, rvp)
	struct modloada *uap;
	rval_t *rvp;
{
	struct dlm_guard g;
	label_t save;
	struct dlm_mod *m;
	char *buf;
	u_int len;
	int e;

	if (!dlm_inited)
		return ENOSYS;
	if (!suser(u.u_cred))
		return EPERM;
	buf = dlm_zalloc((long)MAXPATHLEN);
	if ((e = copyinstr(uap->path, buf, MAXPATHLEN, &len)) == 0 &&
	    strlen(lastcomp(buf)) > MODMAXNAMELEN - 1)
		e = ENAMETOOLONG;
	if (e == 0) {
		GUARD(g, save);
		if (DLM_SETJMP(&u.u_qsav)) {
			UNGUARD(save);
			dlm_abort(&g);
			dlm_free(buf, (long)MAXPATHLEN);
			return EINTR;
		}
		e = dlm_load(buf, DL_DEMAND, 1, &g, &m);
		UNGUARD(save);
		if (e == 0)
			rvp->r_val1 = m->m_id;
	}
	dlm_free(buf, (long)MAXPATHLEN);
	return e;
}

static int
dlm_moduload(uap, rvp)
	struct moduloada *uap;
	rval_t *rvp;
{
	struct dlm_guard g;
	label_t save;
	struct dlm_mod *m;
	int e, done;

	if (!dlm_inited)
		return ENOSYS;
	if (!suser(u.u_cred))
		return EPERM;
	if (dlm_list == 0 || uap->id < 0)
		return EINVAL;
	GUARD(g, save);
	if (DLM_SETJMP(&u.u_qsav)) {
		UNGUARD(save);
		dlm_abort(&g);
		return EINTR;
	}
	if (uap->id > 0) {
		if ((m = findid(uap->id)) == 0)
			e = EINVAL;
		else {
			m->m_flags &= ~DM_DEMAND;
			e = dlm_unload(m, DU_DEMAND, &g);
		}
	} else {
		done = 0;
	restart:
		for (m = dlm_list; m; m = m->m_next) {
			if (m->m_flags & DM_TRANS)
				continue;
			m->m_flags &= ~DM_DEMAND;
			if (dlm_unload(m, DU_DEMAND, &g) == 0) {
				done++;
				goto restart;
			}
		}
		e = done ? 0 : EBUSY;
	}
	UNGUARD(save);
	return e;
}

static int
dlm_modpath(uap, rvp)
	struct modpatha *uap;
	rval_t *rvp;
{
	char *buf;
	u_int len;
	int e, i, n;

	if (!dlm_inited)
		return ENOSYS;
	if (!suser(u.u_cred))
		return EPERM;
	if (uap->path == 0) {
		strcpy(dlm_path, dlm_defpath);
		return 0;
	}
	buf = dlm_zalloc((long)MAXPATHLEN);
	if ((e = copyinstr(uap->path, buf, MAXPATHLEN, &len)) != 0)
		goto out;
	e = EINVAL;
	if (buf[0] != '/')
		goto out;
	for (i = 0; buf[i]; i++)
		if ((buf[i] == ':' || buf[i] == ' ') && buf[i + 1] != '/')
			goto out;
	n = strlen(buf);
	if (n + 1 + strlen(dlm_path) + 1 > MAXPATHLEN)
		goto out;
	buf[n] = ':';
	strcpy(buf + n + 1, dlm_path);
	strcpy(dlm_path, buf);
	e = 0;
out:
	dlm_free(buf, (long)MAXPATHLEN);
	return e;
}

static int
dlm_modstat(uap, rvp)
	struct modstata *uap;
	rval_t *rvp;
{
	struct modstatus *st;
	struct dlm_mod *m, *b = 0;
	struct mod_operations *ops;
	char *ml;
	unsigned long w;
	int i, n, e;

	if (!dlm_inited)
		return ENOSYS;
	if (!suser(u.u_cred))
		return EPERM;
	for (m = dlm_list; m; m = m->m_next) {
		if (uap->next == 0) {
			if (m->m_id == uap->id) {
				b = m;
				break;
			}
		} else if (m->m_id >= uap->id && !(m->m_flags & DM_TRANS) &&
		    (b == 0 || m->m_id < b->m_id))
			b = m;
	}
	if (b == 0 || (b->m_flags & DM_TRANS))
		return EINVAL;
	m = b;
	st = (struct modstatus *)dlm_zalloc((long)sizeof (struct modstatus));
	st->ms_id = m->m_id;
	st->ms_base = (caddr_t)m->m_run;
	st->ms_size = m->m_size;
	w = m->m_wrapper;
	st->ms_rev = G32(DLM_RP(m, w) + MW_REV);
	strncpy(st->ms_path, m->m_path, MAXPATHLEN - 1);
	st->ms_unload_delay = m->m_delay / HZ;
	st->ms_refcnt = m->m_refs;
	st->ms_depcnt = m->m_deps;
	n = m->m_nlink < MODMAXLINK ? m->m_nlink : MODMAXLINK;
	for (i = 0; i < n; i++) {
		ml = DLM_RP(m, MLINK(m, i));
		w = G32(ml + 4);		/* mod_type_data */
		if (inimg(m, w, 8L))
			modstr(m, G32(DLM_RP(m, w)), st->ms_msinfo[i].mss_linkinfo,
			    MODMAXLINKINFOLEN);
		if ((ops = knownops(G32(ml))) != 0 && ops->modm_info)
			(*ops->modm_info)(m, G32(ml + 4), &st->ms_msinfo[i]);
	}
	strcpy(st->ms_name, m->m_name);
	st->ms_flags = (m->m_flags & DM_DEMAND ? MS_DEMAND : 0) |
	    (m->m_flags & DM_LOCKED ? MS_LOCKED : 0) |
	    (m->m_flags & DM_CAND ? MS_CAND : 0);
	e = copyout((caddr_t)st, (caddr_t)uap->st, sizeof (struct modstatus)) ?
	    EFAULT : 0;
	dlm_free((char *)st, (long)sizeof (struct modstatus));
	return e;
}

static int
dlm_modadm(uap, rvp)
	struct modadma *uap;
	rval_t *rvp;
{
	struct mod_mreg r;
	struct mod_execreg er;
	int i, mj;

	if (!dlm_inited)
		return ENOSYS;
	if (!suser(u.u_cred))
		return EPERM;
	if (uap->cmd != MOD_C_MREG)
		return EINVAL;		/* MOD_C_AUTOUNLD not implemented yet */
	if (uap->type <= MOD_TY_NONE || uap->type > MOD_TY_MAX)
		return EINVAL;
	if (copyin(uap->arg, (caddr_t)&r, sizeof r))
		return EFAULT;
	for (i = 0; i < MODMAXNAMELEN && r.md_modname[i]; i++)
		;
	if (i == 0 || i == MODMAXNAMELEN)
		return EINVAL;
	if (uap->type == MOD_TY_MISC)
		return 0;
	if (uap->type == MOD_TY_EXEC) {
		if (copyin(r.md_typedata, (caddr_t)&er, sizeof er))
			return EFAULT;
		return dlm_xreg(r.md_modname, &er);
	}
	if (uap->type == MOD_TY_CDEV) {
		if (copyin(r.md_typedata, (caddr_t)&mj, sizeof mj))
			return EFAULT;
		return dlm_creg(r.md_modname, mj);
	}
#ifndef DLM_HOST
	if (uap->type == MOD_TY_SDEV) {
		if (copyin(r.md_typedata, (caddr_t)&mj, sizeof mj))
			return EFAULT;
		return dlm_sreg(r.md_modname, mj);
	}
#endif
	return EINVAL;			/* other slot types not implemented yet */
}

/* No privilege needed: any user learns kernel and module addresses. */
static int
dlm_getksym(uap, rvp)
	struct getksyma *uap;
	rval_t *rvp;
{
	struct dlm_mod *m;
	char *buf, *nm, *tab;
	unsigned long val, info, off;
	u_int len;
	int e, t, found = 0;

	if (!dlm_inited)
		return ENOSYS;
	if (copyin((caddr_t)uap->value, (caddr_t)&val, sizeof val))
		return EFAULT;
	buf = dlm_zalloc((long)MAXSYMNMLEN);
	if (val == 0) {
		if ((e = copyinstr(uap->name, buf, MAXSYMNMLEN, &len)) != 0)
			goto out;
		found = dlm_blklookup(dlm_ktab, buf, &val, &t);
		for (m = dlm_list; m && !found; m = m->m_next)
			if (m->m_flags & DM_SYMOK) {
				m->m_incall++;
				found = dlm_blklookup(m->m_tab, buf, &val, &t);
				m->m_incall--;
			}
		e = ENOMATCH;
		if (found) {
			info = ST_TYPE(t);
			e = copyout((caddr_t)&val, (caddr_t)uap->value, sizeof val) ||
			    copyout((caddr_t)&info, (caddr_t)uap->info,
			    sizeof info) ? EFAULT : 0;
		}
	} else {
		tab = 0;
		m = 0;
		if (val >= G32(dlm_ktab + KH_LO) && val < G32(dlm_ktab + KH_HI))
			tab = dlm_ktab;
		else
			for (m = dlm_list; m; m = m->m_next)
				if ((m->m_flags & DM_SYMOK) && val >= m->m_run &&
				    val < m->m_run + m->m_size)
					break;
		if (m) {
			m->m_incall++;
			tab = m->m_tab;
		}
		if (tab && dlm_blkaddr(tab, val, &nm, &off)) {
			strncpy(buf, nm, MAXSYMNMLEN - 1);
			found = 1;
		}
		if (m)
			m->m_incall--;
		e = ENOMATCH;
		if (found)
			e = copyout((caddr_t)buf, (caddr_t)uap->name,
			    strlen(buf) + 1) ||
			    copyout((caddr_t)&off, (caddr_t)uap->info,
			    sizeof off) ? EFAULT : 0;
	}
out:
	dlm_free(buf, (long)MAXSYMNMLEN);
	return e;
}

/* ---- initialisation (from dmainit) ---- */

static struct {
	int	num, narg;
	int	(*call)();
} dlm_calls[] = {
	{ SYS_modload,	1, dlm_modload },
	{ SYS_moduload,	1, dlm_moduload },
	{ SYS_modpath,	1, dlm_modpath },
	{ SYS_modstat,	3, dlm_modstat },
	{ SYS_modadm,	3, dlm_modadm },
	{ SYS_getksym,	3, dlm_getksym },
};
#define	NCALLS	(sizeof dlm_calls / sizeof dlm_calls[0])

#ifndef DLM_HOST
/* sysinfo(SI_ARCHITECTURE): the stock "m68k" made "m68030", "m68040" or "m68060" */
char architecture[8] = "m68030";
__asm__(".weak cputype");
extern long dlm_cputype __asm__("cputype");	/* 30, 40 or 60 */
#endif

void
dlm_init()
{
	int i;

#ifndef DLM_HOST
	{
		long *__volatile__ cpu = &dlm_cputype;

		if (cpu && (*cpu == 40 || *cpu == 60))
			architecture[4] = '0' + *cpu / 10;
	}
#endif
	dlm_slot_init();
#ifndef DLM_HOST
	dlm_str_init();
	dlm_fs_init();
#endif
	if (dlm_blkcheck(dlm_ksym, (long)KSYM_SPACE) != 0 ||
	    G32(dlm_ksym + KH_LO) != DLM_KLO || G32(dlm_ksym + KH_HI) != DLM_KHI) {
		printf("dlm: no kernel symbol table, modules disabled\n");
		return;
	}
	if (sysentsize != 142) {
		printf("dlm: sysent has %d entries, modules disabled\n", sysentsize);
		return;
	}
	for (i = 0; i < NCALLS; i++)
		if (sysent[dlm_calls[i].num].sy_call != nosys) {
			printf("dlm: sysent[%d] in use, modules disabled\n",
			    dlm_calls[i].num);
			return;
		}
	for (i = 0; i < NCALLS; i++) {
		sysent[dlm_calls[i].num].sy_narg = dlm_calls[i].narg;
		sysent[dlm_calls[i].num].sy_flags = SETJUMP;
		sysent[dlm_calls[i].num].sy_call = dlm_calls[i].call;
	}
	dlm_ktab = dlm_ksym;
	strcpy(dlm_path, dlm_defpath);
	dlm_inited = 1;
#ifndef DLM_HOST
	if (dlm_creg("cpmguest", 59) != 0)
		printf("dlm: cpmguest registration failed\n");
	{
		extern void ds_modinit();
		void (*__volatile__ init)() = ds_modinit;
		__asm__(".weak ds_modinit");
		if (init)
			(*init)();
	}
#endif
	printf("dlm: %d kernel symbols\n", (int)G32(dlm_ksym + KH_NSYM) - 1);
}
