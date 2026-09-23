/*
 * sys/rusage.h - Atari System V has no struct rusage (UniSoft SVR4 left out
 * the BSD resource-usage calls). XView's SVR4 notifier includes this
 * Solaris header only to pass a struct rusage through its wait3-style
 * callbacks; nothing on ASV fills one in, so callers see zeroes.
 */
#ifndef _SYS_RUSAGE_H
#define _SYS_RUSAGE_H
#include <sys/time.h>

struct rusage {
	struct timeval ru_utime;	/* user time used */
	struct timeval ru_stime;	/* system time used */
	long	ru_maxrss;
	long	ru_ixrss;
	long	ru_idrss;
	long	ru_isrss;
	long	ru_minflt;
	long	ru_majflt;
	long	ru_nswap;
	long	ru_inblock;
	long	ru_oublock;
	long	ru_msgsnd;
	long	ru_msgrcv;
	long	ru_nsignals;
	long	ru_nvcsw;
	long	ru_nivcsw;
};

#define RUSAGE_SELF	0
#define RUSAGE_CHILDREN	-1
#endif
