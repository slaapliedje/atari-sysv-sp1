/*
 * asvjwm.h - put ahead of every JWM source file (-include): what Atari
 * System V's headers declare only for its own cc in extended mode
 * (__STDC__ == 0), which GCC is not.
 */
#ifndef ASVJWM_H
#define ASVJWM_H
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <signal.h>

extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
extern int gettimeofday(struct timeval *, void *);
extern int putenv(char *);
extern void tzset(void);
extern int kill(pid_t, int);
extern int sigaction(int, const struct sigaction *, struct sigaction *);

#endif
