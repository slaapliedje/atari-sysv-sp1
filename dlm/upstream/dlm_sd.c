/*
 * dlm_sd.c -- the SCSI host adapter list under the base's disk driver
 * (sd.h: sdopen, sdqueue, sdhardwarename).  Controller drivers, boot
 * modules or static, add themselves with sd_attach.  Cards are
 * numbered by address, as the base's autoconfig walk numbers them.
 *
 * The first open of a card with no driver loads the modules named by
 * the host's sd_modnames[] from the module path until one serves it.
 * A host can hold a card number with a null queue; the driver
 * attaching at that address takes it.
 *
 * K&R C.
 */

#include "sys/types.h"
#include "sys/errno.h"
#include "rico.h"
#include "sd.h"

extern int	printf(), dlm_loadname();
extern void	sd_hostinit();	/* the host's static controllers */
extern char	*sd_modnames[];
extern struct vnode *rootdir;

static struct {
	int	(*o)();
	bool	(*f)();
	uint	c;
	char	*a;
	char	*name;
} q[SDCARDS];

static int	n, ndrv;

static void
hostinit()
{
	static int inited;

	if (!inited) {
		inited = 1;
		sd_hostinit();
	}
}

/*
 * open(c) runs at each sdopen of the card, queue(c, cp) takes each
 * request; a nonzero open result fails the open.
 */
int
sd_attach(open, queue, c, a, name)
	int (*open)();
	bool (*queue)();
	uint c;
	char *a, *name;
{
	int i;

	hostinit();
	for (i = 0; i < n && (q[i].f || q[i].a != a); i++)
		;
	if (i == n) {
		if (n == SDCARDS) {
			printf("sd: no card for %s\n", name);
			return ENOSPC;
		}
		for (i = n++; i > 0 && q[i - 1].a > a; i--)
			q[i] = q[i - 1];
	}
	if (queue)
		ndrv++;
	q[i].o = open;
	q[i].f = queue;
	q[i].c = c;
	q[i].a = a;
	q[i].name = name;
	return 0;
}

int
sdopen(card)
uint card;
{
	static char tried[SDCARDS];
	static int told;
	int i;

	hostinit();
	/* the module path needs a root */
	if (card < SDCARDS && !tried[card] && rootdir &&
	    (card >= n || !q[card].f)) {
		tried[card] = 1;
		for (i = 0; sd_modnames[i] && (card >= n || !q[card].f); i++)
			(void)dlm_loadname(sd_modnames[i]);
	}
	if (ndrv == 0 && !told) {
		told = 1;
		printf("sd: no disk controller driver; is its boot module missing?\n");
	}
	if (card >= n || !q[card].f)
		return ENXIO;
	return q[card].o ? (*q[card].o)(q[card].c) : 0;
}

void
sdqueue(cp)
struct sdcom *cp;
{
	if (cp->card >= n || !q[cp->card].f) {
		cp->okay = FALSE;
		(*cp->intr)(cp);
	} else
		(*q[cp->card].f)(q[cp->card].c, cp);
}

char *
sdhardwarename(card)
uint card;
{
	return card < n ? q[card].name : "none";
}
