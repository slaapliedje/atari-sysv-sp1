/*
 * amixfix.h - included ahead of every vim source (build.sh): what AMIX's
 * SVR4.0 headers leave out, and the library calls that go through rt.c.
 */
#include <sys/types.h>
typedef int ssize_t;

/* no prototypes for these, and GCC 15 refuses to call an undeclared
 * function */
struct rlimit;
struct timeval;
struct fd_set;
int getrlimit(int, struct rlimit *);
int setrlimit(int, const struct rlimit *);
int select(int, struct fd_set *, struct fd_set *, struct fd_set *, struct timeval *);
int fchmod(int, mode_t);

/* they return a double in fp0, the mint GCC reads d0:d1 (rt.c) */
#define strtod	amix_strtod
#define sqrt	amix_sqrt
#define pow	amix_pow
#define exp	amix_exp
#define log	amix_log
#define log10	amix_log10
#define floor	amix_floor
#define ceil	amix_ceil
#define fmod	amix_fmod
#define sin	amix_sin
#define cos	amix_cos
#define tan	amix_tan
#define asin	amix_asin
#define acos	amix_acos
#define atan	amix_atan
#define atan2	amix_atan2
#define sinh	amix_sinh
#define cosh	amix_cosh
#define tanh	amix_tanh

/* no ll length in AMIX's printf (rt.c) */
#define sprintf	amix_sprintf
