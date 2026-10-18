/*
 * ubuser - the USB stack in user space, on the Lightning VME (-l) or the
 * NetUSBee (-n): sets the host controller going, polls the hubs for a
 * while, lists what was found.
 *
 *   ubuser [-l|-n] [SECONDS]
 */
#include "ub.h"
#include "usb.h"
#include "hub.h"
#include "usb_api.h"

long ub_hcd_init(void);
struct usb_device *ub_root_hub(void);

int
main(argc, argv)
int argc;
char **argv;
{
	int bus = UB_LIGHTNING, secs = 5, i, t;
	struct usb_device *d;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-n") == 0)
			bus = UB_NETUSBEE;
		else if (strcmp(argv[i], "-l") == 0)
			bus = UB_LIGHTNING;
		else
			secs = atoi(argv[i]);
	}
	if (ub_io_init(bus) < 0) {
		printf("no ISP1160 on the %s\n", bus == UB_LIGHTNING ? "Lightning VME" : "NetUSBee");
		return 1;
	}
	printf("ISP1160 on the %s, words %s\n", bus == UB_LIGHTNING ? "Lightning VME" : "NetUSBee",
	    ub_swap ? "swapped" : "as they come");
	usb_init();
	if (ub_hcd_init() < 0)
		return 1;
	for (t = 0; t < secs * 2; t++) {
		for (i = 0; i < USB_MAX_HUB; i++) {
			struct usb_hub_device *hub = usb_get_hub_index(i);
			if (hub)
				usb_hub_events(hub);
		}
		mdelay(500);
	}
	for (i = 0; i < USB_MAX_DEVICE; i++) {
		d = usb_get_dev_index(i);
		if (d == NULL)
			continue;
		printf("dev %d: addr %d  %04x:%04x  class %d  \"%s\" \"%s\" \"%s\"%s\n", i, (int)d->devnum,
		    d->descriptor.idVendor, d->descriptor.idProduct, d->descriptor.bDeviceClass,
		    d->mf, d->prod, d->serial, d->maxchild ? " (hub)" : "");
	}
	return 0;
}
