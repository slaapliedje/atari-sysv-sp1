/*
 * atwIo.c - input for the ATW800/2 ddx: the raw IKBD byte stream from
 * /dev/ikbd, split into keyboard make/break codes and relative mouse
 * packets, fed to dix as X events. The software cursor is mi's.
 *
 * IKBD packets (Atari ST/TT): a byte < 0xF6 is a key scan code, bit 7 set
 * on release. 0xF8..0xFB start a 3-byte relative mouse packet: header
 * bits 0-1 are the right/left buttons, then dx, dy as signed bytes. The
 * other 0xF6..0xFF headers carry fixed-length reports we skip.
 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>

#include "atw.h"
#include "mipointer.h"
#include "keysym.h"

#define TVTOMILLI(tv)	((tv).tv_usec / 1000 + (tv).tv_sec * 1000)
#define MIN_KEYCODE	8

int	atwIkbdFd = -1;
long	atwLastEventTime;
extern int atwInputPending, atwInputProcessed;
extern int screenIsSaved;

static DevicePtr atwPointer, atwKeyboard;
static int	 atwDx, atwDy;		/* motion not yet delivered */
static int	 atwButtons;		/* IKBD button bits last seen */

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

static int
atwMouseGetMotionEvents(buff, start, stop)
    xTimecoord	*buff;
    CARD32	start, stop;
{
    return 0;
}

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
	InitPointerDeviceStruct(pDev, map, 3, atwMouseGetMotionEvents,
				atwMouseCtrl, 0);
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
	if (atwIkbdFd < 0) {
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
	AddEnabledDevice(atwIkbdFd);
	break;
    case DEVICE_OFF:
    case DEVICE_CLOSE:
	pDev->on = FALSE;
	RemoveEnabledDevice(atwIkbdFd);
	break;
    }
    return Success;
}

Bool
LegalModifier(key)
    BYTE key;
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
	miPointerDeltaCursor(screenInfo.screens[0], atwDx, atwDy, TRUE);
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
    miPointerPosition(screenInfo.screens[0],
		      &xE.u.keyButtonPointer.rootX,
		      &xE.u.keyButtonPointer.rootY);
    (*atwPointer->processInputProc)(&xE, atwPointer, 1);
}

static void
atwKey(code)
    int code;
{
    xEvent xE;

    atwFlushMotion();
    xE.u.u.type = (code & 0x80) ? KeyRelease : KeyPress;
    xE.u.u.detail = (code & 0x7f) + MIN_KEYCODE;
    xE.u.keyButtonPointer.time = atwLastEventTime;
    (*atwKeyboard->processInputProc)(&xE, atwKeyboard, 1);
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

void
ProcessInputEvents()
{
    static unsigned char buf[256];
    static int have;		/* bytes carried over: a split packet */
    struct timeval now;
    int n, i;

    atwInputProcessed = atwInputPending;
    for (;;) {
	n = read(atwIkbdFd, (char *)buf + have, sizeof buf - have);
	if (n <= 0)
	    break;
	n += have;
	have = 0;
	gettimeofday(&now, (struct timezone *)0);
	atwLastEventTime = TVTOMILLI(now);
	if (screenIsSaved == SCREEN_SAVER_ON)
	    SaveScreens(SCREEN_SAVER_OFF, ScreenSaverReset);

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
