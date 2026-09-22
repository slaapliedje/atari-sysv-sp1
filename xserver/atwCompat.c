/*
 * atwCompat.c - the BSD names the X11R4 server and ASV's libsocket use,
 * on top of what Atari System V's libc really exports.
 *
 * ASV's libc.so.1 carries the non-ABI functions under "_abi_" names
 * (_abi_select, _abi_gettimeofday, ...) and nothing on the system defines
 * the plain ones; the stock XatariServer had them linked in from a
 * UniSoft compatibility file that did not ship. This is that file.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <string.h>

extern int _abi_select(), _abi_gettimeofday(), _abi_sysinfo();
extern int _abi_syslog(), _abi_vsyslog(), _abi_setgrent(), _abi_endgrent();
extern struct group *_abi_getgrent();

void
bcopy(from, to, n)
    char *from, *to;
    int n;
{
    memmove(to, from, n);
}

void
bzero(p, n)
    char *p;
    int n;
{
    memset(p, 0, n);
}

int
bcmp(a, b, n)
    char *a, *b;
    int n;
{
    return memcmp(a, b, n);
}

long
random()
{
    return rand();
}

void
srandom(seed)
    int seed;
{
    srand(seed);
}

int
getdtablesize()
{
    return 64;
}

int
gettimeofday(tv, tz)
    struct timeval *tv;
    struct timezone *tz;
{
    return _abi_gettimeofday(tv, tz);
}

/*
 * SVR4's fd_set is 1024 bits and _abi_select writes the whole thing back;
 * the R4 server passes masks sized for MAXSOCKS bits. Marshal through
 * full-size buffers or the server's stack gets overwritten.
 */
#define SVR4_FDSET_LONGS 32

int
select(nfds, r, w, e, tv)
    int nfds;
    long *r, *w, *e;
    struct timeval *tv;
{
    long rr[SVR4_FDSET_LONGS], ww[SVR4_FDSET_LONGS], ee[SVR4_FDSET_LONGS];
    int n = (nfds + 31) / 32, res;

    if (n > SVR4_FDSET_LONGS)
	n = SVR4_FDSET_LONGS;
    memset(rr, 0, sizeof rr); memset(ww, 0, sizeof ww); memset(ee, 0, sizeof ee);
    if (r) memcpy(rr, r, n * sizeof(long));
    if (w) memcpy(ww, w, n * sizeof(long));
    if (e) memcpy(ee, e, n * sizeof(long));
    res = _abi_select(nfds, r ? rr : (long *)0, w ? ww : (long *)0,
		      e ? ee : (long *)0, tv);
    if (r) memcpy(r, rr, n * sizeof(long));
    if (w) memcpy(w, ww, n * sizeof(long));
    if (e) memcpy(e, ee, n * sizeof(long));
    return res;
}

int
sysinfo(cmd, buf, n)
    int cmd;
    char *buf;
    long n;
{
    return _abi_sysinfo(cmd, buf, n);
}

int
seteuid(uid)
    int uid;
{
    return setuid(uid);
}

struct group *
getgrent()
{
    return _abi_getgrent();
}

int
setgrent()
{
    return _abi_setgrent();
}

int
endgrent()
{
    return _abi_endgrent();
}

/* varargs in K&R: pass the first few through, X never logs more */
int
syslog(pri, fmt, a1, a2, a3, a4, a5, a6)
    int pri;
    char *fmt;
    long a1, a2, a3, a4, a5, a6;
{
    return _abi_syslog(pri, fmt, a1, a2, a3, a4, a5, a6);
}
