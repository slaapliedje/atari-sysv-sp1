/*
 * rt.c - what a static AMIX vim needs that no AMIX library has, and the
 * calls into AMIX's libraries that the mint GCC would get wrong.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* the mint GCC, an a.out compiler, calls __main at the top of main() to
 * run constructors; there are none */
void __main(void) { }

/* C99's trunc, which AMIX's libm predates: the FPU's round-toward-zero */
double trunc(double x)
{
	double r;

	__asm__("fintrz.x %1,%0" : "=f" (r) : "f" (x));
	return r;
}

/*
 * The mint GCC returns a double in d0:d1, even with -m68881. AMIX's libc
 * and libm return one in fp0. amixfix.h renames the functions vim calls to
 * amix_NAME, which are these: NAME is called with a copy of the arguments
 * (16 bytes, the most any of them takes: two doubles; for the others the
 * rest is the caller's frame, only read), and fp0 goes to d0:d1.
 */
#define FP0_TO_D0D1(f)					\
	"	.globl	amix_" #f "\n"			\
	"amix_" #f ":\n"				\
	"	move.l	16(%sp),-(%sp)\n"		\
	"	move.l	16(%sp),-(%sp)\n"		\
	"	move.l	16(%sp),-(%sp)\n"		\
	"	move.l	16(%sp),-(%sp)\n"		\
	"	jsr	" #f "\n"			\
	"	lea	16(%sp),%sp\n"			\
	"	fmove.d	%fp0,-(%sp)\n"			\
	"	move.l	(%sp)+,%d0\n"			\
	"	move.l	(%sp)+,%d1\n"			\
	"	rts\n"
__asm__("	.text\n"
	FP0_TO_D0D1(strtod)
	FP0_TO_D0D1(sqrt) FP0_TO_D0D1(pow) FP0_TO_D0D1(exp)
	FP0_TO_D0D1(log) FP0_TO_D0D1(log10)
	FP0_TO_D0D1(floor) FP0_TO_D0D1(ceil) FP0_TO_D0D1(fmod)
	FP0_TO_D0D1(sin) FP0_TO_D0D1(cos) FP0_TO_D0D1(tan)
	FP0_TO_D0D1(asin) FP0_TO_D0D1(acos) FP0_TO_D0D1(atan) FP0_TO_D0D1(atan2)
	FP0_TO_D0D1(sinh) FP0_TO_D0D1(cosh) FP0_TO_D0D1(tanh));

/*
 * AMIX's printf family (1991) has no ll length. vim formats each integer
 * on its own, as sprintf(buf, "%lld", n) (or llu, llo, llx, llX), so
 * those formats are done here and every other one by AMIX's vsprintf.
 */
static int lltoa(char *buf, int spec, unsigned long long v)
{
	char t[24];
	const char *dig = spec == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
	unsigned base = spec == 'o' ? 8 : spec == 'x' || spec == 'X' ? 16 : 10;
	int n = 0, k = 0;

	if (spec == 'd' && (long long)v < 0) {
		buf[n++] = '-';
		v = -v;
	}
	do {
		t[k++] = dig[v % base];
		v /= base;
	} while (v != 0);
	while (k > 0)
		buf[n++] = t[--k];
	buf[n] = '\0';
	return n;
}

int amix_sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	if (fmt[0] == '%' && fmt[1] == 'l' && fmt[2] == 'l' && fmt[3] != '\0'
		&& strchr("duoxX", fmt[3]) && fmt[4] == '\0')
		n = lltoa(buf, fmt[3], va_arg(ap, unsigned long long));
	else
		n = vsprintf(buf, fmt, ap);
	va_end(ap);
	return n;
}

/* ---- what AMIX's libc has not (nano) ------------------------------- */
#include <sys/utsname.h>
#include <fcntl.h>
#include <errno.h>

/* gnulib's getprogname module knows no way to the name on this system */
const char *getprogname(void)
{
	return "nano";
}

/* SVR4 has it in libsocket, with libnsl behind: uname's node name does */
int gethostname(char *buf, int len)
{
	struct utsname u;

	if (uname(&u) < 0 || len <= 0)
		return -1;
	strncpy(buf, u.nodename, len);
	buf[len - 1] = 0;
	return 0;
}

char *mktemp(char *);

/* mktemp and an exclusive create, a few names tried */
int mkstemp(char *template)
{
	char saved[1024];
	int fd, i;

	if (strlen(template) >= sizeof saved) {
		errno = EINVAL;
		return -1;
	}
	strcpy(saved, template);
	for (i = 0; i < 100; i++) {
		strcpy(template, saved);
		if (mktemp(template)[0] == 0)
			break;
		if ((fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600)) >= 0)
			return fd;
		if (errno != EEXIST)
			return -1;
	}
	errno = EEXIST;
	return -1;
}
