/*
 * amixfix.h - what AMIX's SVR4.0 headers and libc leave out, for ncurses
 * (build.sh puts it ahead of each source with -include) and nano (at the
 * end of its config.h, which every source includes first; gnulib's
 * replacement headers insist on that).  Not ahead of configure's tests,
 * which declare a function as "char f();" to see whether it links.
 */
#ifndef AMIXFIX_H
#define AMIXFIX_H
#include <sys/types.h>

/* no prototypes for these, and GCC 15 refuses to call an undeclared
 * function */
struct timeval;
struct timezone;
struct fd_set;
struct rlimit;
int select(int, struct fd_set *, struct fd_set *, struct fd_set *, struct timeval *);
int gettimeofday(struct timeval *, struct timezone *);
int getrlimit(int, struct rlimit *);
int fchmod(int, mode_t);
int strncasecmp(const char *, const char *, size_t);	/* gnulib's */
int strcasecmp(const char *, const char *);

/* not in AMIX's libc at all: rt.c */
int gethostname(char *, int);
int mkstemp(char *);
const char *getprogname(void);
#define HAVE_GETPROGNAME 1

/* gnulib: no threads here (mbrtowc's lock around mbtowc) */
#define AVOID_ANY_THREADS 1
#endif
