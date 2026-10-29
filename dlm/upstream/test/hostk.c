/*
 * hostk.c -- the kernel services dlm_core.c uses, simulated on the host
 * for the state harness.
 *
 *	files		vn_open maps "/x" to host_root "/x"
 *	memory		kmem_zalloc/kmem_free with a byte and block count
 *	images		each image gets a fake 32-bit run address
 *	module calls	host_call looks the address up in the module's
 *			table and runs the handler registered for that
 *			name (host_handler), else returns 0
 *	signals		host_intr_after > 0 longjmps through u.u_qsav
 *			on that many-th vn_rdwr, host_intr_open on vn_open
 *
 * K&R C.
 */

#include "dlm.h"

struct user	u;
struct proc	host_proc;
struct cred	host_cred;
long		lbolt;
struct sysent	sysent[142];
unsigned	sysentsize = 142;
char		dlm_ksym[KSYM_SPACE];
unsigned long	host_klo, host_khi;
char		*host_root = ".";
long		host_kbytes, host_kblocks;
int		host_intr_after, host_intr_open;
int		host_sleeps;
unsigned long	host_nextrun = 0x01000000;
int		host_fault;	/* copyin/copyout of (char *)1 fault */

int
nosys()
{
	return ENOSYS;
}

int
nodev()
{
	return ENODEV;
}

char *
kmem_zalloc(n, f)
	size_t n;
	int f;
{
	char *p = calloc(1, n);

	if (!p) {
		fprintf(stderr, "out of memory\n");
		exit(2);
	}
	host_kbytes += n;
	host_kblocks++;
	return p;
}

void
kmem_free(p, n)
	char *p;
	size_t n;
{
	host_kbytes -= n;
	host_kblocks--;
	free(p);
}

struct cred *
crget()
{
	struct cred *c = (struct cred *)kmem_zalloc(sizeof (struct cred), 0);

	c->cr_ref = 1;
	return c;
}

int
suser(cr)
	struct cred *cr;
{
	return cr->cr_uid == 0;
}

int
copyin(from, to, n)
	char *from, *to;
	size_t n;
{
	if (from == (char *)1)
		return -1;
	memmove(to, from, n);
	return 0;
}

int
copyout(from, to, n)
	char *from, *to;
	size_t n;
{
	if (to == (char *)1)
		return -1;
	memmove(to, from, n);
	return 0;
}

int
copyinstr(from, to, max, lenp)
	char *from, *to;
	size_t max;
	u_int *lenp;
{
	size_t n;

	if (from == (char *)1)
		return EFAULT;
	n = strlen(from);
	if (n + 1 > max)
		return ENAMETOOLONG;
	memmove(to, from, n + 1);
	if (lenp)
		*lenp = n + 1;
	return 0;
}

static struct vnode hostroot;
struct vnode *rootdir = &hostroot;

int
vn_open(path, seg, mode, cmode, vpp, why)
	char *path;
	int seg, mode, cmode;
	struct vnode **vpp;
	int why;
{
	char buf[2048];
	FILE *f;
	struct vnode *vp;

	if (host_intr_open > 0 && --host_intr_open == 0)
		longjmp(u.u_qsav.jb, 1);
	snprintf(buf, sizeof buf, "%s%s", host_root, path);
	if ((f = fopen(buf, "rb")) == 0)
		return ENOENT;
	vp = (struct vnode *)malloc(sizeof *vp);
	vp->v_fp = f;
	*vpp = vp;
	return 0;
}

int
vn_rdwr(rw, vp, base, len, off, seg, ioflag, ulimit, cr, residp)
	int rw;
	struct vnode *vp;
	char *base;
	int len;
	long off;
	int seg, ioflag;
	long ulimit;
	struct cred *cr;
	int *residp;
{
	size_t n;

	if (host_intr_after > 0 && --host_intr_after == 0)
		longjmp(u.u_qsav.jb, 1);
	if (fseek(vp->v_fp, off, 0) != 0)
		return EIO;
	n = fread(base, 1, (size_t)len, vp->v_fp);
	*residp = len - (int)n;
	return 0;
}

int
host_close(vp)
	struct vnode *vp;
{
	fclose(vp->v_fp);
	free(vp);
	return 0;
}

int
sleep(chan, pri)
	char *chan;
	int pri;
{
	host_sleeps++;
	fprintf(stderr, "hostk: sleep on %p (would deadlock the harness)\n",
	    (void *)chan);
	exit(2);
	return 0;
}

void
wakeup(chan)
	char *chan;
{
}

void
cmn_err(level, fmt, a, b, c)
	int level;
	char *fmt;
	char *a, *b, *c;
{
	if (level == CE_PANIC) {
		fprintf(stderr, "PANIC: ");
		fprintf(stderr, fmt, a, b, c);
		fprintf(stderr, "\n");
		exit(4);
	}
	if (getenv("HOSTK_VERBOSE")) {
		fprintf(stderr, fmt, a, b, c);
		fprintf(stderr, "\n");
	}
}

void
dlm_cacheflush()
{
}

struct bootmod *
dlm_bootfind(name)
	char *name;
{
	return 0;
}

/* ---- images ---- */

char *
host_rp(m, a)
	struct dlm_mod *m;
	unsigned long a;
{
	return m->m_base + (a - m->m_run);
}

unsigned long
host_runaddr(m)
	struct dlm_mod *m;
{
	unsigned long r = host_nextrun;

	host_nextrun += (m->m_size + 0xffff) & ~0xffffL;
	return r;
}

/* ---- calls into module code ---- */

struct hh {
	char	*name;
	int	(*fn)();
} host_hh[64];
int host_nhh;

void
host_handler(name, fn)
	char *name;
	int (*fn)();
{
	host_hh[host_nhh].name = name;
	host_hh[host_nhh++].fn = fn;
}

int
host_call(m, f, a, b)
	struct dlm_mod *m;
	unsigned long f;
	char *a;
	long b;
{
	char *nm;
	unsigned long off;
	int i;

	if (m->m_tab && dlm_blkaddr(m->m_tab, f, &nm, &off) && off == 0)
		for (i = 0; i < host_nhh; i++)
			if (strcmp(host_hh[i].name, nm) == 0)
				return (*host_hh[i].fn)(m, a, b);
	return 0;
}

unsigned long
host_kaddr(p, name)
	char *p, *name;
{
	unsigned long v;

	if (name == 0)
		return (unsigned long)p;
	if (dlm_blklookup(dlm_ksym, name, &v, (int *)0))
		return v;
	return 0;
}
