/*
 * ubhid.h - USB HID keyboards and mice on the USB stack in driver-ub
 * (ubhid.c): what ub.c needs of them.
 */
#ifndef UBHID_H
#define UBHID_H
#define UB_HID_MAX	4

#define UB_HID_KBD	1		/* boot-protocol keyboard */
#define UB_HID_MOUSE	2		/* boot-protocol mouse */

struct ub_hid {
	struct usb_device *dev;		/* 0: free */
	int		ifnum, kind;
	unsigned long	pipe;		/* the interrupt IN pipe */
	int		maxp;
	unsigned char	old[8];		/* the previous report */
	unsigned char	leds;		/* keyboard: the LED report sent */
	unsigned long	polls, reports;	/* statistics */
};

extern struct ub_hid ub_hid[UB_HID_MAX];

long ub_hid_init();
int ub_hid_present();
void ub_hid_poll();
void ub_ikbd_put();		/* (unsigned char *bytes, int n): ub.c */
#endif
