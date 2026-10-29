/*
 * dlmtest -- MISC test module: kernel calls, a dependency, commons
 * bound to the kernel and allocated, static data, strings, a switch,
 * and a weak reference that stays unresolved.
 */

#include "sys/types.h"
#include "sys/moddefs.h"

extern int printf(), strlen(), dlmdep_add();
extern int dlmdep_value;
extern int dlmtest_absent();
asm(".weak dlmtest_absent");

long	lbolt;				/* common: binds to the kernel's */
char	dlmtest_buf[100];		/* common: allocated */
int	dlmtest_calls;			/* common: allocated */
static int st;				/* .bss */
static char *names[] = { "zero", "one", "two", "three" };
int	(*dlmtest_weak)() = dlmtest_absent;

int
dlmtest_run(n)
	int n;
{
	int r;

	switch (n) {
	case 0:
		r = strlen(names[0]);
		break;
	case 1:
		r = dlmdep_add(n, dlmdep_value);
		break;
	case 2:
		r = (int)lbolt;
		break;
	case 3:
		bcopy(names[3], dlmtest_buf, 6);
		r = dlmtest_buf[0];
		break;
	case 4:
		r = dlmtest_weak ? (*dlmtest_weak)() : -1;
		break;
	default:
		r = st++;
		break;
	}
	dlmtest_calls++;
	return r;
}

static int
dlmtest_load()
{
	printf("dlmtest: %s %d\n", names[1], dlmtest_run(1));
	return 0;
}

static int
dlmtest_unload()
{
	return 0;
}

MOD_MISC_WRAPPER(dlmtest, dlmtest_load, dlmtest_unload, "dlm test module");
