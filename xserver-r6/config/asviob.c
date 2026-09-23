/*
 * asviob.c - the stdio streams for Atari System V programs built with the
 * cross toolchain (see mksysroot.sh, whose stdio.h defines stdin, stdout
 * and stderr through __asv_iob()).
 *
 * libc.so.1 works on its own __iob directly. Any reference to __iob from
 * the executable - even through the GOT - makes GNU ld copy the table
 * into it, and the program's streams and libc's drift apart. So the
 * program never names __iob: the dynamic linker hands us libc's.
 */
#include <stdio.h>

#define RTLD_LAZY	1
extern void *_dlopen(const char *, int);
extern void *_dlsym(void *, const char *);

FILE *
__asv_iob(void)
{
	static FILE *iob;

	if (iob == (FILE *)0)
		iob = (FILE *)_dlsym(_dlopen((char *)0, RTLD_LAZY), "__iob");
	return iob;
}
