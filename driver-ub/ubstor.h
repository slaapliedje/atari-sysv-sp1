#ifndef UBSTOR_H
#define UBSTOR_H
/* ubstor.h - USB mass storage devices (ubstor.c) */
#define UB_STOR_MAX	4

struct ub_stor {
	struct usb_device *dev;		/* 0: free */
	int	ifnum, ep_in, ep_out;
	int	maxlun, lun;
	int	ready, removable, devtype;
	unsigned long blocks, blocksize;
	char	vendor[9], product[17], rev[5];
	unsigned char sense[18];
};
extern struct ub_stor ub_stor[UB_STOR_MAX];

long ub_stor_init();			/* register the class driver */
long ub_stor_attach();			/* (s) ready, inquiry, capacity */
long ub_stor_read();			/* (s, lba, n, buf) */
long ub_stor_write();			/* (s, lba, n, buf) */
long ub_stor_test_ready();		/* (s) */
#endif /* UBSTOR_H */
