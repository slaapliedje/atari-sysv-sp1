/*
 * dlmconf.c -- loadable-module tunables, the static-module list, and
 * dmainit(), which replaces the empty stock stub.
 *
 * K&R C.
 */

#include "dlm.h"

long	dlm_maximage = 2 * 1024 * 1024;	/* largest image, bytes */
int	dlm_verbose = 0;		/* undefined symbols on the console */
int	dlm_def_unload_delay = 60;	/* seconds (DEF_UNLOAD_DELAY) */
int	dlm_unload_wake = 60;		/* seconds (UNLOAD_WAKE) */

/*
 * Names of modules configured into the static kernel; modload refuses
 * them.  Sorted, NULL-terminated.
 */
char	*dlm_static[] = {
	0
};

#ifndef DLM_HOST
extern void plat_dmainit();

/* startup() calls this before the first process exists */
void
dmainit()
{
	dlm_init();
	plat_dmainit();
}
#endif
