/*
 * hconf.c -- dlmconf.c's tunables for the state harness, with one
 * static module name.
 */

#include "dlm.h"

long	dlm_maximage = 2 * 1024 * 1024;
int	dlm_verbose = 0;
int	dlm_def_unload_delay = 60;
int	dlm_unload_wake = 60;
char	*dlm_static[] = { "stat1", 0 };

/* stock exec rows and a hook table, as the static kernel has them */
int	stock_calls, stock_rv = 42;
static short coffmagic = 0x150, elfmagic = 0x7f45, intpmagic = 0x2321;

int
stock_coff(a0, a1, a2, a3, a4, a5, a6, a7)
	long a0, a1, a2, a3, a4, a5, a6, a7;
{
	stock_calls++;
	return stock_rv;
}

int
stock_core()
{
	return 0;
}

int
stock_other()
{
	return ENOEXEC;
}

struct execsw __amix_execsw[3] = {
	{ &coffmagic, stock_coff, stock_core },
	{ &elfmagic, stock_other, 0 },
	{ &intpmagic, stock_other, 0 },
};
int	nexectype = 3;

char	h_one_dflt[1];
char	*h_one = h_one_dflt, *h_two;
struct hooksw hooksw[] = {
	{ "h_one", &h_one, h_one_dflt, 0 },
	{ "h_two", &h_two, 0, 0 },
	{ 0, 0, 0, 0 },
};

/* character switch: row 3 in use, the rest empty */
static int zflag[1];
int cdevcnt = 70;
struct cdevsw cdevsw[70];

int
hconf_used()
{
	return 0;
}

void
hconf_cdev()
{
	int i, k;

	for (i = 0; i < 70; i++) {
		for (k = 0; k < 10; k++)
			(&cdevsw[i].d_open)[k] = nodev;
		cdevsw[i].d_flag = zflag;
	}
	cdevsw[3].d_open = hconf_used;
}
