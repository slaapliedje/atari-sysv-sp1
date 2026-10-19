/*
 * ubhid.c - USB keyboards and mice (HID boot protocol) for the USB stack
 * in driver-ub, as a second IKBD: each report becomes the bytes the
 * Atari's keyboard controller would have sent (scancodes, make and
 * break; relative mouse records F8-FB dx dy), and ub_ikbd_put() in ub.c
 * puts them where the kernel's ikbd interrupt puts the real ones, so the
 * console and the X server (which reads /dev/ikbd) see one keyboard and
 * one mouse.  The key table and the special keys follow FreeMiNT's
 * usb_keyboard.c (Christian Zietz, GPL-2): a US layout to Atari ST
 * scancodes, F11/F12 as Help/Undo, End/PgUp/PgDn as Shift+Home/Up/Down.
 * The devices are polled from the daemon (ub_hid_poll), as FreeMiNT does
 * from a kernel thread: an interrupt pipe is read as a bulk message that
 * the host controller driver tries once (a NAK, nothing to report, is
 * the usual answer).
 */
#include "ub.h"
#include "usb.h"
#include "usb_api.h"
#include "ubhid.h"

struct ub_hid ub_hid[UB_HID_MAX];

static long hid_probe();
static long hid_disconnect();
static long hid_ioctl();
static char hid_lname[] = "USB HID keyboard and mouse";
static struct uddif hid_uif = {
	0, USB_API_VERSION, USB_DEVICE, hid_lname, "hid", 0, 0,
	hid_probe, hid_disconnect, 0, hid_ioctl, 0
};

/* HID usage (keyboard page) -> Atari scancode; 0 = no key */
static unsigned char hid_scan[0x68] = {
	0x00, 0x00, 0x00, 0x00, 0x1e, 0x30, 0x2e, 0x20,	/* 00: -, -, -, -, A, B, C, D */
	0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,	/* 08: E, F, G, H, I, J, K, L */
	0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14,	/* 10: M, N, O, P, Q, R, S, T */
	0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x02, 0x03,	/* 18: U, V, W, X, Y, Z, 1, 2 */
	0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,	/* 20: 3, 4, 5, 6, 7, 8, 9, 0 */
	0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a,	/* 28: Return, ESC, Backspace, TAB, Space, - and _, = and +, [ and { */
	0x1b, 0x2b, 0x2b, 0x27, 0x28, 0x29, 0x33, 0x34,	/* 30: ] and }, \ and |, Non-US # and ~, ; and :, ' and ", ` and ~, ,, . */
	0x35, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40,	/* 38: / and ?, Caps-Lock, F1, F2, F3, F4, F5, F6 */
	0x41, 0x42, 0x43, 0x44, 0x62, 0x61, 0x00, 0x63,	/* 40: F7, F8, F9, F10, F11 -> Help, F12 -> Undo, -, Scroll Lock -> KP ( */
	0x64, 0x52, 0x47, 0x00, 0x53, 0x00, 0x00, 0x4d,	/* 48: Pause -> KP ), Insert, Home -> ClrHome, -, Delete, -, -, Right arrow */
	0x4b, 0x50, 0x48, 0x00, 0x65, 0x66, 0x4a, 0x4e,	/* 50: Left arrow, Down arrow, Up arrow, -, Keypad /, Keypad *, Keypad -, Keypad + */
	0x72, 0x6d, 0x6e, 0x6f, 0x6a, 0x6b, 0x6c, 0x67,	/* 58: Keypad Enter, Keypad 1, Keypad 2, Keypad 3, Keypad 4, Keypad 5, Keypad 6, Keypad 7 */
	0x68, 0x69, 0x70, 0x71, 0x60, 0x00, 0x00, 0x00,	/* 60: Keypad 8, Keypad 9, Keypad 0, Keypad ., Non-US \ and |, -, -, - */
};

/* the keys the Atari keyboard lacks, as the chord FreeMiNT sends */
#define HID_PRTSCR	0x46	/* Alt+Help */
#define HID_PGUP	0x4b	/* Shift+Up */
#define HID_END		0x4d	/* Shift+Home */
#define HID_PGDN	0x4e	/* Shift+Down */

#define SC_CTRL		0x1d
#define SC_LSHIFT	0x2a
#define SC_RSHIFT	0x36
#define SC_ALT		0x38
#define SC_HOME		0x47
#define SC_UP		0x48
#define SC_DOWN		0x50
#define SC_HELP		0x62
#define SC_BREAK	0x80

/* the IKBD bytes of one report go out together */
static unsigned char hid_out[128];
static int hid_nout;

static void
put(b)
int b;
{
	if (hid_nout < sizeof hid_out)
		hid_out[hid_nout++] = b;
}

static void
flush()
{
	if (hid_nout)
		ub_ikbd_put(hid_out, hid_nout);
	hid_nout = 0;
}

/* a key by its usage, make (brk 0) or break (brk SC_BREAK) */
static void
key(usage, brk)
int usage, brk;
{
	int sc;

	switch (usage) {
	case HID_END:	put(SC_LSHIFT | brk); put(SC_HOME | brk); return;
	case HID_PGUP:	put(SC_LSHIFT | brk); put(SC_UP | brk); return;
	case HID_PGDN:	put(SC_LSHIFT | brk); put(SC_DOWN | brk); return;
	case HID_PRTSCR: put(SC_ALT | brk); put(SC_HELP | brk); return;
	}
	sc = usage < sizeof hid_scan ? hid_scan[usage] : 0;
	if (sc)
		put(sc | brk);
}

/* the modifier byte's bits: 0 LCtrl 1 LShift 2 LAlt 3 LGUI 4 RCtrl 5 RShift 6 RAlt 7 RGUI */
static void
modifiers(bits, brk)
int bits, brk;
{
	if (bits & 0x11)
		put(SC_CTRL | brk);
	if (bits & 0x02)
		put(SC_LSHIFT | brk);
	if (bits & 0x20)
		put(SC_RSHIFT | brk);
	if (bits & 0x44)
		put(SC_ALT | brk);
}

static void
set_leds(h, leds)
struct ub_hid *h;
int leds;
{
	unsigned char b = leds;

	if (usb_control_msg(h->dev, usb_sndctrlpipe(h->dev, 0), USB_REQ_SET_REPORT,
	    USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0x0200, h->ifnum, &b, 1,
	    USB_CNTL_TIMEOUT * 5) >= 0)
		h->leds = leds;
}

#define LED_CAPS	0x02
#define HID_CAPSLOCK	0x39

/* a keyboard report: modifiers, reserved, up to six keys held */
static void
kbd_report(h, r, n)
struct ub_hid *h;
unsigned char *r;
int n;
{
	int i, j, changed;

	while (n < 8)
		r[n++] = 0;
	if (r[2] == 1)			/* rollover: too many keys, no information */
		return;
	changed = h->old[0] ^ r[0];
	modifiers(changed & h->old[0], SC_BREAK);
	modifiers(changed & r[0], 0);
	for (i = 2; i < 8; i++) {	/* released: in the old report, not the new */
		if (r[i] == 0 && h->old[i] == 0)
			continue;
		if (h->old[i]) {
			for (j = 2; j < 8 && r[j] != h->old[i]; j++)
				;
			if (j == 8)
				key(h->old[i], SC_BREAK);
		}
	}
	for (i = 2; i < 8; i++) {	/* pressed: in the new, not the old */
		if (r[i] == 0)
			continue;
		for (j = 2; j < 8 && h->old[j] != r[i]; j++)
			;
		if (j == 8) {
			key(r[i], 0);
			if (r[i] == HID_CAPSLOCK)
				set_leds(h, h->leds ^ LED_CAPS);
		}
	}
	flush();
	memcpy(h->old, r, 8);
}

/* a mouse report: buttons (0 left 1 right 2 middle), dx, dy, [wheel] */
static void
mouse_report(h, r, n)
struct ub_hid *h;
unsigned char *r;
int n;
{
	int i, w;

	if (n >= 4 && r[3]) {		/* the wheel: arrow keys, one per notch */
		w = (signed char)r[3];
		for (i = w < 0 ? -w : w; i > 0; i--) {
			put(w > 0 ? SC_UP : SC_DOWN);
			put((w > 0 ? SC_UP : SC_DOWN) | SC_BREAK);
		}
	}
	if (r[1] || r[2] || (r[0] ^ h->old[0]) & 3) {
		put(0xf8 | ((r[0] & 1) << 1) | ((r[0] & 2) >> 1));
		put(r[1]);
		put(r[2]);
	}
	flush();
	h->old[0] = r[0];
}

int
ub_hid_present()
{
	int i;

	for (i = 0; i < UB_HID_MAX; i++)
		if (ub_hid[i].dev)
			return 1;
	return 0;
}

/* the daemon's poll: one try at each device's interrupt pipe */
void
ub_hid_poll()
{
	unsigned char buf[8];
	long act;
	int i;

	for (i = 0; i < UB_HID_MAX; i++) {
		struct ub_hid *h = &ub_hid[i];

		if (h->dev == (struct usb_device *)0)
			continue;
		h->polls++;
		act = 0;
		if (usb_bulk_msg(h->dev, h->pipe, buf, h->maxp > 8 ? 8 : h->maxp, &act,
		    USB_CNTL_TIMEOUT, 0) < 0 || act < 3)
			continue;
		h->reports++;
		if (h->kind == UB_HID_KBD)
			kbd_report(h, buf, (int)act);
		else
			mouse_report(h, buf, (int)act);
	}
}

static long
hid_probe(dev, ifnum)
struct usb_device *dev;
unsigned int ifnum;
{
	struct usb_interface *iface = &dev->config.if_desc[ifnum];
	struct usb_endpoint_descriptor *ep;
	struct ub_hid *h;
	int i, kind;

	if (iface->desc.bInterfaceClass != USB_CLASS_HID)
		return -1;
	if (iface->desc.bInterfaceSubClass != USB_SUB_HID_BOOT) {
		ALERT(("ubhid: device %d interface %d: HID without the boot protocol, not handled", (int)dev->devnum, ifnum));
		return -1;
	}
	if (iface->desc.bInterfaceProtocol == 1)
		kind = UB_HID_KBD;
	else if (iface->desc.bInterfaceProtocol == 2)
		kind = UB_HID_MOUSE;
	else
		return -1;
	ep = (struct usb_endpoint_descriptor *)0;
	for (i = 0; i < iface->desc.bNumEndpoints; i++)
		if ((iface->ep_desc[i].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT &&
		    (iface->ep_desc[i].bEndpointAddress & USB_DIR_IN))
			ep = &iface->ep_desc[i];
	if (ep == (struct usb_endpoint_descriptor *)0) {
		ALERT(("ubhid: device %d: no interrupt IN endpoint", (int)dev->devnum));
		return -1;
	}
	for (i = 0; i < UB_HID_MAX && ub_hid[i].dev != (struct usb_device *)0; i++)
		;
	if (i == UB_HID_MAX) {
		ALERT(("ubhid: no room for another device"));
		return -1;
	}
	h = &ub_hid[i];
	memset((char *)h, 0, sizeof *h);
	h->dev = dev;
	h->ifnum = ifnum;
	h->kind = kind;
	h->pipe = usb_rcvintpipe(dev, (long)(ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK));
	h->maxp = usb_maxpacket(dev, h->pipe);
	usb_set_protocol(dev, ifnum, 0);	/* the boot protocol's fixed reports */
	usb_set_idle(dev, ifnum, 0, 0);		/* a report only when something changed */
	if (kind == UB_HID_KBD)
		set_leds(h, 0);
	ALERT(("ubhid: USB %s, device %d interface %d, reports of %d bytes every %d ms",
	    kind == UB_HID_KBD ? "keyboard" : "mouse", (int)dev->devnum, ifnum, h->maxp, ep->bInterval));
	return 0;
}

static long
hid_disconnect(dev)
struct usb_device *dev;
{
	int i;

	for (i = 0; i < UB_HID_MAX; i++)
		if (ub_hid[i].dev == dev) {
			ALERT(("ubhid: USB %s, device %d, gone", ub_hid[i].kind == UB_HID_KBD ? "keyboard" : "mouse", (int)dev->devnum));
			ub_hid[i].dev = (struct usb_device *)0;
		}
	return 0;
}

static long
hid_ioctl(u, cmd, arg)
struct uddif *u;
short cmd;
long arg;
{
	return ENOSYS;
}

long
ub_hid_init()
{
	return udd_register(&hid_uif);
}
