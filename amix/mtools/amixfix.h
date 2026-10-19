/*
 * amixfix.h - ahead of every mtools source (build.sh): prototypes AMIX's
 * SVR4.0 headers leave out, which GCC 15 insists on.
 */
#include <sys/types.h>
struct timeval;
int usleep(unsigned);
int gettimeofday(struct timeval *, void *);
#include <sys/stat.h>
#ifndef S_ISLNK
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#endif
/* in libc, but not in unistd.h */
int seteuid(uid_t);
int setegid(gid_t);
