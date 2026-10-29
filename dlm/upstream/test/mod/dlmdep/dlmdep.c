/*
 * dlmdep -- MISC test module that dlmtest depends on.
 */

#include "sys/types.h"
#include "sys/moddefs.h"

extern int printf();

int	dlmdep_counter;			/* common, allocated here */
int	dlmdep_value = 42;
static char msg[] = "dlmdep";

int
dlmdep_add(a, b)
	int a, b;
{
	dlmdep_counter++;
	return a + b + dlmdep_value;
}

static int
dlmdep_load()
{
	printf("%s: loaded\n", msg);
	return 0;
}

static int
dlmdep_unload()
{
	printf("%s: unloaded\n", msg);
	return 0;
}

MOD_MISC_WRAPPER(dlmdep, dlmdep_load, dlmdep_unload, "dlm dependency test");
