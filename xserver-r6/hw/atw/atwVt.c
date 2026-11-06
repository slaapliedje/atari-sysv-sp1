/*
 * atwVt.c - more than one X server on the one ATW800/2, switched with
 * Ctrl+Alt+F1..F9 the way XFree86 switches virtual terminals.
 *
 * Atari System V has no virtual terminals for the card, so the servers
 * arrange it among themselves. Each has its own screen in video memory
 * (-fboffset) and keeps drawing into it whether it is shown or not:
 * showing a screen is only the display start, the mode and the LUT, so a
 * switch copies nothing and redraws nothing. The server on show - the
 * ACTIVE one - also owns what cannot be shared: /dev/ikbd, the LUT and
 * the 2D engine. The others draw with the CPU and never touch a register.
 *
 * /tmp/.atwvt holds the number of the server that should be active, and
 * /tmp/.atwvtN the pid of server N. To switch to N: write N, then SIGUSR2
 * the active server; it lets go (atwDeactivate) and SIGUSR2s N, which
 * finds its own number in the file and takes over (atwActivate). A server
 * starting up asks for the card the same way; one exiting hands it back
 * to the server it took it from.
 *
 * The signal only sets a flag. The work is done from the block handler,
 * between requests, which also wakes every 200 ms in case a signal lands
 * between the check and the select.
 *
 * Ctrl+Alt+F10 lends the keyboard and mouse to the console on the TT's own
 * screen: the server on show closes /dev/ikbd and the console gets the
 * keys; the card goes on showing X. They come back when the console user
 * logs out, when nobody has logged in there after a minute (so a stray
 * Ctrl+Alt+F10 leaves no one stuck at a login prompt), when the marker
 * /tmp/.atwkbd is newer than the loan (atwkbd, by any user), or with a
 * switch to this server (SIGUSR2). The marker is only ever looked at,
 * never opened or removed: whatever someone left at that path in /tmp
 * can do no harm.
 */
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <utmp.h>

#include "atw.h"

#define ATW_VT_FILE	"/tmp/.atwvt"
#define ATW_VT_MAX	9
#define ATW_KBD_FILE	"/tmp/.atwkbd"	/* atwkbd: the keyboard back to X */
#define ATW_LEND_WAIT	60		/* s: the console gets it if someone logs in */

static volatile int atwVtSig;	/* a SIGUSR2 came */
static int	atwVtWant;	/* Ctrl+Alt+Fn pressed: switch to n */
static int	atwVtPrev;	/* the server we took the card from */
static int	atwVtLend;	/* Ctrl+Alt+F10 pressed: lend the console the input */
static time_t	atwVtLent;	/* when it was lent, 0 = it is ours */
static int	atwVtLogin;	/* someone was logged in on the console since */
static int	atwVtTick;

static char *
atwVtPidFile(n)
    int n;
{
    static char path[32];

    sprintf(path, "%s%d", ATW_VT_FILE, n);
    return path;
}

static int
atwVtRead(path)
    char *path;
{
    FILE *f = fopen(path, "r");
    int n = -1;

    if (f) {
	if (fscanf(f, "%d", &n) != 1)
	    n = -1;
	fclose(f);
    }
    return n;
}

/* whole or not at all: written aside, then renamed over */
static void
atwVtWrite(path, n)
    char *path;
    int n;
{
    char tmp[40];
    FILE *f;

    sprintf(tmp, "%s.%d", path, (int)getpid());
    if ((f = fopen(tmp, "w")) == NULL)
	return;
    fprintf(f, "%d\n", n);
    fclose(f);
    chmod(tmp, 0644);
    if (rename(tmp, path) < 0)
	unlink(tmp);
}

/* server n's pid if it is running, else 0 */
static int
atwVtAlive(n)
    int n;
{
    int pid;

    if (n < 1 || n > ATW_VT_MAX)
	return 0;
    pid = atwVtRead(atwVtPidFile(n));
    return pid > 0 && kill(pid, 0) == 0 ? pid : 0;
}

static void
atwVtSignal(sig)
    int sig;
{
    atwVtSig = 1;
    signal(SIGUSR2, atwVtSignal);	/* SVR4 signal() resets it */
}

/*
 * From InitOutput, before the card is touched. Without -vt the server is
 * alone and always active. With it: if another server is on show, ask it
 * for the card and start in the background (atwScreen.active = 0, so the
 * screen setup leaves the hardware alone); otherwise take the card.
 */
void
atwVtInit()
{
    static int done;
    int cur, pid;

    if (done)
	return;			/* a server reset: keep the state */
    done = 1;
    if (atwScreen.vt == 0) {
	atwScreen.active = 1;
	return;
    }
    signal(SIGUSR2, atwVtSignal);
    if (atwVtAlive(atwScreen.vt))
	FatalError("atw: -vt %d is taken by pid %d\n", atwScreen.vt,
		   atwVtAlive(atwScreen.vt));
    atwVtWrite(atwVtPidFile(atwScreen.vt), (int)getpid());
    cur = atwVtRead(ATW_VT_FILE);
    atwVtWrite(ATW_VT_FILE, atwScreen.vt);
    if (cur != atwScreen.vt && (pid = atwVtAlive(cur)) != 0) {
	atwVtPrev = cur;
	atwScreen.active = 0;
	kill(pid, SIGUSR2);
    } else
	atwScreen.active = 1;
}

/* Ctrl+Alt+Fn (atwIo.c): done at the next poll, outside the input read */
void
atwVtSwitch(n)
    int n;
{
    if (atwScreen.vt != 0 && atwScreen.active && n != atwScreen.vt &&
	atwVtAlive(n))
	atwVtWant = n;
}

/* Ctrl+Alt+F10 (atwIo.c): done at the next poll */
void
atwVtConsole()
{
    if (atwScreen.vt != 0 && atwScreen.active && !atwVtLent)
	atwVtLend = 1;
}

/* someone logged in on the console? utmp names its line by whichever of
 * the console's device names the login used (term/con on this system):
 * compare devices, not names */
static int
atwVtConsoleUser()
{
    struct utmp *u;
    struct stat con, st;
    char path[5 + sizeof(u->ut_line) + 1];
    int found = 0;

    if (stat("/dev/console", &con) < 0)
	return 0;
    setutent();
    while ((u = getutent()) != NULL) {
	if (u->ut_type != USER_PROCESS)
	    continue;
	strcpy(path, "/dev/");
	strncat(path, u->ut_line, sizeof(u->ut_line));
	if (stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFCHR &&
	    st.st_rdev == con.st_rdev)
	    found = 1;
    }
    endutent();
    return found;
}

static void
atwVtConsoleSay(msg)
    char *msg;
{
    int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NDELAY);

    if (fd >= 0) {
	write(fd, msg, strlen(msg));
	close(fd);
    }
}

static void
atwVtLendInput()
{
    atwKeysReleaseAll();
    atwInputOff();
    atwVtLent = time((time_t *)0);
    atwVtLogin = atwVtConsoleUser();
    ErrorF("atw: Ctrl+Alt+F10: the keyboard lent to the console%s\n",
	   atwVtLogin ? " (someone is logged in there)" : "");
    atwVtConsoleSay("\r\nThe keyboard is here now (Ctrl+Alt+F10 on the ATW800/2's X screen).\r\n"
		    "Logging out, or atwkbd, gives it back to X; so does a minute without a login.\r\n");
}

/* the console is done with it (or a switch to us came): ours again */
static void
atwVtTakeInput(why)
    char *why;
{
    ErrorF("atw: the keyboard back from the console: %s\n", why);
    atwVtLent = 0;
    if (atwScreen.active)
	atwInputOn();
}

/* once a second while lent: a reason to take it back? */
static void
atwVtCheckLent()
{
    struct stat st;
    time_t now = time((time_t *)0);

    if (stat(ATW_KBD_FILE, &st) == 0 && st.st_mtime >= atwVtLent) {
	atwVtTakeInput("atwkbd");
	return;
    }
    if (atwVtConsoleUser())
	atwVtLogin = 1;
    else if (atwVtLogin)
	atwVtTakeInput("logged out");
    else if (now - atwVtLent >= ATW_LEND_WAIT)
	atwVtTakeInput("no login within a minute");
}

/* Hand the card to server n (we are active). */
static void
atwVtGive(n)
    int n;
{
    int pid = atwVtAlive(n);

    if (!pid)
	return;
    atwVtWrite(ATW_VT_FILE, n);
    atwVtLent = 0;		/* lent or not: n takes the keyboard now */
    atwDeactivate();
    kill(pid, SIGUSR2);
}

/* From the block and wakeup handlers. */
void
atwVtPoll(pTimeout)
    pointer pTimeout;
{
    static struct timeval tv;
    struct timeval **tvp = (struct timeval **)pTimeout;
    int want;

    if (atwScreen.vt == 0)
	return;
    if (tvp && (*tvp == NULL || (*tvp)->tv_sec > 0 || (*tvp)->tv_usec > 200000)) {
	tv.tv_sec = 0;
	tv.tv_usec = 200000;
	*tvp = &tv;
    }
    if (atwVtWant) {
	want = atwVtWant;
	atwVtWant = 0;
	if (atwScreen.active)
	    atwVtGive(want);
    }
    if (atwVtLend) {
	atwVtLend = 0;
	if (atwScreen.active && !atwVtLent)
	    atwVtLendInput();
    }
    if (atwVtLent && ++atwVtTick >= 5) {
	atwVtTick = 0;
	atwVtCheckLent();
    }
    if (!atwVtSig)
	return;
    atwVtSig = 0;
    want = atwVtRead(ATW_VT_FILE);
    if (want == atwScreen.vt) {
	if (!atwScreen.active)
	    atwActivate();
	else if (atwVtLent)
	    atwVtTakeInput("switched to");
    } else if (atwScreen.active) {
	if (atwVtAlive(want))
	    atwVtGive(want);
	else
	    atwVtWrite(ATW_VT_FILE, atwScreen.vt);	/* nobody there: keep it */
    }
}

/*
 * The server is going away. On show, it hands the card back to the one it
 * took it from (or any other still running), else switches the video off
 * as a lone server does. In the background it touches nothing.
 */
void
atwVtExit()
{
    int n, to = 0;

    if (atwScreen.vt == 0) {
	if (atwScreen.fb)
	    *(volatile unsigned short *)(atwScreen.fb + ATW_OFF_VTG + ATW_R_CTRL) = 0;
	return;
    }
    if (atwVtRead(atwVtPidFile(atwScreen.vt)) == (int)getpid())
	unlink(atwVtPidFile(atwScreen.vt));
    if (!atwScreen.active)
	return;
    if (atwVtAlive(atwVtPrev))
	to = atwVtPrev;
    for (n = 1; !to && n <= ATW_VT_MAX; n++)
	if (n != atwScreen.vt && atwVtAlive(n))
	    to = n;
    if (to) {
	atwVtGive(to);
	return;
    }
    unlink(ATW_VT_FILE);
    if (atwScreen.fb)
	*(volatile unsigned short *)(atwScreen.fb + ATW_OFF_VTG + ATW_R_CTRL) = 0;
}
