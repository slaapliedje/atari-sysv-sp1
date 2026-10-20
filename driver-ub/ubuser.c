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
#include "ubstor.h"
#include <stdarg.h>

void
ub_uprintf(char *fmt, ...)
{
	static char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsprintf(buf, fmt, ap);
	va_end(ap);
	if (n > 0)
		write(1, buf, n);
}

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
		ub_uprintf("no ISP1160 on the %s\n", bus == UB_LIGHTNING ? "Lightning VME" : "NetUSBee");
		return 1;
	}
	ub_uprintf("ISP1160 on the %s, words %s\n", bus == UB_LIGHTNING ? "Lightning VME" : "NetUSBee",
	    ub_swap ? "swapped" : "as they come");
	usb_init();
	ub_stor_init();
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
		ub_uprintf("dev %d: addr %d  %04x:%04x  class %d  \"%s\" \"%s\" \"%s\"%s\n", i, (int)d->devnum,
		    d->descriptor.idVendor, d->descriptor.idProduct, d->descriptor.bDeviceClass,
		    d->mf, d->prod, d->serial, d->maxchild ? " (hub)" : "");
	}
	for (i = 0; i < UB_STOR_MAX; i++) {
		static unsigned char sec[2048];
		struct ub_stor *s = &ub_stor[i];
		int k;

		if (s->dev == NULL || !s->ready)
			continue;
		if (ub_stor_read(s, 0L, 1L, sec) < 0) {
			ub_uprintf("read of block 0 failed (sense %02x %02x %02x)\n", s->sense[2], s->sense[12], s->sense[13]);
			continue;
		}
		ub_uprintf("block 0 of %s %s:", s->vendor, s->product);
		for (k = 0; k < 32; k++)
			ub_uprintf("%s%02x", k % 16 ? " " : "\n  ", sec[k]);
		ub_uprintf("\n  bytes 510-511: %02x %02x%s\n", sec[510], sec[511],
		    sec[510] == 0x55 && sec[511] == 0xaa ? " (boot signature)" : "");
		if (sec[510] == 0x55 && sec[511] == 0xaa) {
			for (k = 0; k < 4; k++) {
				unsigned char *p = sec + 446 + 16 * k;
				if (p[4])
					ub_uprintf("  partition %d: type %02x, start %lu, %lu blocks\n", k + 1, p[4],
					    (unsigned long)p[8] | ((unsigned long)p[9] << 8) | ((unsigned long)p[10] << 16) | ((unsigned long)p[11] << 24),
					    (unsigned long)p[12] | ((unsigned long)p[13] << 8) | ((unsigned long)p[14] << 16) | ((unsigned long)p[15] << 24));
			}
			if (sec[0] == 0xeb || sec[0] == 0xe9)
				ub_uprintf("  looks like a FAT boot sector: OEM \"%.8s\", %u bytes/sector, %u sectors/cluster\n",
				    sec + 3, sec[11] | (sec[12] << 8), sec[13]);
		}
	}
	return 0;
}
