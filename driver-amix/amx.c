/*
 * amx - run AMIX (Amiga UNIX SVR4) binaries on Atari System V.
 *
 * Both systems are SVR4.0 for the 68k with the same system call numbers
 * (95 of 95 libc stubs agree), but ASV follows the 88open BCS where AMIX
 * is plain AT&T SVR4, so this is a small personality layer, in the manner
 * of iBCS: AMIX programs run on AMIX's own libc (see amixify.py) and this
 * module translates at the kernel boundary.
 *
 * Entry. AMIX calls the kernel with
 *	moveq #N,d0; trap #0	(args on the user stack above the stub's
 *				 return address)
 * ASV with
 *	moveq #N,d0; args in a0,d1,a1,d2,a2,d3,a3,d4,a4,d5; trap #10
 * and answers trap #0 with SIGSYS. Every ELF process's traps go through
 * u.u_procp->p_syscallops (s_trapfunc[N] for trap #N, and s_sendsig), set
 * to svr4_syscallops by elfexec. This module fills slot 0 of that table with
 * a gate; the first trap #0 moves the process to amx_ops (the same table
 * with the gate and a signal-delivery wrapper), fork copies it and exec of
 * a native program resets it. The gate copies the arguments from the stack
 * into the register slots svr4_trap10 reads and runs svr4_trap10, so the
 * system call machinery (restart, signals, /proc, errors) is ASV's own.
 * Registers borrowed beyond the first three (d2-d5, a2-a4: callee-saved in
 * the AMIX ABI) are put back; a0, d1, a1 are scratch (and exec's new image
 * must keep them).
 *
 * Translated (AMIX <-> ASV):
 *	signal numbers	21..22, 28..31 (ASV has URG, IO, XCPU, XFSZ, VTALRM,
 *			PROF at 33..38); kill, sigsend, signal/sigset, the
 *			wait status, siginfo, delivery
 *	signal masks	AMIX: 4 words, 1 << (n-1); ASV: 2 words, MSB first
 *	sigaction	field order, SA_* bits; sigprocmask's `how' (1..3/0..2)
 *	siginfo		si_code and si_errno are swapped
 *	ucontext	16- vs 8-byte uc_sigmask shifts uc_mcontext by 8
 *	errno		the networking range (ASV 128..162, 235..242)
 *	stat v2		ASV writes 512 bytes, AMIX has SVR4's 136
 *	termios		ASV has a pad byte before c_cc (BCS)
 *	utsname		fields of 256 vs 257 bytes
 *	pathconf	_PC_NO_TRUNC, _PC_VDISABLE, _PC_CHOWN_RESTRICTED
 * Structures that come back from the kernel are written to scratch space
 * below the user's stack pointer and converted into the caller's buffer.
 * Not translated (yet): the System V IPC *_ds structures and I_RECVFD.
 * (sigaltstack's stack_t is the same on both.)
 *
 * ASV's own libc never uses trap #0 for anything that works (its one stub,
 * _sys3b, got SIGSYS), so native programs see no change.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/immu.h>
#include <sys/fs/s5dir.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/kmem.h>

extern struct syscallops svr4_syscallops;
extern int svr4_trap10(), sendsig(), grow();

#define D0		0		/* u_ar0 register slots: d0-d7, a0-a7 */
#define D1		1
#define USP		15
#define SR_LO		73		/* byte offsets in the frame: SR's low */
#define PC_OFF		74		/* byte (carry), and the PC */

/* svr4_trap10's argument registers, in order: a0 d1 a1 d2 a2 d3 a3 d4 a4 d5
 * (the kernel's own table, paramstoregs[], is static) */
static int paramstoregs[] = { 8, 1, 9, 2, 10, 3, 11, 4, 12, 5 };
#define NREGARGS	(sizeof paramstoregs / sizeof paramstoregs[0])

#define SYS_wait	7
#define SYS_kill	37
#define SYS_signal	48
#define SYS_ioctl	54
#define SYS_sigprocmask	95
#define SYS_sigsuspend	96
#define SYS_sigaction	98
#define SYS_sigpending	99
#define SYS_context	100
#define SYS_waitsys	107
#define SYS_sigsendsys	108
#define SYS_pathconf	113
#define SYS_fpathconf	118
#define SYS_xstat	123
#define SYS_lxstat	124
#define SYS_fxstat	125
#define SYS_nuname	135

static struct syscallops amx_ops;
int amx_calls;				/* for the curious (adb/crash) */

/* ---- signals ---- */

#define AMX_NSIG	32
#define ASV_NSIG	65

/* AMIX -> ASV */
static char sig_a2k[AMX_NSIG] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	20, 33, 22, 23, 24, 25, 26, 27, 37, 38, 35, 36
};

static int
sig_in(s)
	register int s;
{
	return (s > 0 && s < AMX_NSIG) ? sig_a2k[s] : s;
}

static int
sig_out(s)
	register int s;
{
	register int i;

	if (s < 21 || (s >= 22 && s <= 27))
		return s;
	if (s == 34)			/* SIGIO: AMIX's SIGIO is SIGPOLL */
		return 22;
	for (i = 21; i < AMX_NSIG; i++)
		if (sig_a2k[i] == s)
			return i;
	return s;			/* SIGLOST etc.: no AMIX name */
}

#define KBIT(n)		((unsigned long)0x80000000 >> (((n) - 1) & 31))
#define KWORD(n)	(((n) - 1) >> 5)

/* AMIX sigset_t (4 words) -> ASV (2 words) */
static void
mask_in(a, k)
	register unsigned long *a, *k;
{
	register int n, s;

	k[0] = k[1] = 0;
	for (n = 1; n < AMX_NSIG; n++)
		if (a[0] & ((unsigned long)1 << (n - 1))) {
			s = sig_in(n);
			k[KWORD(s)] |= KBIT(s);
		}
}

static void
mask_out(k, a)
	register unsigned long *k, *a;
{
	register int s, n;

	a[0] = a[1] = a[2] = a[3] = 0;
	for (s = 1; s < ASV_NSIG; s++)
		if (k[KWORD(s)] & KBIT(s)) {
			n = sig_out(s);
			if (n > 0 && n <= 32)
				a[0] |= (unsigned long)1 << (n - 1);
		}
}

/* sa_flags */
static long saf[][2] = {		/* AMIX, ASV */
	{ 0x00000001, 0x00010000 },	/* SA_ONSTACK */
	{ 0x00000002, 0x00020000 },	/* SA_RESETHAND */
	{ 0x00000004, 0x00040000 },	/* SA_RESTART */
	{ 0x00000008, 0x00080000 },	/* SA_SIGINFO */
	{ 0x00000010, 0x00100000 },	/* SA_NODEFER */
	{ 0x00010000, 0x00000002 },	/* SA_NOCLDWAIT */
	{ 0x00020000, 0x00000001 },	/* SA_NOCLDSTOP */
};
#define NSAF	(sizeof saf / sizeof saf[0])

static long
saflags(f, from, to)
	register long f;
	int from, to;
{
	register int i;
	register long r = 0;

	for (i = 0; i < NSAF; i++)
		if (f & saf[i][from])
			r |= saf[i][to];
	return r;
}

/* struct sigaction: AMIX { flags; handler; mask[4]; resv[2] } (32 bytes),
 * ASV { handler; mask[2]; flags } (16) */
static void
sa_in(a, k)
	register long *a, *k;
{
	k[0] = a[1];
	mask_in((unsigned long *)&a[2], (unsigned long *)&k[1]);
	k[3] = saflags(a[0], 0, 1);
}

static void
sa_out(k, a)
	register long *k, *a;
{
	a[0] = saflags(k[3], 1, 0);
	a[1] = k[0];
	mask_out((unsigned long *)&k[1], (unsigned long *)&a[2]);
	a[6] = a[7] = 0;
}

/* siginfo (128 bytes): si_signo, si_code, si_errno on AMIX; signo, errno,
 * code on ASV. For SIGCHLD other than CLD_EXITED, si_status (word 5) is a
 * signal number. */
static void
si_out(k, a)
	register long *k, *a;
{
	register int i;

	for (i = 3; i < 32; i++)
		a[i] = k[i];
	a[0] = sig_out((int)k[0]);
	a[1] = k[2];
	a[2] = k[1];
	if (k[0] == SIGCLD && k[2] >= 2 && k[2] <= 6)	/* CLD_KILLED.. */
		a[5] = sig_out((int)k[5]);
}

/* a wait() status: a signal in the low 7 bits (killed), or (sig << 8)|0177
 * (stopped) */
static int
wstat_out(st)
	register int st;
{
	if ((st & 0377) == 0177)
		return (st & ~0xff00) | (sig_out((st >> 8) & 0xff) << 8);
	if (st & 0177)
		return (st & ~0177) | sig_out(st & 0177);
	return st;
}

/* ---- errno ---- */

static short errmap[][2] = {		/* ASV, AMIX */
	{ 128, 150 }, { 129, 149 }, { 130, 95 }, { 131, 96 }, { 132, 97 },
	{ 133, 98 }, { 134, 99 }, { 135, 120 }, { 136, 121 }, { 137, 122 },
	{ 138, 123 }, { 139, 124 }, { 140, 125 }, { 141, 126 }, { 142, 127 },
	{ 143, 128 }, { 144, 129 }, { 145, 130 }, { 146, 131 }, { 147, 132 },
	{ 148, 133 }, { 149, 134 }, { 150, 143 }, { 151, 144 }, { 152, 145 },
	{ 153, 146 }, { 156, 147 }, { 157, 148 }, { 158, 93 }, { 160, 94 },
	{ 162, 151 }, { 235, 135 }, { 237, 137 }, { 238, 138 }, { 239, 139 },
	{ 240, 140 }, { 241, 141 }, { 242, 142 },
};
#define NERRMAP	(sizeof errmap / sizeof errmap[0])

static int
err_out(e)
	register int e;
{
	register int i;

	if (e < 128)
		return e;
	for (i = 0; i < NERRMAP; i++)
		if (errmap[i][0] == e)
			return errmap[i][1];
	return e;
}

/* ---- other structures ---- */

#define STAT_VER	2
#define ASV_STATSZ	512		/* ASV's struct stat, version 2 */
#define AMX_STATSZ	136		/* SVR4's */

/* ASV stat v2 -> SVR4: dev ino mode nlink uid gid rdev size are packed at
 * the front on ASV; st_atim .. st_fstype (bytes 56..103) match */
static void
st_out(k, a)
	register long *k, *a;
{
	register int i;

	for (i = 0; i < AMX_STATSZ / 4; i++)
		a[i] = 0;
	a[0] = k[0];			/* st_dev */
	a[4] = k[1];			/* st_ino (after st_pad1[3]) */
	a[5] = k[2];			/* st_mode */
	a[6] = k[3];			/* st_nlink */
	a[7] = k[4];			/* st_uid */
	a[8] = k[5];			/* st_gid */
	a[9] = k[6];			/* st_rdev */
	a[12] = k[7];			/* st_size (after st_pad2[2]) */
	for (i = 14; i < 26; i++)
		a[i] = k[i];		/* times, st_blksize, st_blocks, st_fstype */
}

#define TCGETS		(('T'<<8)|13)
#define TCSETS		(('T'<<8)|14)
#define TCSETSF		(('T'<<8)|16)
#define TIOS_FLAGS	16		/* c_iflag c_oflag c_cflag c_lflag */
#define TIOS_NCCS	19
#define ASV_TIOSZ	(TIOS_FLAGS + 1 + TIOS_NCCS)	/* c_pad1 */
#define AMX_TIOSZ	(TIOS_FLAGS + TIOS_NCCS)

#define UTS_FIELDS	5
#define ASV_NMLN	256
#define AMX_NMLN	257

/* ucontext: flags, link, sigmask (AMIX 16 bytes, ASV 8), stack (12),
 * mcontext (gregs 72 + fpregs 108 + state); both 1024 bytes */
#define UC_SIZE		1024
#define UC_FAULT	010		/* ASV: mc_state valid; AMIX: UC_FPU */
#define AMX_UC_MC	36
#define ASV_UC_MC	28
#define UC_MC_COPY	(UC_SIZE - AMX_UC_MC)

static void
uc_in(a, k)			/* AMIX -> ASV */
	register char *a, *k;
{
	bzero(k, UC_SIZE);
	((long *)k)[0] = ((long *)a)[0] & ~UC_FAULT;
	((long *)k)[1] = ((long *)a)[1];
	mask_in((unsigned long *)(a + 8), (unsigned long *)(k + 8));
	bcopy(a + 24, k + 16, 12);
	bcopy(a + AMX_UC_MC, k + ASV_UC_MC, UC_MC_COPY);
}

static void
uc_out(k, a)			/* ASV -> AMIX */
	register char *k, *a;
{
	bzero(a, UC_SIZE);
	((long *)a)[0] = ((long *)k)[0] & ~UC_FAULT;
	((long *)a)[1] = ((long *)k)[1];
	mask_out((unsigned long *)(k + 8), (unsigned long *)(a + 8));
	bcopy(k + 16, a + 24, 12);
	bcopy(k + ASV_UC_MC, a + AMX_UC_MC, UC_MC_COPY);
}

/* _PC_: AMIX NO_TRUNC 7, VDISABLE 8, CHOWN_RESTRICTED 9; ASV 8, 9, 7 */
static int
pc_in(n)
	int n;
{
	switch (n) {
	case 7: return 8;
	case 8: return 9;
	case 9: return 7;
	}
	return n;
}

/* ---- the gate ---- */

/* what to do after the call */
#define POST_NONE	0
#define POST_STAT	1
#define POST_SIGMASK	2		/* sigprocmask oset, sigpending */
#define POST_SIGACTION	3
#define POST_SIGINFO	4
#define POST_TCGETS	5
#define POST_UNAME	6
#define POST_CONTEXT	7
#define POST_WAIT	8

/* the largest structure that passes through scratch (a ucontext; the
 * utsname is 1285 bytes), and the kernel-side work buffers for both forms
 * of it: allocated per call, since copyin/copyout can sleep */
#define SCRATCH		(2 * UC_SIZE)
#define BUFSZ		(UTS_FIELDS * AMX_NMLN + 3)

static int
amx_trap0(fp)
	int *fp;			/* the trap's stack frame: d0-d7, a0-a7 */
{
	register int n, i;
	int num, rv, post, err, args[NREGARGS], save[NREGARGS];
	caddr_t sp, tmp, ubuf, ksrc;
	char *kbuf = 0, *abuf;
	long pc;

	amx_calls++;
	if (u.u_procp->p_syscallops == &svr4_syscallops)
		u.u_procp->p_syscallops = &amx_ops;

	num = fp[D0];
	n = 0;
	if ((unsigned)num < sysentsize)
		n = sysent[num].sy_narg;
	if (n > NREGARGS)
		n = NREGARGS;
	/* args above the return address; a bad stack reads as zeros and the
	 * call then fails on its own with EFAULT/EINVAL */
	if (n > 0 && copyin((caddr_t)(fp[USP] + 4), (caddr_t)args, n * sizeof(int)))
		for (i = 0; i < n; i++)
			args[i] = 0;

	/* scratch: below the stack pointer, clear of anything in use */
	sp = (caddr_t)((fp[USP] - 64) & ~3);
	tmp = sp - SCRATCH;
	ubuf = 0;
	post = POST_NONE;
	err = 0;

	switch (num) {
	case SYS_sigprocmask: case SYS_sigsuspend: case SYS_sigpending:
	case SYS_sigaction: case SYS_waitsys: case SYS_context:
	case SYS_xstat: case SYS_lxstat: case SYS_fxstat:
	case SYS_ioctl: case SYS_nuname:
		kbuf = (char *)kmem_alloc(2 * BUFSZ, KM_SLEEP);
		abuf = kbuf + BUFSZ;
	}
	ksrc = tmp;
	switch (num) {
	case SYS_kill:
		args[1] = sig_in(args[1]);
		break;
	case SYS_sigsendsys:
		args[1] = sig_in(args[1]);
		break;
	case SYS_signal:		/* sig | SIGHOLD etc. */
		args[0] = (args[0] & ~0xff) | sig_in(args[0] & 0xff);
		break;
	case SYS_sigprocmask:
		if (args[0] >= 1 && args[0] <= 3)
			args[0]--;	/* SIG_BLOCK 1 -> 0 ... */
		if (args[1]) {
			if (copyin((caddr_t)args[1], abuf, 16)) {
				err = EFAULT;
				break;
			}
			mask_in((unsigned long *)abuf, (unsigned long *)kbuf);
			if (copyout(kbuf, tmp, 8)) {
				err = EFAULT;
				break;
			}
			args[1] = (int)tmp;
		}
		if (args[2]) {
			ubuf = (caddr_t)args[2];
			ksrc = tmp + 8;
			args[2] = (int)ksrc;
			post = POST_SIGMASK;
		}
		break;
	case SYS_sigsuspend:
		if (copyin((caddr_t)args[0], abuf, 16)) {
			err = EFAULT;
			break;
		}
		mask_in((unsigned long *)abuf, (unsigned long *)kbuf);
		if (copyout(kbuf, tmp, 8)) {
			err = EFAULT;
			break;
		}
		args[0] = (int)tmp;
		break;
	case SYS_sigpending:		/* (cmd, set) */
		if (args[1]) {
			ubuf = (caddr_t)args[1];
			args[1] = (int)tmp;
			post = POST_SIGMASK;
		}
		break;
	case SYS_sigaction:
		args[0] = sig_in(args[0]);
		if (args[1]) {
			if (copyin((caddr_t)args[1], abuf, 32)) {
				err = EFAULT;
				break;
			}
			sa_in((long *)abuf, (long *)kbuf);
			if (copyout(kbuf, tmp, 16)) {
				err = EFAULT;
				break;
			}
			args[1] = (int)tmp;
		}
		if (args[2]) {
			ubuf = (caddr_t)args[2];
			ksrc = tmp + 16;
			args[2] = (int)ksrc;
			post = POST_SIGACTION;
		}
		break;
	case SYS_waitsys:		/* (idtype, id, infop, options) */
		if (args[2]) {
			ubuf = (caddr_t)args[2];
			args[2] = (int)tmp;
			post = POST_SIGINFO;
		}
		break;
	case SYS_wait:
		post = POST_WAIT;
		break;
	case SYS_context:		/* (GETCONTEXT 0 | SETCONTEXT 1, ucp) */
		if (args[0] == 0) {
			ubuf = (caddr_t)args[1];
			args[1] = (int)tmp;
			post = POST_CONTEXT;
		} else if (args[0] == 1) {
			if (copyin((caddr_t)args[1], abuf, UC_SIZE)) {
				err = EFAULT;
				break;
			}
			uc_in(abuf, kbuf);
			if (copyout(kbuf, tmp, UC_SIZE)) {
				err = EFAULT;
				break;
			}
			args[1] = (int)tmp;
		}
		break;
	case SYS_xstat:
	case SYS_lxstat:
	case SYS_fxstat:
		if (args[0] == STAT_VER) {
			ubuf = (caddr_t)args[2];
			args[2] = (int)tmp;
			post = POST_STAT;
		}
		break;
	case SYS_ioctl:
		if (args[1] == TCGETS) {
			ubuf = (caddr_t)args[2];
			args[2] = (int)tmp;
			post = POST_TCGETS;
		} else if (args[1] > TCGETS && args[1] <= TCSETSF) {
			if (copyin((caddr_t)args[2], abuf, AMX_TIOSZ)) {
				err = EFAULT;
				break;
			}
			bcopy(abuf, kbuf, TIOS_FLAGS);
			kbuf[TIOS_FLAGS] = 0;
			bcopy(abuf + TIOS_FLAGS, kbuf + TIOS_FLAGS + 1, TIOS_NCCS);
			if (copyout(kbuf, tmp, ASV_TIOSZ)) {
				err = EFAULT;
				break;
			}
			args[2] = (int)tmp;
		}
		break;
	case SYS_nuname:
		ubuf = (caddr_t)args[0];
		args[0] = (int)tmp;
		post = POST_UNAME;
		break;
	case SYS_pathconf:
	case SYS_fpathconf:
		args[1] = pc_in(args[1]);
		break;
	}
	if (err) {
		if (kbuf)
			kmem_free(kbuf, 2 * BUFSZ);
		fp[D0] = err_out(err);
		((char *)fp)[SR_LO] |= 1;
		return 0;
	}
	/* extends the stack if the scratch is below it; returns 0 when it is
	 * already inside, so the result means nothing here */
	(void) grow((int *)tmp);

	for (i = 0; i < n; i++) {
		save[i] = fp[paramstoregs[i]];
		fp[paramstoregs[i]] = args[i];
	}
	pc = *(long *)((char *)fp + PC_OFF);
	rv = svr4_trap10(fp);
	/* a restarted call (PC backed up), setcontext and exec leave through
	 * a new PC: nothing to convert, and the registers are not ours */
	if (*(long *)((char *)fp + PC_OFF) != pc)
		goto done;
	for (i = 3; i < n; i++)
		fp[paramstoregs[i]] = save[i];

	if (((char *)fp)[SR_LO] & 1) {		/* carry: failed */
		fp[D0] = err_out(fp[D0]);
		goto done;
	}
	switch (post) {
	case POST_NONE:
		goto done;
	case POST_WAIT:
		fp[D1] = wstat_out(fp[D1]);
		goto done;
	case POST_STAT:
		if (copyin(tmp, kbuf, 104))
			break;
		st_out((long *)kbuf, (long *)abuf);
		if (copyout(abuf, ubuf, AMX_STATSZ))
			break;
		goto done;
	case POST_SIGMASK:
		if (copyin(ksrc, kbuf, 8))
			break;
		mask_out((unsigned long *)kbuf, (unsigned long *)abuf);
		if (copyout(abuf, ubuf, 16))
			break;
		goto done;
	case POST_SIGACTION:
		if (copyin(ksrc, kbuf, 16))
			break;
		sa_out((long *)kbuf, (long *)abuf);
		if (copyout(abuf, ubuf, 32))
			break;
		goto done;
	case POST_SIGINFO:
		if (copyin(tmp, kbuf, 128))
			break;
		si_out((long *)kbuf, (long *)abuf);
		if (copyout(abuf, ubuf, 128))
			break;
		goto done;
	case POST_TCGETS:
		if (copyin(tmp, kbuf, ASV_TIOSZ))
			break;
		bcopy(kbuf, abuf, TIOS_FLAGS);
		bcopy(kbuf + TIOS_FLAGS + 1, abuf + TIOS_FLAGS, TIOS_NCCS);
		if (copyout(abuf, ubuf, AMX_TIOSZ))
			break;
		goto done;
	case POST_UNAME:
		if (copyin(tmp, kbuf, UTS_FIELDS * ASV_NMLN))
			break;
		bzero(abuf, UTS_FIELDS * AMX_NMLN);
		for (i = 0; i < UTS_FIELDS; i++)
			bcopy(kbuf + i * ASV_NMLN, abuf + i * AMX_NMLN, ASV_NMLN);
		if (copyout(abuf, ubuf, UTS_FIELDS * AMX_NMLN))
			break;
		goto done;
	case POST_CONTEXT:
		if (copyin(tmp, kbuf, UC_SIZE))
			break;
		uc_out(kbuf, abuf);
		if (copyout(abuf, ubuf, UC_SIZE))
			break;
		goto done;
	}
	fp[D0] = err_out(EFAULT);
	((char *)fp)[SR_LO] |= 1;
done:
	if (kbuf)
		kmem_free(kbuf, 2 * BUFSZ);
	return rv;
}

/* Signal delivery: ASV's sendsig builds the handler's frame from the ASV
 * signal number (it needs it for its own bookkeeping), then the arguments
 * the AMIX handler sees are fixed up: at the new user stack pointer are the
 * return address (the copied sigtramp), sig, siginfo * and ucontext *. The
 * ucontext stays in ASV's layout: sigtramp hands it back to the kernel. */
static int
amx_sendsig(sig, sip, hdlr)
	int sig;
	caddr_t sip;
	int (*hdlr)();
{
	int rv, usp, info;
	long k[32], a[32];

	if ((rv = sendsig(sig, sip, hdlr)) == 0)
		return rv;
	usp = u.u_ar0->regs[USP];
	(void) suword((int *)(usp + 4), sig_out(sig));
	info = fuword((int *)(usp + 8));
	if (info != 0 && info != -1 && copyin((caddr_t)info, (caddr_t)k, 128) == 0) {
		si_out(k, a);
		(void) copyout((caddr_t)a, (caddr_t)info, 128);
	}
	return rv;
}

amxinit()
{
	amx_ops = svr4_syscallops;
	amx_ops.s_trapfunc[0] = amx_trap0;
	amx_ops.s_sendsig = amx_sendsig;
	svr4_syscallops.s_trapfunc[0] = amx_trap0;
}
