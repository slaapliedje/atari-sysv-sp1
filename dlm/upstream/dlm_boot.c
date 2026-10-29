/*
 * dlm_boot.c -- boot modules: module files the boot loader read into
 * memory, linked just before the root mounts so that a disk
 * controller's driver can serve the root.
 *
 * config() records them (dlm_bootadd, dlm_boottab) and moves them to
 * the top of memory (dlm_bootpack) before the VM takes the rest.
 * Everything here is in .data: config() runs before BSS is cleared.
 *
 * K&R C.
 */

#include "dlm.h"
#include "sys/bootmod.h"

/* initialized, so in .data */
static struct bootmod bm[BM_MAX] = { { 0 } };
static int	nbm = 0, lost = 0;
static int	linking = 0;

void
dlm_bootadd(addr, size, name)
	unsigned long addr, size;
	char *name;
{
	int i;

	if (nbm == BM_MAX) {
		lost++;
		return;
	}
	bm[nbm].bm_addr = addr;
	bm[nbm].bm_size = size;
	for (i = 0; i < BM_NAMELEN - 1 && name[i]; i++)
		bm[nbm].bm_name[i] = name[i];
	bm[nbm].bm_name[i] = 0;
	nbm++;
}

/* A loader's table (sys/bootmod.h); a bad magic counts as none */
void
dlm_boottab(t)
	char *t;
{
	unsigned long i, n;

	if (t == 0 || G32(t) != BM_MAGIC)
		return;
	n = G32(t + 4);
	if (n > BM_MAX) {
		lost += n - BM_MAX;
		n = BM_MAX;
	}
	for (i = 0; i < n; i++, t += BM_ENTSZ)
		dlm_bootadd(G32(t + 8), G32(t + 12), t + 16);
}

/* placed: moved clear of the VM; noroom: no space for it */
#define	BM_PLACED	1
#define	BM_NOROOM	2
static char	bmst[BM_MAX] = { 0 };

/* the highest untried module starting in [lo, hi) (whole: ending there too) */
static int
bm_next(tried, lo, hi, whole)
	char *tried;
	unsigned long lo, hi;
	int whole;
{
	unsigned long a, e;
	int i, j = -1;

	for (i = 0; i < nbm; i++) {
		a = bm[i].bm_addr;
		e = a + bm[i].bm_size;
		if (tried[i] || a < lo || a >= hi || (whole && (e < a || e > hi)))
			continue;
		if (j < 0 || a > bm[j].bm_addr)
			j = i;
	}
	return j;
}

/* copy module j to just below top; return the new top */
static unsigned long
bm_place(tried, j, lo, top)
	char *tried;
	int j;
	unsigned long lo, top;
{
	unsigned long s = bm[j].bm_size, d, i;
	char *p, *q;

	tried[j] = 1;
	if (s > top - lo || ((top - s) & ~3L) < lo) {
		bmst[j] = BM_NOROOM;
		return top;
	}
	d = (top - s) & ~3L;
	p = (char *)bm[j].bm_addr;
	q = (char *)d;
	if (q > p)
		for (i = s; i-- > 0;)
			q[i] = p[i];
	else
		for (i = 0; i < s; i++)
			q[i] = p[i];
	bm[j].bm_addr = d;
	bmst[j] = BM_PLACED;
	return d;
}

/*
 * Move the modules inside [lo, hi) up to its top, highest first so a
 * move never overwrites a module still to move.  With copy set, copy
 * the rest below them: those starting under hi highest first, then
 * those above.  The MMU is off, so any address works.  Placed modules
 * stay put on later calls.  Returns the new top, which the caller
 * rounds down to its page size.
 */
unsigned long
dlm_bootpack(lo, hi, copy)
	unsigned long lo, hi;
	int copy;
{
	char tried[BM_MAX];
	unsigned long top = hi, a, e;
	int i, j;

	for (i = 0; i < nbm; i++) {
		tried[i] = bmst[i] == BM_PLACED;
		a = bm[i].bm_addr;
		e = a + bm[i].bm_size;
		/* stay below a module that runs past hi */
		if (!tried[i] && a >= lo && a < top && (e < a || e > hi))
			top = a;
	}
	while ((j = bm_next(tried, lo, top, 1)) >= 0)
		top = bm_place(tried, j, lo, top);
	if (!copy)
		return top;
	while ((j = bm_next(tried, 0UL, hi, 0)) >= 0)
		top = bm_place(tried, j, lo, top);
	for (i = 0; i < nbm; i++)
		if (!tried[i])
			top = bm_place(tried, i, lo, top);
	return top;
}

/* placed modules in [lo, hi) are reached at their address plus d once the MMU is on */
void
dlm_bootrebase(lo, hi, d)
	unsigned long lo, hi, d;
{
	int i;

	for (i = 0; i < nbm; i++)
		if (bmst[i] == BM_PLACED && bm[i].bm_addr >= lo && bm[i].bm_addr < hi)
			bm[i].bm_addr += d;
}

/* the module of that name while the boot modules link, else 0 */
struct bootmod *
dlm_bootfind(name)
	char *name;
{
	int i;

	if (linking)
		for (i = 0; i < nbm; i++)
			if (bmst[i] == BM_PLACED &&
			    strcmp(bm[i].bm_name, name) == 0)
				return &bm[i];
	return 0;
}

/* before the root mounts; once */
void
dlm_bootlink()
{
	static int done = 0;
	int i, e;

	if (done || (nbm == 0 && lost == 0))
		return;
	done = 1;
	if (lost)
		printf("boot modules: %d over the limit of %d ignored\n",
		    lost, BM_MAX);
	if (!dlm_inited) {
		printf("boot modules: no module support, %d not linked\n", nbm);
		return;
	}
	linking = 1;
	for (i = 0; i < nbm; i++) {
		if (bmst[i] != BM_PLACED) {
			printf("boot module %s: %s\n", bm[i].bm_name,
			    bmst[i] ? "no room in memory" :
			    "not in kernel memory");
			continue;
		}
		if ((e = dlm_loadname(bm[i].bm_name)) == ENXIO)
			printf("boot module %s: no hardware\n", bm[i].bm_name);
		else if (e == ERELOC)
			printf("boot module %s: damaged file\n", bm[i].bm_name);
		else if (e != 0)
			printf("boot module %s: not linked, error %d\n",
			    bm[i].bm_name, e);
		else
			printf("boot module %s\n", bm[i].bm_name);
	}
	linking = 0;
}
