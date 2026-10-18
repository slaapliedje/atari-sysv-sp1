/*
 * ubioc.h - /dev/ub0, the USB driver's control device (ub.c): what the
 * user side (ubtool, ubd) asks of it.
 */
#define UBIOC		('U' << 8)
#define UBIOC_START	(UBIOC | 1)	/* set the stack going (idempotent), then poll */
#define UBIOC_POLL	(UBIOC | 2)	/* hub events: new and gone devices */
#define UBIOC_DEVS	(UBIOC | 3)	/* arg: struct ub_devinfo[UB_NDEV] */
#define UBIOC_STOR	(UBIOC | 4)	/* arg: struct ub_storinfo[UB_NSTOR] */
#define UBIOC_RD	(UBIOC | 5)	/* arg: struct ub_rw, blocks in */
#define UBIOC_WR	(UBIOC | 6)	/* arg: struct ub_rw, blocks out (root) */
#define UBIOC_STATUS	(UBIOC | 7)	/* returns ub_state */

#define UB_NDEV		32		/* USB_MAX_DEVICE */
#define UB_NSTOR	4		/* UB_STOR_MAX */
#define UB_RWMAX	(64 * 1024)	/* bytes per UBIOC_RD/WR */

struct ub_devinfo {
	int		present, addr, hub, vid, pid, class;
	char		mf[32], prod[32], serial[32];
};

struct ub_storinfo {
	int		present, ready, removable, devaddr;
	unsigned long	blocks, blocksize;
	char		vendor[9], product[17], rev[5];
};

struct ub_rw {
	int		unit;		/* index into UBIOC_STOR's list */
	unsigned long	lba, n;		/* blocks */
	char		*buf;		/* n * blocksize bytes */
};

/* ub_state */
#define UB_NOCHIP	0
#define UB_CHIP		1		/* the host controller answers */
#define UB_RUNNING	2		/* the stack is up */
