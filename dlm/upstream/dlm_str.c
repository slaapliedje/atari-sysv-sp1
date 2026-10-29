/*
 * dlm_str.c -- STREAMS-module linkage.
 *
 * fmodsw is replaced by a larger table; dlm_str_init copies the stock
 * rows in from __amix_fmodsw.  A module's linkage appends a row under
 * its name, or takes back the row it held before: rows are never
 * removed, because sad and qattach keep fmodsw indices.
 *
 * A loaded row points at a streamtab of the loader's own whose read
 * qinit copies the module's with open and close replaced by
 * trampolines.  The open trampoline holds the module while the queue
 * pair is open, from the open that sets q_ptr to the close; a removed
 * row keeps its trampolines and fails opens with ENXIO, so an I_PUSH
 * that found the row before an unload never enters freed code.
 *
 * STREAMS drivers work the same way under a registered major: the
 * cdevsw row's d_str is a streamtab of ours whose open trampoline
 * loads the module first, then reloads the queue pair's qinits.
 *
 * K&R C.
 */

#include "dlm.h"
#include "sys/stream.h"
#include "sys/conf.h"

#define	FMOD_STATIC	11
#define	FMOD_RESERVE	16
#define	NFMODSW		(FMOD_STATIC + FMOD_RESERVE)

/* struct mod_str_data */
#define	SD_NAME		0
#define	SD_TAB		12
#define	SD_FLAG		16
#define	SDSZ		20
#define	STSZ		16		/* struct streamtab */
#define	QISZ		28		/* struct qinit */
#define	QI_OPEN		8
#define	QI_CLOSE	12
#define	QI_MINFO	20
#define	QI_MSTAT	24

struct fmodsw	fmodsw[NFMODSW] = { { { 0 } } };	/* in .data, as the stock table */
extern struct fmodsw __amix_fmodsw[];
extern int	fmodcnt;
extern int	strncmp();

/* the switch and queues are read at interrupt level */
#define	SPLHI(s) do { \
	__asm__ __volatile__("movew %%sr,%0" : "=d" (s) : : "memory"); \
	__asm__ __volatile__("movew %0,%%sr" : : "d" ((s) | 0x0700) : "memory"); \
} while (0)
#define	SPLX(s)	__asm__ __volatile__("movew %0,%%sr" : : "d" (s) : "memory")

static int		fstatic;		/* stock rows */
static struct dlm_mod	*fown[FMOD_RESERVE];	/* per loadable row */
static struct streamtab	ftab[FMOD_RESERVE];
static struct qinit	frq[FMOD_RESERVE], fwq[FMOD_RESERVE];
static int		(*fopen[FMOD_RESERVE])(), (*fclose[FMOD_RESERVE])();
static int		zeroflag;
static struct module_info ph_info = { 0, "dlm", 0, INFPSZ, 1, 0 };

int	dlm_str_open(), dlm_str_close();

void
dlm_str_init()
{
	int i;

	if (fmodcnt > FMOD_STATIC)
		fmodcnt = FMOD_STATIC;		/* the stock kernel has 11 */
	for (i = 0; i < fmodcnt; i++)
		fmodsw[i] = __amix_fmodsw[i];
	fstatic = fmodcnt;
}

static int
inimg(m, a, n)
	struct dlm_mod *m;
	unsigned long a, n;
{
	return a >= m->m_run && n <= m->m_size && a - m->m_run <= m->m_size - n;
}

/* a code or data pointer: 0, kernel, or n bytes in the image */
static int
okptr(m, a, n)
	struct dlm_mod *m;
	unsigned long a, n;
{
	return a == 0 || (a >= DLM_KLO && a < DLM_KHI) || inimg(m, a, n);
}

/* a module qinit at a: in the image, sane pointers */
static int
okqinit(m, a)
	struct dlm_mod *m;
	unsigned long a;
{
	char *q;
	int k;

	if (!inimg(m, a, (unsigned long)QISZ))
		return 0;
	q = DLM_RP(m, a);
	for (k = 0; k < QI_MINFO; k += 4)
		if (!okptr(m, G32(q + k), 2L))
			return 0;
	return G32(q + QI_MINFO) != 0 && okptr(m, G32(q + QI_MINFO),
	    (unsigned long)sizeof (struct module_info)) &&
	    okptr(m, G32(q + QI_MSTAT), 4L);
}

/* loadable row j of queue q, or -1 */
static int
qrow(q)
	queue_t *q;
{
	struct qinit *qi = q->q_qinfo;

	return qi >= frq && qi < frq + FMOD_RESERVE ? qi - frq : -1;
}

/*
 * qi_qopen of a loadable row.  A signal that ends a sleep in the
 * module's open unwinds through u_qsav; the hold goes first.
 */
int
dlm_str_open(q, devp, flag, sflag, cr)
	queue_t *q;
	dev_t *devp;
	int flag, sflag;
	struct cred *cr;
{
	struct { struct dlm_mod *m; } h;	/* in memory: read after a longjmp */
	label_t save;
	caddr_t had = q->q_ptr;
	int j = qrow(q), e;

	if (j < 0 || fown[j] == 0 || fopen[j] == 0)
		return ENXIO;
	h.m = fown[j];
	dlm_hold(h.m);
	bcopy((caddr_t)&u.u_qsav, (caddr_t)&save, sizeof (label_t));
	if (DLM_SETJMP(&u.u_qsav)) {
		bcopy((caddr_t)&save, (caddr_t)&u.u_qsav, sizeof (label_t));
		dlm_rele(h.m);			/* a failed open: no close follows */
		DLM_LONGJMP(&u.u_qsav);
	}
	e = (*fopen[j])(q, devp, flag, sflag, cr);
	bcopy((caddr_t)&save, (caddr_t)&u.u_qsav, sizeof (label_t));
	if (e != 0 || had != 0 || q->q_ptr == 0)
		dlm_rele(h.m);
	return e;
}

/* qi_qclose: the module's close, then the open's hold goes */
int
dlm_str_close(q, flag, cr)
	queue_t *q;
	int flag;
	struct cred *cr;
{
	struct dlm_mod *m;
	caddr_t had = q->q_ptr;
	int j = qrow(q), e = 0;

	if (j < 0 || (m = fown[j]) == 0)
		return 0;
	m->m_incall++;
	if (fclose[j])
		e = (*fclose[j])(q, flag, cr);
	m->m_incall--;
	if (had)
		dlm_rele(m);
	return e;
}

static int
str_install(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	unsigned long d, tab, rq, wq, fl;
	char name[FMNAMESZ + 1], *r;
	int *flag, i, j, s;

	if (!inimg(m, td, 8L))
		return ERELOC;
	d = G32(DLM_RP(m, td) + 4);
	if (!inimg(m, d, (unsigned long)SDSZ))
		return ERELOC;
	r = DLM_RP(m, d);
	for (i = 0; i <= FMNAMESZ; i++)
		if ((name[i] = r[SD_NAME + i]) == 0)
			break;
	if (i == 0 || i > FMNAMESZ)
		return EINVAL;
	tab = G32(r + SD_TAB);
	if (!inimg(m, tab, (unsigned long)STSZ))
		return ERELOC;
	rq = G32(DLM_RP(m, tab));
	wq = G32(DLM_RP(m, tab) + 4);
	if (G32(DLM_RP(m, tab) + 8) || G32(DLM_RP(m, tab) + 12))
		return EINVAL;			/* a multiplexor */
	if (!okqinit(m, rq) || !okqinit(m, wq))
		return ERELOC;
	flag = &zeroflag;
	if ((fl = G32(r + SD_FLAG)) != 0) {
		if (!inimg(m, fl, 4L))
			return ERELOC;
		if (G32(DLM_RP(m, fl)) & D_OLD)
			return EINVAL;
		flag = (int *)DLM_RP(m, fl);
	}
	for (i = 0; i < fmodcnt; i++)
		if (strncmp(fmodsw[i].f_name, name, FMNAMESZ) == 0)
			break;
	j = i - fstatic;
	if (j < 0 || (i < fmodcnt && fown[j] && fown[j] != m))
		return EEXIST;
	if (j >= FMOD_RESERVE)
		return ECONFIG;
	SPLHI(s);
	bcopy(DLM_RP(m, rq), (caddr_t)&frq[j], QISZ);
	bcopy(DLM_RP(m, wq), (caddr_t)&fwq[j], QISZ);
	fopen[j] = frq[j].qi_qopen;
	fclose[j] = frq[j].qi_qclose;
	frq[j].qi_qopen = dlm_str_open;
	frq[j].qi_qclose = dlm_str_close;
	ftab[j].st_rdinit = &frq[j];
	ftab[j].st_wrinit = &fwq[j];
	strncpy(fmodsw[i].f_name, name, FMNAMESZ);
	fmodsw[i].f_str = &ftab[j];
	fmodsw[i].f_flag = flag;
	fown[j] = m;
	if (i == fmodcnt)
		fmodcnt++;
	SPLX(s);
	return 0;
}

static int
str_remove(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	int j, s;

	for (j = 0; j < FMOD_RESERVE; j++)
		if (fown[j] == m) {
			SPLHI(s);
			fown[j] = 0;
			fopen[j] = fclose[j] = 0;
			bzero((caddr_t)&frq[j], QISZ);
			bzero((caddr_t)&fwq[j], QISZ);
			frq[j].qi_qopen = dlm_str_open;
			frq[j].qi_qclose = dlm_str_close;
			frq[j].qi_minfo = fwq[j].qi_minfo = &ph_info;
			fmodsw[fstatic + j].f_flag = &zeroflag;
			SPLX(s);
		}
	return 0;
}

static void
str_info(m, td, st)
	struct dlm_mod *m;
	unsigned long td;
	struct modspecific_stat *st;
{
	int j;

	st->mss_type = MOD_TY_STR;
	st->mss_p0[0] = st->mss_p0[1] = st->mss_p1[0] = st->mss_p1[1] = -1;
	for (j = 0; j < FMOD_RESERVE; j++)
		if (fown[j] == m) {
			st->mss_p0[0] = fstatic + j;
			break;
		}
}

struct mod_operations mod_strops = { str_install, str_remove, str_info };

/* ---- STREAMS drivers ---- */

#define	SDEV_SLOTS	4

static struct sdslot {
	char		sd_name[MODMAXNAMELEN];	/* "" = free */
	int		sd_major;
	struct dlm_mod	*sd_mod;
	int		(*sd_open)(), (*sd_close)();
	struct streamtab sd_tab;
	struct qinit	sd_rq, sd_wq;
} sds[SDEV_SLOTS];

extern int dlm_cempty();
extern void setq();
int	dlm_sdev_open(), dlm_sdev_close();

/* a write before the driver is loaded */
static int
sd_drop(q, mp)
	queue_t *q;
	mblk_t *mp;
{
	freemsg(mp);
	return 0;
}

static void
sdplace(sd)
	struct sdslot *sd;
{
	bzero((caddr_t)&sd->sd_rq, QISZ);
	bzero((caddr_t)&sd->sd_wq, QISZ);
	sd->sd_rq.qi_qopen = dlm_sdev_open;
	sd->sd_rq.qi_qclose = dlm_sdev_close;
	sd->sd_rq.qi_putp = sd->sd_wq.qi_putp = sd_drop;
	sd->sd_rq.qi_minfo = sd->sd_wq.qi_minfo = &ph_info;
	sd->sd_tab.st_rdinit = &sd->sd_rq;
	sd->sd_tab.st_wrinit = &sd->sd_wq;
	sd->sd_mod = 0;
	sd->sd_open = sd->sd_close = 0;
}

static struct sdslot *
sdmajor(mj)
	int mj;
{
	int k;

	for (k = 0; k < SDEV_SLOTS; k++)
		if (sds[k].sd_name[0] && sds[k].sd_major == mj)
			return &sds[k];
	return 0;
}

/* modadm(MOD_TY_SDEV): a slot for (name, major) over an empty row */
int
dlm_sreg(name, mj)
	char *name;
	int mj;
{
	struct sdslot *sd, *fr = 0;
	struct cdevsw *cp;
	int k, s;

	if (mj < 0 || mj >= cdevcnt)
		return ECONFIG;
	if ((sd = sdmajor(mj)) != 0)
		return strcmp(sd->sd_name, name) == 0 ? 0 : EEXIST;
	if (!dlm_cempty(&cdevsw[mj]))
		return EEXIST;
	for (k = 0; k < SDEV_SLOTS && fr == 0; k++)
		if (sds[k].sd_name[0] == 0)
			fr = &sds[k];
	if (fr == 0)
		return ECONFIG;
	bzero((caddr_t)fr, sizeof *fr);
	strcpy(fr->sd_name, name);
	fr->sd_major = mj;
	sdplace(fr);
	cp = &cdevsw[mj];
	SPLHI(s);
	cp->d_flag = &zeroflag;
	cp->d_str = &fr->sd_tab;
	SPLX(s);
	return 0;
}

static struct sdslot *
sdrow(q)
	queue_t *q;
{
	int k;

	for (k = 0; k < SDEV_SLOTS; k++)
		if (q->q_qinfo == &sds[k].sd_rq)
			return &sds[k];
	return 0;
}

/*
 * qi_qopen of a registered major: load the driver on first use; a new
 * queue pair takes the driver's limits.  Held from the open that sets
 * q_ptr to the close.
 */
int
dlm_sdev_open(q, devp, flag, sflag, cr)
	queue_t *q;
	dev_t *devp;
	int flag, sflag;
	struct cred *cr;
{
	struct sdslot *sd = sdrow(q);
	struct dlm_guard g;
	struct { struct dlm_mod *m; } h;	/* in memory: read after a longjmp */
	label_t save;
	struct dlm_mod *m;
	caddr_t had = q->q_ptr;
	int e;

	if (sd == 0)
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
	if (sd->sd_mod == 0 &&
	    ((e = dlm_load(sd->sd_name, DL_SYS, 1, &g, &m)) != 0 || sd->sd_mod == 0))
		e = ENXIO;
	if (e == 0) {
		h.m = m = sd->sd_mod;
		dlm_hold(m);
		if (had == 0)
			setq(q, &sd->sd_rq, &sd->sd_wq);
		e = (*sd->sd_open)(q, devp, flag, sflag, cr);
		if (e != 0 || had != 0 || q->q_ptr == 0)
			dlm_rele(m);
		h.m = 0;
	}
	bcopy((caddr_t)&save, (caddr_t)&u.u_qsav, sizeof (label_t));
	return e;
}

int
dlm_sdev_close(q, flag, cr)
	queue_t *q;
	int flag;
	struct cred *cr;
{
	struct sdslot *sd = sdrow(q);
	struct dlm_mod *m;
	caddr_t had = q->q_ptr;
	int e = 0;

	if (sd == 0 || (m = sd->sd_mod) == 0)
		return 0;
	m->m_incall++;
	if (sd->sd_close)
		e = (*sd->sd_close)(q, flag, cr);
	m->m_incall--;
	if (had)
		dlm_rele(m);
	return e;
}

/* a driver module's streamtab at tab for majors mj .. mj+n-1 */
int
dlm_sdev_install(m, mj, n, tab, flag)
	struct dlm_mod *m;
	int mj, n;
	unsigned long tab;
	int *flag;
{
	struct sdslot *sd;
	unsigned long rq, wq;
	int i, s;

	if (!inimg(m, tab, (unsigned long)STSZ))
		return ERELOC;
	rq = G32(DLM_RP(m, tab));
	wq = G32(DLM_RP(m, tab) + 4);
	if (G32(DLM_RP(m, tab) + 8) || G32(DLM_RP(m, tab) + 12))
		return EINVAL;			/* a multiplexor */
	if (!okqinit(m, rq) || !okqinit(m, wq) ||
	    G32(DLM_RP(m, rq) + QI_OPEN) == 0)
		return ERELOC;
	for (i = 0; i < n; i++) {
		sd = sdmajor(mj + i);
		if (sd == 0 || strcmp(sd->sd_name, m->m_name) != 0 ||
		    (sd->sd_mod && sd->sd_mod != m))
			return EINVAL;
	}
	for (i = 0; i < n; i++) {
		sd = sdmajor(mj + i);
		SPLHI(s);
		bcopy(DLM_RP(m, rq), (caddr_t)&sd->sd_rq, QISZ);
		bcopy(DLM_RP(m, wq), (caddr_t)&sd->sd_wq, QISZ);
		sd->sd_open = sd->sd_rq.qi_qopen;
		sd->sd_close = sd->sd_rq.qi_qclose;
		sd->sd_rq.qi_qopen = dlm_sdev_open;
		sd->sd_rq.qi_qclose = dlm_sdev_close;
		cdevsw[mj + i].d_flag = flag ? flag : &zeroflag;
		sd->sd_mod = m;
		SPLX(s);
	}
	return 0;
}

void
dlm_sdev_remove(m)
	struct dlm_mod *m;
{
	int k, s;

	for (k = 0; k < SDEV_SLOTS; k++)
		if (sds[k].sd_name[0] && sds[k].sd_mod == m) {
			SPLHI(s);
			sdplace(&sds[k]);
			cdevsw[sds[k].sd_major].d_flag = &zeroflag;
			SPLX(s);
		}
}

/* the lowest major and the count of m's rows */
void
dlm_sdev_info(m, mjp, np)
	struct dlm_mod *m;
	int *mjp, *np;
{
	int k;

	for (k = 0; k < SDEV_SLOTS; k++)
		if (sds[k].sd_name[0] && sds[k].sd_mod == m) {
			if (*mjp < 0 || sds[k].sd_major < *mjp)
				*mjp = sds[k].sd_major;
			(*np)++;
		}
}
