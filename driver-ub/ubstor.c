/*
 * ubstor.c - USB mass storage, bulk-only (BBB), SCSI transparent, for
 * the USB stack in driver-ub: what a stick, a card reader or a disk
 * enclosure speaks.  After U-Boot's usb_storage.c and the USB Mass
 * Storage Class Bulk-Only Transport spec; written for ASV: the class
 * driver registers with udd_register() and probes every interface the
 * core finds; ub_stor_read()/ub_stor_write() move 512-byte blocks.
 */
#include "ub.h"
#include "usb.h"
#include "usb_api.h"
#include "ubstor.h"

#define US_PR_BULK	0x50
#define US_BBB_RESET	0xff
#define US_BBB_GET_MAX_LUN 0xfe

#define CBW_SIGNATURE	0x43425355UL	/* "USBC", little-endian */
#define CSW_SIGNATURE	0x53425355UL	/* "USBS" */
#define CBW_FLAG_IN	0x80
#define CBW_SIZE	31
#define CSW_SIZE	13

struct cbw {
	unsigned long	sig, tag, len;	/* little-endian on the wire */
	unsigned char	flags, lun, cblen;
	unsigned char	cb[16];
};
struct csw {
	unsigned long	sig, tag, residue;
	unsigned char	status;
};

struct ub_stor ub_stor[UB_STOR_MAX];
static unsigned long cbw_tag = 1;
static long storage_probe();
static long storage_disconnect();
static long storage_ioctl();
static char stor_lname[] = "USB mass storage";
static struct uddif storage_uif = {
	0, USB_API_VERSION, USB_DEVICE, stor_lname, "storage", 0, 0,
	storage_probe, storage_disconnect, 0, storage_ioctl, 0
};

static void
put_le32(b, v)
unsigned char *b;
unsigned long v;
{
	b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24;
}

static unsigned long
get_le32(b)
unsigned char *b;
{
	return (unsigned long)b[0] | ((unsigned long)b[1] << 8) |
	    ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

static long
clear_stall(s, ep, out)
struct ub_stor *s;
int ep, out;
{
	long r = usb_control_msg(s->dev, usb_sndctrlpipe(s->dev, 0), USB_REQ_CLEAR_FEATURE,
	    USB_RECIP_ENDPOINT, 0, out ? ep : (ep | USB_DIR_IN), 0, 0, USB_CNTL_TIMEOUT * 5);

	usb_settoggle(s->dev, ep, out ? 1 : 0, 0);
	return r;
}

/* reset recovery (spec 5.3.4): mass storage reset, both bulk endpoints unhalted */
static void
bbb_reset(s)
struct ub_stor *s;
{
	usb_control_msg(s->dev, usb_sndctrlpipe(s->dev, 0), US_BBB_RESET,
	    USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0, s->ifnum, 0, 0, USB_CNTL_TIMEOUT * 5);
	mdelay(150);
	usb_clear_halt(s->dev, usb_rcvbulkpipe(s->dev, (long)s->ep_in));
	mdelay(150);
	usb_clear_halt(s->dev, usb_sndbulkpipe(s->dev, (long)s->ep_out));
	mdelay(150);
}

/*
 * One command: the CBW out, the data in or out, the CSW in.
 * 0 good, 1 the command failed (ask for sense), -1 the transport broke.
 */
static long
bbb_cmd(s, cb, cblen, data, len, in, actp)
struct ub_stor *s;
unsigned char *cb;
int cblen;
unsigned char *data;
unsigned long len;
int in;
unsigned long *actp;
{
	unsigned char w[CBW_SIZE], r[CSW_SIZE];
	long act, res;
	unsigned long pin = usb_rcvbulkpipe(s->dev, (long)s->ep_in);
	unsigned long pout = usb_sndbulkpipe(s->dev, (long)s->ep_out);
	int retry;

	memset(w, 0, sizeof w);
	put_le32(w, CBW_SIGNATURE);
	put_le32(w + 4, cbw_tag);
	put_le32(w + 8, len);
	w[12] = in ? CBW_FLAG_IN : 0;
	w[13] = s->lun;
	w[14] = cblen;
	memcpy(w + 15, cb, cblen);
	if (usb_bulk_msg(s->dev, pout, w, CBW_SIZE, &act, USB_CNTL_TIMEOUT * 5, 0) < 0 || act != CBW_SIZE) {
		DEBUG(("ubstor: CBW not taken, status %lx", s->dev->status));
		bbb_reset(s);
		return -1;
	}
	act = 0;
	if (len > 0) {
		res = usb_bulk_msg(s->dev, in ? pin : pout, data, len, &act, USB_CNTL_TIMEOUT * 5, 0);
		if (res < 0 && (s->dev->status & USB_ST_STALLED))
			res = clear_stall(s, in ? s->ep_in : s->ep_out, !in);
		if (res < 0) {
			DEBUG(("ubstor: data phase failed, status %lx", s->dev->status));
			bbb_reset(s);
			return -1;
		}
	}
	if (actp)
		*actp = act;
	for (retry = 0; retry < 2; retry++) {
		long al;

		res = usb_bulk_msg(s->dev, pin, r, CSW_SIZE, &al, USB_CNTL_TIMEOUT * 5, 0);
		if (res < 0 && retry == 0 && (s->dev->status & USB_ST_STALLED)) {
			clear_stall(s, s->ep_in, 0);
			continue;
		}
		break;
	}
	if (res < 0 || get_le32(r) != CSW_SIGNATURE || get_le32(r + 4) != cbw_tag || r[12] > 1) {
		DEBUG(("ubstor: bad CSW (res %ld)", res));
		cbw_tag++;
		bbb_reset(s);
		return -1;
	}
	cbw_tag++;
	return r[12];		/* 0 passed, 1 failed */
}

static long
request_sense(s)
struct ub_stor *s;
{
	unsigned char cb[6];

	memset(cb, 0, sizeof cb);
	cb[0] = 0x03;
	cb[4] = 18;
	memset(s->sense, 0, sizeof s->sense);
	return bbb_cmd(s, cb, 6, s->sense, 18, 1, (unsigned long *)0);
}

/* a command with sense taken on failure: 0, or -1 with s->sense filled */
static long
scsi(s, cb, cblen, data, len, in)
struct ub_stor *s;
unsigned char *cb;
int cblen;
unsigned char *data;
unsigned long len;
int in;
{
	long r = bbb_cmd(s, cb, cblen, data, len, in, (unsigned long *)0);

	if (r == 0)
		return 0;
	if (r > 0)
		request_sense(s);
	return -1;
}

long
ub_stor_test_ready(s)
struct ub_stor *s;
{
	unsigned char cb[6];

	memset(cb, 0, sizeof cb);
	return scsi(s, cb, 6, (unsigned char *)0, 0L, 0);
}

long
ub_stor_inquiry(s)
struct ub_stor *s;
{
	unsigned char cb[6], d[36];
	int i;

	memset(cb, 0, sizeof cb);
	cb[0] = 0x12;
	cb[4] = 36;
	memset(d, 0, sizeof d);
	if (scsi(s, cb, 6, d, 36L, 1) < 0)
		return -1;
	s->devtype = d[0] & 0x1f;
	s->removable = (d[1] & 0x80) != 0;
	for (i = 0; i < 8; i++)
		s->vendor[i] = d[8 + i] >= ' ' && d[8 + i] < 127 ? d[8 + i] : ' ';
	s->vendor[8] = 0;
	for (i = 0; i < 16; i++)
		s->product[i] = d[16 + i] >= ' ' && d[16 + i] < 127 ? d[16 + i] : ' ';
	s->product[16] = 0;
	for (i = 0; i < 4; i++)
		s->rev[i] = d[32 + i] >= ' ' && d[32 + i] < 127 ? d[32 + i] : ' ';
	s->rev[4] = 0;
	return 0;
}

long
ub_stor_capacity(s)
struct ub_stor *s;
{
	unsigned char cb[10], d[8];

	memset(cb, 0, sizeof cb);
	cb[0] = 0x25;
	memset(d, 0, sizeof d);
	if (scsi(s, cb, 10, d, 8L, 1) < 0)
		return -1;
	s->blocks = (((unsigned long)d[0] << 24) | ((unsigned long)d[1] << 16) |
	    ((unsigned long)d[2] << 8) | d[3]) + 1;
	s->blocksize = ((unsigned long)d[4] << 24) | ((unsigned long)d[5] << 16) |
	    ((unsigned long)d[6] << 8) | d[7];
	return 0;
}

/* n blocks from lba into buf (n * blocksize bytes) */
long
ub_stor_read(s, lba, n, buf)
struct ub_stor *s;
unsigned long lba, n;
unsigned char *buf;
{
	unsigned char cb[10];

	memset(cb, 0, sizeof cb);
	cb[0] = 0x28;
	cb[2] = lba >> 24; cb[3] = lba >> 16; cb[4] = lba >> 8; cb[5] = lba;
	cb[7] = n >> 8; cb[8] = n;
	return scsi(s, cb, 10, buf, n * s->blocksize, 1);
}

long
ub_stor_write(s, lba, n, buf)
struct ub_stor *s;
unsigned long lba, n;
unsigned char *buf;
{
	unsigned char cb[10];

	memset(cb, 0, sizeof cb);
	cb[0] = 0x2a;
	cb[2] = lba >> 24; cb[3] = lba >> 16; cb[4] = lba >> 8; cb[5] = lba;
	cb[7] = n >> 8; cb[8] = n;
	return scsi(s, cb, 10, buf, n * s->blocksize, 0);
}

/* the device just arrived: ready it, ask what it is and how big */
long
ub_stor_attach(s)
struct ub_stor *s;
{
	int i;

	for (i = 0; i < 10; i++) {
		if (ub_stor_test_ready(s) == 0)
			break;
		if (s->sense[2] == 0x02 && s->sense[12] == 0x3a)	/* no medium */
			return -1;
		mdelay(100);
	}
	if (ub_stor_inquiry(s) < 0)
		return -1;
	if (ub_stor_capacity(s) < 0)
		return -1;
	s->ready = 1;
	return 0;
}

static long
storage_probe(dev, ifnum)
struct usb_device *dev;
unsigned int ifnum;
{
	struct usb_interface *iface = &dev->config.if_desc[ifnum];
	struct ub_stor *s;
	int i;
	unsigned char maxlun;

	if (iface->desc.bInterfaceClass != USB_CLASS_MASS_STORAGE)
		return -1;
	if (iface->desc.bInterfaceSubClass != US_SC_SCSI || iface->desc.bInterfaceProtocol != US_PR_BULK) {
		ALERT(("ubstor: interface %d: subclass %x protocol %x not supported (only SCSI over bulk-only)",
		    ifnum, iface->desc.bInterfaceSubClass, iface->desc.bInterfaceProtocol));
		return -1;
	}
	for (i = 0; i < UB_STOR_MAX && ub_stor[i].dev != (struct usb_device *)0; i++)
		;
	if (i == UB_STOR_MAX) {
		ALERT(("ubstor: no room for another device"));
		return -1;
	}
	s = &ub_stor[i];
	memset((char *)s, 0, sizeof *s);
	s->dev = dev;
	s->ifnum = ifnum;
	for (i = 0; i < iface->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep = &iface->ep_desc[i];

		if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) {
			if (ep->bEndpointAddress & USB_DIR_IN)
				s->ep_in = ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
			else
				s->ep_out = ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
		}
	}
	if (s->ep_in == 0 || s->ep_out == 0) {
		ALERT(("ubstor: no bulk endpoints"));
		s->dev = (struct usb_device *)0;
		return -1;
	}
	usb_set_interface(dev, ifnum, 0);
	maxlun = 0;
	if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), US_BBB_GET_MAX_LUN,
	    USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN, 0, ifnum, &maxlun, 1,
	    USB_CNTL_TIMEOUT * 5) < 0)
		maxlun = 0;
	s->maxlun = maxlun;
	s->lun = 0;
	ALERT(("ubstor: device %d, bulk in %d out %d, %d LUN(s)", (int)dev->devnum, s->ep_in, s->ep_out, maxlun + 1));
	if (ub_stor_attach(s) == 0)
		ALERT(("ubstor: %s %s %s, %lu blocks of %lu bytes (%lu MB)%s", s->vendor, s->product, s->rev,
		    s->blocks, s->blocksize, s->blocks / 2048 * s->blocksize / 512, s->removable ? ", removable" : ""));
	else
		ALERT(("ubstor: no medium or not ready (sense %02x %02x %02x)", s->sense[2], s->sense[12], s->sense[13]));
	return 0;
}

static long
storage_disconnect(dev)
struct usb_device *dev;
{
	int i;

	for (i = 0; i < UB_STOR_MAX; i++)
		if (ub_stor[i].dev == dev)
			ub_stor[i].dev = (struct usb_device *)0, ub_stor[i].ready = 0;
	return 0;
}

static long
storage_ioctl(u, cmd, arg)
struct uddif *u;
short cmd;
long arg;
{
	return ENOSYS;
}

long
ub_stor_init()
{
	return udd_register(&storage_uif);
}
