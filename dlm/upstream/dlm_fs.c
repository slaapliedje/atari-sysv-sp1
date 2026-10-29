/*
 * dlm_fs.c -- filesystem-type linkage.
 *
 * vfssw is replaced by a larger table; dlm_fs_init copies the stock
 * rows in from __amix_vfssw and makes nfstype cover the spare rows.
 * A spare row has an empty name, which lookups never match, and
 * operations that fail, so the stock walks over every row (root
 * mount, sync, vfsinit) pass it by.  A module claims a spare row on
 * install; its init routine sets the row's vfsops, as a stock
 * vsw_init does.
 *
 * vfs_getvfssw wraps the stock lookup: a name it misses is loaded as
 * the module of that name, for the superuser once root is mounted,
 * and looked up again.
 *
 * K&R C.
 */

#include "dlm.h"
#include "sys/vfs.h"

#define	VFS_STATIC	12
#define	VFS_RESERVE	8
#define	NVFSSW		(VFS_STATIC + VFS_RESERVE)
#define	FSDSZ		12		/* struct mod_fs_data */

struct vfssw	vfssw[NVFSSW] = { { 0 } };	/* in .data, as the stock table */
extern struct vfssw __amix_vfssw[];
extern struct vfssw *__amix_vfs_getvfssw();
extern struct vnode *rootdir;

static int fs_nstatic;
static char fs_name[VFS_RESERVE][MODMAXNAMELEN];
static struct dlm_mod *fs_mod[VFS_RESERVE];

static int
fs_einval()
{
	return EINVAL;
}

static int
fs_zero()
{
	return 0;
}

static struct vfsops fs_spareops = {
	fs_einval, fs_einval, fs_einval, fs_einval,
	fs_zero, fs_einval, fs_einval, fs_einval
};

static void
spare(vp)
	struct vfssw *vp;
{
	vp->vsw_name = "";
	vp->vsw_init = fs_zero;
	vp->vsw_vfsops = &fs_spareops;
	vp->vsw_flag = 0;
}

/* at boot, before vfsinit */
void
dlm_fs_init()
{
	int i;

	fs_nstatic = nfstype < VFS_STATIC ? nfstype : VFS_STATIC;
	for (i = 0; i < fs_nstatic; i++)
		vfssw[i] = __amix_vfssw[i];
	for (; i < NVFSSW; i++)
		spare(&vfssw[i]);
	nfstype = NVFSSW;
}

struct vfssw *
vfs_getvfssw(name)
	char *name;
{
	struct vfssw *vp;
	int i;

	if ((vp = __amix_vfs_getvfssw(name)) != 0 || !dlm_inited ||
	    rootdir == 0 || u.u_cred->cr_uid != 0)
		return vp;
	for (i = 0; name[i]; i++)
		if (name[i] == '/' || i == MODMAXNAMELEN - 1)
			return 0;
	if (i == 0 || dlm_loadname(name) != 0)
		return 0;
	return __amix_vfs_getvfssw(name);
}

static int
inimg(m, a, n)
	struct dlm_mod *m;
	unsigned long a, n;
{
	return a >= m->m_run && n <= m->m_size && a - m->m_run <= m->m_size - n;
}

/* a mounted vfs uses one of m's rows */
int
dlm_fsbusy(m)
	struct dlm_mod *m;
{
	struct vfs *vfsp;
	int k;

	for (k = 0; k < VFS_RESERVE; k++)
		if (fs_mod[k] == m)
			for (vfsp = rootvfs; vfsp; vfsp = vfsp->vfs_next)
				if (vfsp->vfs_fstype == fs_nstatic + k ||
				    vfsp->vfs_op == vfssw[fs_nstatic + k].vsw_vfsops)
					return 1;
	return 0;
}

/* td: run address of the mod_type_data; its mtd_pdata is the mod_fs_data */
static int
fs_install(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	struct vfssw *vp;
	char name[MODMAXNAMELEN];
	unsigned long d, a, f;
	int i, k, e;

	if (!inimg(m, td, 8L))
		return ERELOC;
	d = G32(DLM_RP(m, td) + 4);
	if (!inimg(m, d, (unsigned long)FSDSZ))
		return ERELOC;
	a = G32(DLM_RP(m, d));
	f = G32(DLM_RP(m, d) + 4);
	if (!inimg(m, f, 2L))
		return ERELOC;
	for (i = 0; i < MODMAXNAMELEN; i++) {
		if (!inimg(m, a + i, 1L))
			return ERELOC;
		if ((name[i] = *DLM_RP(m, a + i)) == 0)
			break;
	}
	if (i == 0 || i == MODMAXNAMELEN)
		return EINVAL;
	if (__amix_vfs_getvfssw(name))
		return EEXIST;
	for (k = 0; k < VFS_RESERVE && fs_mod[k]; k++)
		;
	if (k == VFS_RESERVE)
		return ECONFIG;
	vp = &vfssw[fs_nstatic + k];
	fs_mod[k] = m;
	strcpy(fs_name[k], name);
	vp->vsw_name = fs_name[k];	/* some inits find their row by name */
	e = DLM_CALL(m, f, vp, fs_nstatic + k);
	if (e == 0 && (vp->vsw_vfsops == 0 || vp->vsw_vfsops == &fs_spareops))
		e = EINVAL;
	if (e) {
		spare(vp);
		fs_mod[k] = 0;
		return e;
	}
	vp->vsw_init = (int (*)())f;
	vp->vsw_flag = S32(G32(DLM_RP(m, d) + 8));
	return 0;
}

static int
fs_remove(m, td)
	struct dlm_mod *m;
	unsigned long td;
{
	int k;

	for (k = 0; k < VFS_RESERVE; k++)
		if (fs_mod[k] == m) {
			spare(&vfssw[fs_nstatic + k]);
			fs_mod[k] = 0;
		}
	return 0;
}

static void
fs_info(m, td, st)
	struct dlm_mod *m;
	unsigned long td;
	struct modspecific_stat *st;
{
	int k;

	st->mss_type = MOD_TY_FS;
	st->mss_p0[0] = st->mss_p0[1] = st->mss_p1[0] = st->mss_p1[1] = -1;
	for (k = 0; k < VFS_RESERVE; k++)
		if (fs_mod[k] == m)
			st->mss_p0[0] = fs_nstatic + k;
}

struct mod_operations mod_fsops = { fs_install, fs_remove, fs_info };
