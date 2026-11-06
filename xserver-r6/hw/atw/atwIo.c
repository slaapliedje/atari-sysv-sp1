/*
 * atwIo.c - input for the ATW800/2 ddx (X11R6.3): the raw IKBD byte stream from
 * /dev/ikbd, split into keyboard make/break codes and relative mouse
 * packets, fed to dix as X events. The software cursor is mi's.
 *
 * IKBD packets (Atari ST/TT): a byte < 0xF6 is a key scan code, bit 7 set
 * on release. 0xF8..0xFB start a 3-byte relative mouse packet: header
 * bits 0-1 are the right/left buttons, then dx, dy as signed bytes. The
 * other 0xF6..0xFF headers carry fixed-length reports we skip.
 *
 * With -vt, Ctrl+Alt+F1..F9 switches to another server (atwVt.c) and
 * never reaches the clients; /dev/ikbd is open only while we are on show.
 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>

#include "atw.h"
#include "mi.h"
#include "keysym.h"

#define TVTOMILLI(tv)	((tv).tv_usec / 1000 + (tv).tv_sec * 1000)
#define MIN_KEYCODE	8

int	atwIkbdFd = -1;
long	atwLastEventTime;

static DevicePtr atwPointer, atwKeyboard;
static int	 atwDx, atwDy;		/* motion not yet delivered */
static int	 atwButtons;		/* IKBD button bits last seen */
static unsigned char atwDown[128];	/* keys the clients were told are down */
static unsigned char atwEaten[128];	/* a switch key: eat its release too */

#define SC_CTRL		0x1D		/* IKBD scan codes */
#define SC_ALT		0x38
#define SC_F1		0x3B
#define SC_F9		0x43
#define SC_F10		0x44

static int	atwCtrl, atwAlt;	/* held now, as far as we have seen */

static void atwFlushMotion();

int
TimeSinceLastInputEvent()
{
    struct timeval now;

    gettimeofday(&now, (struct timezone *)0);
    if (atwLastEventTime == 0)
	atwLastEventTime = TVTOMILLI(now);
    return TVTOMILLI(now) - atwLastEventTime;
}

void
SetTimeSinceLastInputEvent()
{
    struct timeval now;

    gettimeofday(&now, (struct timezone *)0);
    atwLastEventTime = TVTOMILLI(now);
}

/*
 * Pointer device.
 */
static void
atwMouseCtrl(pDev, ctrl)
    DevicePtr	pDev;
    PtrCtrl	*ctrl;
{
}

/*
 * mi's pointer: one screen, so the cursor never goes off it.
 */
static Bool
atwCursorOffScreen(pScreen, x, y)
    ScreenPtr	*pScreen;
    int		*x, *y;
{
    return FALSE;
}

static void
atwCrossScreen(pScreen, entering)
    ScreenPtr	pScreen;
    Bool	entering;
{
}

miPointerScreenFuncRec atwPointerScreenFuncs = {
    atwCursorOffScreen,
    atwCrossScreen,
    miPointerWarpCursor,
    mieqEnqueue,
    mieqSwitchScreen,
};

int
atwMouseProc(pDev, what)
    DevicePtr	pDev;
    int		what;
{
    BYTE map[4];

    switch (what) {
    case DEVICE_INIT:
	map[1] = 1;
	map[2] = 2;
	map[3] = 3;
	InitPointerDeviceStruct(pDev, map, 3, miPointerGetMotionEvents,
				atwMouseCtrl, miPointerGetMotionBufferSize());
	atwPointer = pDev;
	break;
    case DEVICE_ON:
	pDev->on = TRUE;
	break;
    case DEVICE_OFF:
    case DEVICE_CLOSE:
	pDev->on = FALSE;
	break;
    }
    return Success;
}

/*
 * Keyboard device. The bell is the PSG; /dev/psg exists on the stock
 * image but its interface is not documented, so no bell for now.
 */
static void
atwBell(loudness, pDev)
    int		loudness;
    DevicePtr	pDev;
{
}

static void
atwKbdCtrl(pDev, ctrl)
    DevicePtr	pDev;
    KeybdCtrl	*ctrl;
{
}

int
atwKbdProc(pDev, what)
    DevicePtr	pDev;
    int		what;
{
    switch (what) {
    case DEVICE_INIT:
	/* in the background the server on show has the device */
	if (atwIkbdFd < 0 && atwScreen.active) {
	    atwIkbdFd = open("/dev/ikbd", O_RDONLY | O_NDELAY);
	    if (atwIkbdFd < 0) {
		ErrorF("atw: cannot open /dev/ikbd (errno %d)\n", errno);
		return !Success;
	    }
	}
	atwInitModMap();
	InitKeyboardDeviceStruct(pDev, &atwKeySyms, atwModMap, atwBell,
				 atwKbdCtrl);
	atwKeyboard = pDev;
	break;
    case DEVICE_ON:
	pDev->on = TRUE;
	if (atwIkbdFd >= 0)
	    AddEnabledDevice(atwIkbdFd);
	break;
    case DEVICE_OFF:
    case DEVICE_CLOSE:
	pDev->on = FALSE;
	if (atwIkbdFd >= 0)
	    RemoveEnabledDevice(atwIkbdFd);
	break;
    }
    return Success;
}

/* On show (atwVt.c): the keyboard and mouse are ours. */
void
atwInputOn()
{
    if (atwIkbdFd < 0) {
	atwIkbdFd = open("/dev/ikbd", O_RDONLY | O_NDELAY);
	if (atwIkbdFd < 0) {
	    ErrorF("atw: cannot open /dev/ikbd (errno %d)\n", errno);
	    return;
	}
    }
    if (atwKeyboard && atwKeyboard->on)
	AddEnabledDevice(atwIkbdFd);
    atwCtrl = atwAlt = 0;	/* their releases went to whoever had the keyboard */
}

/* ... and now the next server's. */
void
atwInputOff()
{
    if (atwIkbdFd < 0)
	return;
    RemoveEnabledDevice(atwIkbdFd);
    close(atwIkbdFd);
    atwIkbdFd = -1;
}

Bool
LegalModifier(key, pDev)
    unsigned int key;
    DevicePtr pDev;
{
    return TRUE;
}

/*
 * Event delivery.
 */
static void
atwFlushMotion()
{
    if (atwDx || atwDy) {
	miPointerDeltaCursor(atwDx, atwDy, atwLastEventTime);
	atwDx = atwDy = 0;
    }
}

static void
atwButton(button, down)
    int button, down;
{
    xEvent xE;

    atwFlushMotion();
    if (getenv("ATW_DEBUG"))
	ErrorF("atw: button %d %s\n", button, down ? "down" : "up");
    xE.u.u.type = down ? ButtonPress : ButtonRelease;
    xE.u.u.detail = button;
    xE.u.keyButtonPointer.time = atwLastEventTime;
    mieqEnqueue(&xE);
}

static void
atwKey(code)
    int code;
{
    int sc = code & 0x7f, up = code & 0x80;
    xEvent xE;

    if (getenv("ATW_DEBUG"))
	ErrorF("atw: key %02x\n", code);
    if (sc == SC_CTRL)
	atwCtrl = !up;
    else if (sc == SC_ALT)
	atwAlt = !up;
    if (atwScreen.vt && sc >= SC_F1 && sc <= SC_F10) {
	if (!up && atwCtrl && atwAlt) {
	    if (getenv("ATW_DEBUG"))
		ErrorF("atw: Ctrl+Alt+F%d\n", sc - SC_F1 + 1);
	    atwEaten[sc] = 1;
	    if (sc == SC_F10)
		atwVtConsole();
	    else
		atwVtSwitch(sc - SC_F1 + 1);
	    return;
	}
	if (up && atwEaten[sc]) {
	    atwEaten[sc] = 0;
	    return;
	}
    }
    atwDown[sc] = !up;
    atwFlushMotion();
    xE.u.u.type = (code & 0x80) ? KeyRelease : KeyPress;
    xE.u.u.detail = (code & 0x7f) + MIN_KEYCODE;
    xE.u.keyButtonPointer.time = atwLastEventTime;
    mieqEnqueue(&xE);
}

/*
 * A relative mouse packet: header buttons then dx, dy. IKBD bit 1 is the
 * left button, bit 0 the right one; X buttons are 1 = left, 3 = right.
 */
static void
atwMousePacket(hdr, dx, dy)
    int hdr, dx, dy;
{
    int b = hdr & 3;

    if (dx || dy) {
	atwDx += dx;
	atwDy += dy;
    }
    if ((b ^ atwButtons) & 2)
	atwButton(1, b & 2);
    if ((b ^ atwButtons) & 1)
	atwButton(3, b & 1);
    atwButtons = b;
}

/* Leaving the screen: release, to the clients, every key and button they
 * were told is down - their real releases will go to the next server. */
int
atwKeysReleaseAll()
{
    int sc, n = 0;

    for (sc = 0; sc < 128; sc++)
	if (atwDown[sc]) {
	    atwKey(sc | 0x80);
	    n++;
	}
    if (atwButtons & 2)
	atwButton(1, 0);
    if (atwButtons & 1)
	atwButton(3, 0);
    atwButtons = 0;
    atwDx = atwDy = 0;
    return n;
}

void
ProcessInputEvents()
{
    mieqProcessInputEvents();
    miPointerUpdate();
}

/*
 * Drain /dev/ikbd into mi's event queue (from the wakeup handler).
 */
void
atwReadInput()
{
    static unsigned char buf[256];
    static int have;		/* bytes carried over: a split packet */
    struct timeval now;
    int n, i;

    for (;;) {
	n = read(atwIkbdFd, (char *)buf + have, sizeof buf - have);
	if (n <= 0)
	    break;
	n += have;
	have = 0;
	gettimeofday(&now, (struct timezone *)0);
	atwLastEventTime = TVTOMILLI(now);

	for (i = 0; i < n; ) {
	    int c = buf[i];
	    int len;

	    if (c < 0xF6) {			/* key */
		atwKey(c);
		i++;
		continue;
	    }
	    switch (c) {
	    case 0xF8: case 0xF9: case 0xFA: case 0xFB: len = 3; break;
	    case IKBD_ABSMOUSE:  len = 6; break;
	    case IKBD_TIME:      len = 7; break;
	    case IKBD_JOYS:      len = 3; break;
	    case IKBD_JOY0: case IKBD_JOY1: len = 2; break;
	    default:             len = 1; break;	/* status etc.: drop */
	    }
	    if (i + len > n) {			/* wait for the rest */
		have = n - i;
		bcopy((char *)buf + i, (char *)buf, have);
		break;
	    }
	    if (getenv("ATW_DEBUG") && c >= 0xF6)
		ErrorF("atw: ikbd %02x %02x %02x\n", c, buf[i + 1], buf[i + 2]);
	    if (c >= 0xF8 && c <= 0xFB)
		atwMousePacket(c, (int)(signed char)buf[i + 1],
			       (int)(signed char)buf[i + 2]);
	    i += len;
	}
    }
    atwFlushMotion();
}
