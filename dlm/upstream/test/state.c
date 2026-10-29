/*
 * state -- host harness for dlm_core.c: the system calls, the module
 * list, ids, reference counts, dependencies, unload, the guard, and the
 * resolution rules that need more than one module.
 *
 *	state ksym.bin root
 *
 * root holds etc/conf/mod.d/<module> (built from mkmods.py).  One
 * PASS/FAIL line per check; exit 1 if any failed.
 *
 * K&R C.
 */

#include "dlm.h"

extern char *host_root;
extern long host_kbytes;
extern int host_intr_after, host_intr_open;
extern struct proc host_proc;
extern struct cred host_cred;
extern void host_handler();
extern char dlm_path[];
extern char dlm_ksym[];
extern unsigned long host_klo, host_khi;

int	fails, passes;

void
check(ok, what)
	int ok;
	char *what;
{
	printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (ok)
		passes++;
	else
		fails++;
}

int
call(n, a, rv)
	int n;
	char *a;
	int *rv;
{
	rval_t r;
	int e;

	r.r_val1 = -1;
	e = (*sysent[n].sy_call)(a, &r);
	if (rv)
		*rv = r.r_val1;
	return e;
}

int
modload(path, idp)
	char *path;
	int *idp;
{
	struct modloada a;

	a.path = path;
	return call(SYS_modload, (char *)&a, idp);
}

int
moduload(id)
	int id;
{
	struct moduloada a;

	a.id = id;
	return call(SYS_moduload, (char *)&a, (int *)0);
}

int
modstat(id, st, next)
	int id, next;
	struct modstatus *st;
{
	struct modstata a;

	a.id = id;
	a.st = st;
	a.next = next;
	return call(SYS_modstat, (char *)&a, (int *)0);
}

int
modpath(p)
	char *p;
{
	struct modpatha a;

	a.path = p;
	return call(SYS_modpath, (char *)&a, (int *)0);
}

int
modadm(type, cmd, arg)
	int type, cmd;
	char *arg;
{
	struct modadma a;

	a.type = type;
	a.cmd = cmd;
	a.arg = arg;
	return call(SYS_modadm, (char *)&a, (int *)0);
}

int
getksym(name, val, info)
	char *name;
	unsigned long *val, *info;
{
	struct getksyma a;

	a.name = name;
	a.value = val;
	a.info = info;
	return call(SYS_getksym, (char *)&a, (int *)0);
}

struct dlm_mod *
byname(n)
	char *n;
{
	struct dlm_mod *m;

	for (m = dlm_list; m; m = m->m_next)
		if (strcmp(m->m_name, n) == 0)
			return m;
	return 0;
}

int
nloaded()
{
	struct dlm_mod *m;
	int n = 0;

	for (m = dlm_list; m; m = m->m_next)
		n++;
	return n;
}

unsigned long
modsym(m, name)
	struct dlm_mod *m;
	char *name;
{
	unsigned long v = 0;

	dlm_blklookup(m->m_tab, name, &v, (int *)0);
	return v;
}

unsigned long
kval(name)
	char *name;
{
	unsigned long v = 0;

	dlm_blklookup(dlm_ksym, name, &v, (int *)0);
	return v;
}

unsigned long
word(m, a)
	struct dlm_mod *m;
	unsigned long a;
{
	return G32(DLM_RP(m, a));
}

/* the candidate list is consistent with the flags and the counts */
int
candok()
{
	struct dlm_mod *m, *p = 0;
	int n = 0, k = 0;

	for (m = dlm_cand; m; p = m, m = m->m_cnext) {
		if (m->m_cprev != p || !(m->m_flags & DM_CAND) ||
		    m->m_refs || m->m_deps || ++n > 1000)
			return 0;
	}
	for (m = dlm_list; m; m = m->m_next)
		if (m->m_flags & DM_CAND)
			k++;
	return n == k;
}

/* handlers bound to module routine names */
int ldf_rv, unf_rv;

int
h_ldf(m, a, b)
	char *m, *a;
	long b;
{
	return ldf_rv;
}

int
h_unf(m, a, b)
	char *m, *a;
	long b;
{
	return unf_rv;
}

/* xa_exec: records the hold it runs under; may longjmp or return xa_rv */
int xa_rv, xa_calls, xa_refs, xa_jmp;
long xa_a0;

int
h_xa(m, a, b)
	struct dlm_mod *m;
	char *a;
	long b;
{
	xa_calls++;
	xa_refs = m->m_refs;
	xa_a0 = (long)a;
	if (xa_jmp)
		longjmp(u.u_qsav.jb, 1);
	return xa_rv;
}

extern struct execsw execsw[], __amix_execsw[];
extern int nexectype, stock_calls, stock_rv;
extern char *h_one, *h_two, h_one_dflt[];

int
xreg(name, magic, flags)
	char *name;
	int magic, flags;
{
	struct mod_mreg reg;
	struct mod_execreg er;

	strcpy(reg.md_modname, name);
	reg.md_typedata = (caddr_t)&er;
	er.er_magic = magic;
	er.er_flags = flags;
	return modadm(MOD_TY_EXEC, MOD_C_MREG, (char *)&reg);
}

int
xexec(row)
	int row;
{
	return (*execsw[row].exec_func)(11L, 2L, 3L, 4L, 5L, 6L, 0L, 0L);
}

void
exec_tests()
{
	struct modstatus st;
	struct dlm_mod *m;
	label_t save;
	int e, i, id, ok;

	check(nexectype == 3 && execsw[0].exec_func == __amix_execsw[0].exec_func &&
	    execsw[2].exec_magic == __amix_execsw[2].exec_magic,
	    "exec: dlm_init copied the stock execsw rows");
	check(xreg("xa", 0x150, EXF_FIRST) == 0 && nexectype == 4 &&
	    *execsw[0].exec_magic == 0x150 &&
	    execsw[1].exec_func == __amix_execsw[0].exec_func &&
	    execsw[0].exec_core == __amix_execsw[0].exec_core,
	    "exec: EXF_FIRST row goes before the stock 0x150 row, with its core");
	check(xreg("xa", 0x150, EXF_FIRST) == 0 && nexectype == 4,
	    "exec: re-registration is idempotent");
	check(xreg("xz", 0x150, EXF_FIRST) == EEXIST, "exec: magic of another module -> EEXIST");
	check(xreg("xz", 0x7f45, 0) == EEXIST, "exec: appended behind a static row -> EEXIST");

	stock_calls = xa_calls = 0;
	xa_rv = 0;
	e = xexec(0);
	m = byname("xa");
	check(e == 0 && m && xa_calls == 1 && xa_refs == 1 && xa_a0 == 11 &&
	    m->m_refs == 0 && (m->m_flags & DM_CAND) && stock_calls == 0 && candok(),
	    "exec: first use loads xa, holds it across the call, releases");
	xa_rv = ENOEXEC;
	e = xexec(0);
	check(e == stock_rv && stock_calls == 1 && xa_calls == 2,
	    "exec: ENOEXEC falls through to the stock row");
	xa_rv = E2BIG;
	check(xexec(0) == E2BIG && stock_calls == 1, "exec: other errors are returned");
	xa_rv = 0;
	xa_jmp = 1;
	bcopy((char *)&u.u_qsav, (char *)&save, sizeof save);
	e = 0;
	if (setjmp(u.u_qsav.jb))
		e = 1;
	else
		(void)xexec(0);
	bcopy((char *)&save, (char *)&u.u_qsav, sizeof save);
	xa_jmp = 0;
	check(e == 1 && m->m_refs == 0 && candok(),
	    "exec: a longjmp through the trampoline drops the hold and goes on");
	check(modstat(m->m_id, &st, 0) == 0 && st.ms_msinfo[0].mss_type == MOD_TY_EXEC &&
	    st.ms_msinfo[0].mss_p0[0] == 0x150 && st.ms_msinfo[0].mss_p0[1] == 0,
	    "exec: modstat reports magic and row");
	check(moduload(m->m_id) == 0 && !byname("xa"), "exec: idle module unloads");
	dlm_maximage = 16;
	e = xexec(0);
	dlm_maximage = 2 * 1024 * 1024;
	check(e == stock_rv && stock_calls == 2 && !byname("xa"),
	    "exec: load failure falls through");
	check(xreg("xb", 0x152, EXF_FIRST) == 0 && nexectype == 5 &&
	    *execsw[4].exec_magic == 0x152 && xexec(4) == ENOEXEC && !byname("xb"),
	    "exec: module without that magic in its data -> ENOEXEC, not loaded");
	ok = 1;
	for (i = 0; i < 6; i++)
		ok &= xreg("f", 0x7001 + i, 0) == 0;
	check(ok && nexectype == 11 && xreg("f", 0x7010, 0) == ECONFIG,
	    "exec: eight slots, then ECONFIG");
	check(xexec(10) == ENOEXEC, "exec: unloadable, no fall-through -> ENOEXEC");
	(void)id;
}

void
hook_tests()
{
	struct dlm_mod *m;
	int id, e;

	e = modload("hk", &id);
	m = byname("hk");
	check(e == 0 && m && h_one == (char *)modsym(m, "hk_f0") &&
	    h_two == (char *)modsym(m, "hk_f1") && h_one != 0,
	    "hook: module sets both hooks");
	check(modload("hk2", &id) == EEXIST && !byname("hk2") &&
	    h_one == (char *)modsym(m, "hk_f0"), "hook: owned hook -> EEXIST");
	check(modload("hk3", &id) == EINVAL && !byname("hk3"),
	    "hook: unknown name -> EINVAL");
	check(moduload(m->m_id) == 0 && h_one == h_one_dflt && h_two == 0,
	    "hook: unload restores the defaults");
	e = modload("hk2", &id);
	check(e == 0 && h_one == (char *)modsym(byname("hk2"), "hk2_f0"),
	    "hook: free again after unload");
	moduload(0);
}

/* cd_open / cd_close: count calls, return cd_rv */
int cd_rv, cd_opens, cd_closes;

int
h_cdopen(m, a, b)
	struct dlm_mod *m;
	char *a;
	long b;
{
	cd_opens++;
	return cd_rv;
}

int
h_cdclose(m, a, b)
	struct dlm_mod *m;
	char *a;
	long b;
{
	cd_closes++;
	return 0;
}

extern struct cdevsw cdevsw[];
extern int dlm_cdev_open(), dlm_cdev_close(), dlm_stale_close;
extern void hconf_cdev();

int
creg(name, mj)
	char *name;
	int mj;
{
	struct mod_mreg reg;

	strcpy(reg.md_modname, name);
	reg.md_typedata = (caddr_t)&mj;
	return modadm(MOD_TY_CDEV, MOD_C_MREG, (char *)&reg);
}

int
copen(mj, mn, otyp)
	int mj, mn, otyp;
{
	dev_t d = makedevice(mj, mn);

	return (*cdevsw[mj].d_open)(&d, 3, otyp, (struct cred *)0);
}

int
cclose(mj, mn, otyp)
	int mj, mn, otyp;
{
	return (*cdevsw[mj].d_close)(makedevice(mj, mn), 3, otyp, (struct cred *)0);
}

void
drv_tests()
{
	struct modstatus st;
	struct dlm_mod *m;
	int e;

	check(creg("cd", 60) == 0 && cdevsw[60].d_open == dlm_cdev_open &&
	    cdevsw[60].d_close == dlm_cdev_close && cdevsw[60].d_read == nodev,
	    "cdev: registration puts the trampolines in the empty row");
	check(creg("cd", 60) == 0, "cdev: re-registration is idempotent");
	check(creg("zz", 60) == EEXIST && creg("zz", 3) == EEXIST,
	    "cdev: another module's major or a used row -> EEXIST");
	check(creg("zz", 70) == ECONFIG && creg("zz", -1) == ECONFIG,
	    "cdev: major outside the switch -> ECONFIG");
	cd_rv = 0;
	e = copen(60, 0, OTYP_CHR);
	m = byname("cd");
	check(e == 0 && m && cd_opens == 1 && m->m_refs == 1 &&
	    cdevsw[60].d_read == (int (*)())modsym(m, "cd_read") &&
	    cdevsw[60].d_ioctl == (int (*)())modsym(m, "cd_ioctl") &&
	    cdevsw[60].d_write == nodev && cdevsw[60].d_open == dlm_cdev_open,
	    "cdev: first open loads the module, fills the row, holds it");
	check(copen(60, 0, OTYP_CHR) == 0 && m->m_refs == 1 && cd_opens == 2,
	    "cdev: reopen of an open key holds nothing more");
	check(copen(60, 1, OTYP_CHR) == 0 && m->m_refs == 2, "cdev: another minor holds once more");
	check(copen(60, 2, OTYP_LYR) == 0 && copen(60, 2, OTYP_LYR) == 0 && m->m_refs == 4,
	    "cdev: layered opens hold per call");
	check(cclose(60, 2, OTYP_LYR) == 0 && cclose(60, 2, OTYP_LYR) == 0 && m->m_refs == 2,
	    "cdev: layered closes release per call");
	check(moduload(m->m_id) == EBUSY, "cdev: open driver does not unload");
	check(modstat(m->m_id, &st, 0) == 0 && st.ms_msinfo[0].mss_type == MOD_TY_CDEV &&
	    st.ms_msinfo[0].mss_p1[0] == 60 && st.ms_msinfo[0].mss_p1[1] == 1,
	    "cdev: modstat reports the major");
	check(cclose(60, 0, OTYP_CHR) == 0 && cclose(60, 1, OTYP_CHR) == 0 &&
	    cd_closes == 4 && m->m_refs == 0 && (m->m_flags & DM_CAND) && candok(),
	    "cdev: last closes release, module becomes a candidate");
	e = dlm_stale_close;
	check(cclose(60, 5, OTYP_CHR) == 0 && dlm_stale_close == e + 1 && m->m_refs == 0,
	    "cdev: close of a key never opened is counted, not released");
	cd_rv = ENXIO;
	check(copen(60, 0, OTYP_CHR) == ENXIO && m->m_refs == 0, "cdev: failed open releases");
	cd_rv = 0;
	check(moduload(m->m_id) == 0 && !byname("cd") && cdevsw[60].d_read == nodev &&
	    cdevsw[60].d_open == dlm_cdev_open, "cdev: unload puts the placeholder back");
	check(creg("cdold", 61) == 0 && copen(61, 0, OTYP_CHR) == ENXIO && !byname("cdold"),
	    "cdev: D_OLD driver is refused");
	check(creg("cdblk", 62) == 0 && copen(62, 0, OTYP_CHR) == ENXIO && !byname("cdblk"),
	    "cdev: block majors are refused");
	check(creg("zz", 63) == 0 && copen(63, 0, OTYP_CHR) == ENXIO && !byname("zz"),
	    "cdev: module that cannot be loaded -> ENXIO");
	check(copen(60, 0, OTYP_CHR) == 0 && byname("cd") && cclose(60, 0, OTYP_CHR) == 0,
	    "cdev: loads again after unload");
	moduload(0);
}

/* kernel table plus the names the harness modules need */
void
kernel_table(path)
	char *path;
{
	FILE *f = fopen(path, "rb");
	static char *ops[4] = { "mod_miscops", "mod_execops", "mod_hookops",
	    "mod_drvops" };
	char *b, *nb, *s, *e;
	long len, n, i, k, ss, so, miss[4];
	unsigned long v;

	if (!f) {
		perror(path);
		exit(2);
	}
	b = calloc(1, KSYM_SPACE);
	len = fread(b, 1, KSYM_SPACE, f);
	fclose(f);
	if (dlm_blkcheck(b, len) != 0) {
		fprintf(stderr, "bad kernel table %s\n", path);
		exit(2);
	}
	for (k = i = 0; i < 4; i++)
		if (!dlm_blklookup(b, ops[i], &v, (int *)0))
			miss[k++] = i;
	if (k == 0) {
		memcpy(dlm_ksym, b, len);
	} else {
		/* rebuild with the missing linkage ops at fake addresses */
		n = G32(b + KH_NSYM);
		ss = G32(b + KH_STRSIZE) + 12 * k;
		nb = calloc(1, dlm_blksize(n + k, ss, (long)dlm_prime((n + k) / 4)));
		dlm_blkinit(nb, n + k, ss, (long)dlm_prime((n + k) / 4),
		    G32(b + KH_LO), G32(b + KH_HI));
		memcpy(nb + G32(nb + KH_STROFF), b + G32(b + KH_STROFF),
		    G32(b + KH_STRSIZE));
		memcpy(nb + G32(nb + KH_SYMOFF), b + G32(b + KH_SYMOFF), n * SYMSZ);
		so = G32(b + KH_STRSIZE);
		for (i = 0; i < k; i++, so += 12) {
			strcpy(nb + G32(nb + KH_STROFF) + so, ops[miss[i]]);
			e = nb + G32(nb + KH_SYMOFF) + (n + i) * SYMSZ;
			P32(e + ST_NAME, so);
			P32(e + ST_VALUE, 0x00abc000L + 0x10 * miss[i]);
			e[ST_INFO] = (STB_GLOBAL << 4) | STT_OBJECT;
			P16(e + ST_SHNDX, SHN_ABS);
		}
		dlm_blkhash(nb);
		if (G32(nb + KH_SIZE) > KSYM_SPACE) {
			fprintf(stderr, "table too large\n");
			exit(2);
		}
		memcpy(dlm_ksym, nb, G32(nb + KH_SIZE));
	}
	host_klo = G32(dlm_ksym + KH_LO);
	host_khi = G32(dlm_ksym + KH_HI);
	(void)s;
	(void)i;
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	struct modstatus st;
	struct dlm_mod *m, *m1, *ma, *mb, *mc;
	struct mod_mreg reg;
	int id, id1, id2, id3, e, i, k, n, seen[4];
	long base;
	unsigned long v, info;
	char name[MAXSYMNMLEN + 8], big[300];
	unsigned seed = 12345;

	if (argc != 3) {
		fprintf(stderr, "usage: state ksym.bin root\n");
		return 2;
	}
	kernel_table(argv[1]);
	host_root = argv[2];
	u.u_procp = &host_proc;
	host_proc.p_cred = &host_cred;
	for (i = 0; i < 142; i++)
		sysent[i].sy_call = nosys;
	host_handler("ldf_load", h_ldf);
	host_handler("unf_unload", h_unf);

	/* ---- initialisation ---- */
	check(dlm_load("m1", 0, 1, (struct dlm_guard *)0, &m) == ENOSYS,
	    "uninitialised: ENOSYS");
	dlm_init();
	check(dlm_inited && sysent[64].sy_call != nosys &&
	    sysent[69].sy_call != nosys && sysent[70].sy_call == nosys &&
	    sysent[67].sy_narg == 3 && sysent[64].sy_flags == SETJUMP,
	    "dlm_init writes sysent[64..69], narg and SETJUMP");
	dlm_inited = 0;
	dlm_init();
	check(!dlm_inited, "dlm_init refuses when a claimed slot is not nosys");
	dlm_inited = 1;
	check(strcmp(dlm_path, "/etc/conf/mod.d") == 0, "default path");
	base = host_kbytes;

	/* ---- ids, gaps, demand ---- */
	e = modload("m1", &id1);
	check(e == 0 && id1 == 1, "modload m1 -> id 1");
	e = modload("rev2", &id);
	check(e == EBADVER, "wrapper revision 2 -> EBADVER");
	e = modload("m2", &id2);
	check(e == 0 && id2 == 3, "failed load consumed id 2; m2 -> id 3");
	e = modload("/etc/conf/mod.d/m1", &id);
	check(e == 0 && id == id1, "modload of a loaded module returns its id");
	e = modload("m3", &id3);
	check(e == 0 && id3 == 4, "m3 -> id 4");
	m1 = byname("m1");
	check(m1 && (m1->m_flags & DM_DEMAND) && m1->m_nlink == 1 &&
	    m1->m_delay == 60 * HZ && !(m1->m_flags & DM_CAND),
	    "demand mark, one linkage, default delay, not a candidate");

	/* ---- modstat ---- */
	e = modstat(id1, &st, 0);
	check(e == 0 && st.ms_id == 1 && strcmp(st.ms_name, "m1") == 0 &&
	    strcmp(st.ms_path, "/etc/conf/mod.d/m1") == 0 && st.ms_rev == 1 &&
	    st.ms_flags == MS_DEMAND && st.ms_unload_delay == 60 &&
	    strcmp(st.ms_msinfo[0].mss_linkinfo, "m1 test module") == 0 &&
	    st.ms_msinfo[0].mss_type == MOD_TY_MISC &&
	    st.ms_base == (caddr_t)m1->m_run && st.ms_size == m1->m_size,
	    "modstat fields");
	check(modstat(2, &st, 0) == EINVAL, "modstat of a consumed id -> EINVAL");
	check(modstat(id1, (struct modstatus *)1, 0) == EFAULT,
	    "modstat bad buffer -> EFAULT");
	byname("m2")->m_flags |= DM_LOADING;
	n = 0;
	for (k = 1; modstat(k, &st, 1) == 0; k = st.ms_id + 1)
		seen[n++ & 3] = st.ms_id;
	check(n == 2 && seen[0] == 1 && seen[1] == 4,
	    "modstat iteration skips a loading record (1, 4)");
	check(modstat(id2, &st, 0) == EINVAL, "modstat of a loading record -> EINVAL");
	check(moduload(id2) == EBUSY, "moduload of a loading record -> EBUSY");
	byname("m2")->m_flags &= ~DM_LOADING;

	/* ---- getksym ---- */
	v = 0;
	e = getksym("printf", &v, &info);
	check(e == 0 && v == kval("printf") && info == STT_FUNC,
	    "getksym printf -> value, STT_FUNC");
	v = kval("printf") + 4;
	strcpy(name, "");
	e = getksym(name, &v, &info);
	check(e == 0 && strcmp(name, "printf") == 0 && info == 4,
	    "getksym by address -> printf + 4");
	v = 0;
	e = getksym("m1_load", &v, &info);
	check(e == 0 && v == modsym(m1, "m1_load") && v >= m1->m_run &&
	    v < m1->m_run + m1->m_size, "getksym finds a module symbol");
	v = modsym(m1, "m1_unload") + 2;
	e = getksym(name, &v, &info);
	check(e == 0 && strcmp(name, "m1_unload") == 0 && info == 2,
	    "getksym by address inside a module");
	v = 0;
	check(getksym("no_such_symbol", &v, &info) == ENOMATCH, "unknown -> ENOMATCH");
	check(getksym("printf", (unsigned long *)1, &info) == EFAULT,
	    "getksym bad pointer -> EFAULT");
	memset(big, 'a', 299);
	big[299] = 0;
	v = 0;
	check(getksym(big, &v, &info) == ENAMETOOLONG, "getksym 299-char name");
	v = 0x7fffffff;
	check(getksym(name, &v, &info) == ENOMATCH, "address outside all images");

	/* ---- privilege ---- */
	host_cred.cr_uid = 1;
	check(modload("m1", &id) == EPERM && moduload(1) == EPERM &&
	    modpath("/x") == EPERM && modstat(1, &st, 0) == EPERM &&
	    modadm(MOD_TY_MISC, MOD_C_MREG, (char *)&reg) == EPERM,
	    "non-root: EPERM");
	v = 0;
	check(getksym("printf", &v, &info) == 0, "non-root: getksym allowed");
	host_cred.cr_uid = 0;

	/* ---- mod_hold / mod_rele ---- */
	mod_hold((struct modwrapper *)m1->m_wrapper);
	check(m1->m_refs == 1 && moduload(id1) == EBUSY &&
	    !(m1->m_flags & DM_DEMAND), "held: moduload EBUSY, demand cleared");
	mod_rele((struct modwrapper *)m1->m_wrapper);
	check(m1->m_refs == 0 && (m1->m_flags & DM_CAND) && candok(),
	    "released: candidate");

	/* ---- moduload(0) over a 3-chain ---- */
	e = modload("ha", &id);
	ma = byname("ha");
	mb = byname("hb");
	mc = byname("hc");
	check(e == 0 && ma && mb && mc && mb->m_deps == 1 && mc->m_deps == 1 &&
	    !(mb->m_flags & DM_DEMAND) && (ma->m_flags & DM_DEMAND) &&
	    ma->m_ndep == 1 && ma->m_dep[0] == mb && ma->m_id < mb->m_id &&
	    mb->m_id < mc->m_id, "ha -> hb -> hc loaded, deps counted, ids in record order");
	check(moduload(mc->m_id) == EBUSY, "dependency in use -> EBUSY");
	e = modload("hb", &id);
	check(e == 0 && id == mb->m_id && (mb->m_flags & DM_DEMAND),
	    "modload of an auto-loaded dependency sets the demand mark");
	check(moduload(0) == 0 && nloaded() == 0 && dlm_cand == 0 && candok(),
	    "moduload(0) unloads everything, chain included");
	check(host_kbytes - base <= (long)sizeof (struct cred),
	    "memory back to the baseline");
	check(moduload(0) == EINVAL && moduload(1) == EINVAL,
	    "nothing loaded -> EINVAL");

	/* ---- dependency errors ---- */
	check(modload("cyca", &id) == EINVAL && nloaded() == 0, "cycle -> EINVAL");
	check(modload("d1", &id) == EINVAL && nloaded() == 0, "depth 9 -> EINVAL");
	check(modload("d2", &id) == 0 && nloaded() == 8, "depth 8 loads");
	check(moduload(0) == 0 && nloaded() == 0, "and unloads");
	check(modload("tra", &id) == ERELOC && !byname("tra") &&
	    byname("trb") && byname("trc") && byname("trb")->m_deps == 0 &&
	    (byname("trb")->m_flags & DM_CAND) && byname("trc")->m_deps == 1 &&
	    candok(), "transitive dependency's symbol not searched -> ERELOC; "
	    "released dependency becomes a candidate");
	moduload(0);
	e = modload("multi", &id);
	m = byname("multi");
	check(e == 0 && m && m->m_ndep == 3 && byname("hc") && byname("shd") &&
	    byname("trc"), "all three dependencies honoured");
	moduload(0);
	e = modload("usest", &id);
	m = byname("usest");
	check(e == 0 && m && m->m_ndep == 0 && nloaded() == 1,
	    "static dependency adds nothing");
	check(modload("stat1", &id) == EINVAL, "static module name -> EINVAL");
	moduload(0);

	/* ---- resolution ---- */
	e = modload("shu", &id);
	m = byname("shu");
	check(e == 0 && word(m, modsym(m, "shu_ref")) == modsym(byname("shd"), "bcmp") &&
	    word(m, modsym(m, "shu_ref")) != kval("bcmp"),
	    "dependency definition shadows the kernel's");
	e = modload("wk", &id);
	m = byname("wk");
	check(e == 0 && word(m, modsym(m, "wk_ref")) == 0 &&
	    modsym(m, "wk_none") == 0, "weak undefined -> 0, not in the table");
	e = modload("cm", &id);
	m = byname("cm");
	v = word(m, modsym(m, "cm_ref") + 4);
	check(e == 0 && word(m, modsym(m, "cm_ref")) == kval("lbolt") &&
	    v == modsym(m, "cm_own") && v >= m->m_run &&
	    v + 64 <= m->m_run + m->m_size && (v & 3) == 0 &&
	    modsym(m, "lbolt") == 0,
	    "common bound to the kernel's lbolt; cm_own allocated in the image");
	moduload(0);

	/* ---- load and unload routines ---- */
	base = host_kbytes;
	ldf_rv = ENXIO;
	check(modload("ldf", &id) == ENXIO && nloaded() == 0 && host_kbytes == base,
	    "_load error passed through, all freed");
	unf_rv = EBUSY;
	e = modload("unf", &id);
	m = byname("unf");
	check(e == 0 && moduload(id) == EBUSY && byname("unf") == m &&
	    m->m_nlink == 1 && !(m->m_flags & DM_TRANS),
	    "_unload EBUSY: stays loaded, linkage reinstalled");
	unf_rv = 0;
	check(moduload(id) == 0 && nloaded() == 0 && host_kbytes == base,
	    "then unloads");

	/* ---- limits ---- */
	check(modload("/etc/conf/mod.d/abcdefghijklmno", &id) == ENAMETOOLONG,
	    "15-character name -> ENAMETOOLONG");
	check(modload("nosuch", &id) == ENOENT, "missing file -> ENOENT");
	dlm_maximage = 1024;
	check(modload("big", &id) == ENOMEM && nloaded() == 0, "over dlm_maximage -> ENOMEM");
	dlm_maximage = 2 * 1024 * 1024;

	/* ---- modpath ---- */
	check(modpath("/nonexist:/also/missing") == 0 &&
	    strcmp(dlm_path, "/nonexist:/also/missing:/etc/conf/mod.d") == 0,
	    "modpath prepends");
	check(modload("m1", &id) == 0, "missing directories skipped");
	check(modpath("rel") == EINVAL && modpath("/a::/b") == EINVAL &&
	    modpath("/a: b") == EINVAL && modpath("/a:") == EINVAL,
	    "modpath element checks");
	check(modpath((char *)1) == EFAULT, "modpath bad pointer -> EFAULT");
	memset(big, '/', 299);
	for (i = 0; i < 4; i++)
		e = modpath(big);
	check(e == EINVAL, "modpath total over MAXPATHLEN -> EINVAL");
	check(modpath((char *)0) == 0 && strcmp(dlm_path, "/etc/conf/mod.d") == 0,
	    "modpath(NULL) resets");
	moduload(0);

	/* ---- modadm ---- */
	strcpy(reg.md_modname, "m1");
	reg.md_typedata = 0;
	check(modadm(MOD_TY_MISC, MOD_C_MREG, (char *)&reg) == 0, "register MISC");
	check(modadm(MOD_TY_NONE, MOD_C_MREG, (char *)&reg) == EINVAL &&
	    modadm(9, MOD_C_MREG, (char *)&reg) == EINVAL &&
	    modadm(MOD_TY_MISC, 7, (char *)&reg) == EINVAL, "bad type/cmd -> EINVAL");
	check(modadm(MOD_TY_MISC, MOD_C_MREG, (char *)1) == EFAULT, "bad arg -> EFAULT");
	memset(reg.md_modname, 'x', MODMAXNAMELEN);
	check(modadm(MOD_TY_MISC, MOD_C_MREG, (char *)&reg) == EINVAL,
	    "unterminated name -> EINVAL");

	/* ---- guard: a signal while loading ---- */
	base = host_kbytes;
	k = dlm_nextid;
	host_intr_after = 12;
	e = modload("ha", &id);
	check(e == EINTR && nloaded() == 0 && host_kbytes == base &&
	    dlm_nextid > k && host_proc.p_cred == &host_cred,
	    "EINTR mid-chain: everything unwound, ids consumed, cred restored");
	host_intr_after = 0;
	check(modload("ha", &id) == 0 && nloaded() == 3, "then loads");
	moduload(0);

	/* interrupt each open and each read of that load in turn */
	base = host_kbytes;
	k = 1;
	for (n = 0; n < 2 && k; n++)
		for (i = 1; k; i++) {
			if (n)
				host_intr_after = i;
			else
				host_intr_open = i;
			e = modload("ha", &id);
			host_intr_after = host_intr_open = 0;
			if (e == 0) {
				moduload(0);
				k = host_kbytes == base;
				break;
			}
			/* settled dependencies stay: candidates, or held by one */
			k = e == EINTR && host_proc.p_cred == &host_cred;
			for (m = dlm_list; m; m = m->m_next)
				if ((m->m_flags & (DM_DEMAND | DM_TRANS)) || m->m_refs ||
				    !(m->m_flags & DM_CAND) == !m->m_deps)
					k = 0;
			if (dlm_list)
				moduload(0);
			k = k && nloaded() == 0 && host_kbytes == base;
		}
	check(k, "EINTR at every open and read of a 3-chain: nothing leaks");

	/* ---- candidate list under random counts ---- */
	modload("m1", &id);
	modload("m2", &id);
	modload("m3", &id);
	k = 1;
	for (i = 0; i < 20000 && k; i++) {
		seed = seed * 1103515245 + 12345;
		m = dlm_list;
		for (n = (seed >> 16) % 3; n > 0; n--)
			m = m->m_next;
		switch ((seed >> 8) % 4) {
		case 0: dlm_hold(m); break;
		case 1: if (m->m_refs) dlm_rele(m); break;
		case 2: dlm_depadd(m); break;
		case 3: if (m->m_deps) dlm_deprele(m); break;
		}
		k = candok();
	}
	check(k, "candidate list consistent over 20000 random hold/rele/dep ops");
	for (m = dlm_list; m; m = m->m_next) {
		while (m->m_refs)
			dlm_rele(m);
		while (m->m_deps)
			dlm_deprele(m);
	}
	check(candok() && dlm_cand && moduload(0) == 0 && dlm_cand == 0,
	    "all released: all candidates, then unloaded");

	host_handler("xa_exec", h_xa);
	exec_tests();
	hook_tests();
	hconf_cdev();
	host_handler("cd_open", h_cdopen);
	host_handler("cd_close", h_cdclose);
	drv_tests();

	printf("%d passed, %d failed\n", passes, fails);
	return fails != 0;
}
