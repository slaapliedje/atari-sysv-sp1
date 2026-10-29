/*
 * dlm_asv.c -- Atari System V glue for the loadable-module core
 * (../upstream, Ash Nazag's DLM, built with -DDLM_ASV and asv.patch).
 *
 * dlminit()	the loader's start: the kernel configuration calls it from
 *		io_init (master.d "ox dlm"), before the first process.
 * spare rows	cunix sizes cdevsw to the drivers configured, and a
 *		character major reaches its row through MAJOR[] (external
 *		to internal; unused entries hold cdevcnt).  The placeholder
 *		drivers dlma .. dlmh (external majors DLM_SPARE_EXT ..)
 *		give rows with no entries; a module's major registered
 *		with modadm is pointed at one of them.
 * stext	the start of the kernel's text, which ASV's kernel has no
 *		symbol for: 0x400 (bootargs, then start at 0x440).
 * stubs	the exec-format, STREAMS-module and filesystem linkages
 *		replace stock tables and are not built for ASV yet.
 *
 * K&R C, built with the AMIX/ASV kernel flags (-traditional).
 */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/conf.h"
#include "sys/stream.h"
#include "dlm.h"

#define	DLM_SPARE_EXT	110	/* the placeholders' external majors */
#define	DLM_NSPARE	8

__asm__(".globl stext\n\t.set stext,0x400");

extern char	MINOR[];
extern int	dlm_cempty();

static int	spare_row[DLM_NSPARE];	/* internal row, -1 = none */
static int	spare_ext[DLM_NSPARE];	/* external major on it, -1 = free */
int		dlm_nspare;

/*
 * The offsets upstream reads module data and switch rows by, against
 * ASV's own structures: a mismatch fails the compile here.
 */
#define	CHECK(n, c)	static char n[(c) ? 1 : -1]
#define	OFF(t, f)	((int)&((t *)0)->f)
CHECK(ck_cdsz, sizeof (struct cdevsw) == 60);
CHECK(ck_cdxhalt, OFF(struct cdevsw, d_xhalt) == 36);
CHECK(ck_cddrv, OFF(struct cdevsw, d_driver) == 40);
CHECK(ck_cdttys, OFF(struct cdevsw, d_ttys) == 48);
CHECK(ck_cdstr, OFF(struct cdevsw, d_str) == 52);
CHECK(ck_cdflag, OFF(struct cdevsw, d_flag) == 56);
CHECK(ck_drvsz, sizeof (struct mod_drv_data) == 108);
CHECK(ck_drvbc, OFF(struct mod_drv_data, drv_bcount) == 36);
CHECK(ck_drvcd, OFF(struct mod_drv_data, drv_cdevsw) == 40);
CHECK(ck_drvcm, OFF(struct mod_drv_data, drv_cmajor) == 100);
CHECK(ck_drvcc, OFF(struct mod_drv_data, drv_ccount) == 104);
CHECK(ck_qisz, sizeof (struct qinit) == 28);
CHECK(ck_qiopen, OFF(struct qinit, qi_qopen) == 8);
CHECK(ck_qiclose, OFF(struct qinit, qi_qclose) == 12);
CHECK(ck_qiminfo, OFF(struct qinit, qi_minfo) == 20);
CHECK(ck_qimstat, OFF(struct qinit, qi_mstat) == 24);
CHECK(ck_stsz, sizeof (struct streamtab) == 16);

#define	ROW(e)	((int)(unsigned char)MAJOR[e])

void
dlminit()
{
	int i, e, r;

	for (i = 0; i < DLM_NSPARE; i++) {
		spare_row[i] = spare_ext[i] = -1;
		e = DLM_SPARE_EXT + i;
		r = ROW(e);
		if (r < cdevcnt && dlm_cempty(&cdevsw[r])) {
			spare_row[i] = r;
			MAJOR[e] = cdevcnt;	/* the placeholder's own major: none */
			dlm_nspare++;
		}
	}
	dlm_init();
	if (dlm_inited)
		printf("dlm: %d spare driver rows\n", dlm_nspare);
}

/*
 * The row for external major ext: the one it was given, or with take a
 * free spare row, which MAJOR[ext] then points at.  Negative errno: a
 * static driver has the major (EEXIST), no spare row is left (ECONFIG).
 */
int
dlm_asv_row(ext, take)
	int ext, take;
{
	int i;

	if (ext < 0 || ext >= DLM_SPARE_EXT)
		return -EINVAL;
	for (i = 0; i < DLM_NSPARE; i++)
		if (spare_ext[i] == ext)
			return spare_row[i];
	if (!take)
		return -ENXIO;
	if (ROW(ext) < cdevcnt)
		return -EEXIST;
	for (i = 0; i < DLM_NSPARE; i++)
		if (spare_row[i] >= 0 && spare_ext[i] < 0) {
			spare_ext[i] = ext;
			MINOR[ext] = 0;
			MAJOR[ext] = spare_row[i];
			return spare_row[i];
		}
	return -ECONFIG;
}

/* a registration that failed: the row is free again */
void
dlm_asv_unrow(row)
	int row;
{
	int i;

	for (i = 0; i < DLM_NSPARE; i++)
		if (spare_row[i] == row && spare_ext[i] >= 0) {
			MAJOR[spare_ext[i]] = cdevcnt;
			spare_ext[i] = -1;
		}
}

/* the external major a row serves, for modstat */
int
dlm_asv_ext(row)
	int row;
{
	int i;

	for (i = 0; i < DLM_NSPARE; i++)
		if (spare_row[i] == row && spare_ext[i] >= 0)
			return spare_ext[i];
	return row;
}

/* ---- linkages not built for ASV yet (dlm_fs.c) ---- */

void
dlm_fs_init()
{
}

int
dlm_fsbusy(m)
	struct dlm_mod *m;
{
	return 0;
}

static int
nofs()
{
	return EINVAL;
}

static void
nofsinfo()
{
}

struct mod_operations mod_fsops = { nofs, nofs, nofsinfo };
