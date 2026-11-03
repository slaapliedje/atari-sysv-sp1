/*
 * atwconf - the ATW800/2's screen mode, in a dialog: the modes Xatw
 * offers (Xatw -listmodes), greyed out where the card has too little
 * video memory, with how to set its jumpers for 4 MB, and Apply, which
 * writes the mode into xdm's Xservers and restarts X.
 *
 *   atwconf                    the dialog, on $DISPLAY
 *   atwconf -l                 the modes as text: which fit, the one set
 *   atwconf -m WxH [-d 8|32] [-y]
 *                              set that mode and restart X (asks first,
 *                              unless -y)
 *   atwconf -back              the mode before the last change, and
 *                              restart X
 *   atwconf -confirm           for Xsetup_0: after a change, keep the new
 *                              mode or go back - by itself after 20
 *                              seconds, for a monitor that shows nothing
 *                              in the new mode
 *
 * The card's memory comes from the running Xatw (_ATW_FRAMEBUFFER on the
 * root window), else from its last start in xdm-errors ("atw: ..., 2 MB").
 * Changing the mode needs root: Xservers is root's, and X restarts by
 * stopping xdm, which init's xd line starts again.
 *
 * Plain Xlib, drawn like ttpanel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

/* not declared by ASV's headers in this mode; popen's pointer must not
 * be taken for an int (m68k returns them in different registers) */
extern FILE *popen(const char *, const char *);
extern int pclose(FILE *);
extern int kill(pid_t, int);
extern int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

#define XATW		"/usr/x11r6/bin/Xatw"
#define XDMDIR		"/usr/x11r6/lib/X11/xdm"
#define XSERVERS	XDMDIR "/Xservers"
#define XSERVERS_NEW	XDMDIR "/Xservers.new"
#define XSERVERS_PREV	XDMDIR "/Xservers.prev"
#define PENDING		XDMDIR "/atwconf.pending"
#define XDMPID		XDMDIR "/xdm-pid"
#define XDMERRORS	XDMDIR "/xdm-errors"
#define CONFIRM_SECS	20

#define MAXMODE		16
#define MAXFILE		8192

typedef struct {
	int	w, h, depth, hz;
	int	needs4;			/* Xatw: more than a 2 MB card holds */
} Mode;

static Mode	mode[MAXMODE];
static int	nmode;
static int	card_mb;		/* 0: not known */
static char	card_id[80];		/* "V0205 build ...", from xdm-errors */
static int	set_w = 640, set_h = 480, set_depth = 8;	/* Xservers */
static int	run_w, run_h, run_depth;	/* the running server, if Xatw */

/* --- the modes, the card, the Xservers line --------------------------- */

static int load_modes(void)
{
	FILE *p = popen(XATW " -listmodes 2>&1", "r");
	char line[200];
	Mode m;

	if (p == NULL)
		return 0;
	while (fgets(line, sizeof line, p) != NULL && nmode < MAXMODE)
		if (sscanf(line, " -mode %dx%d -depth %d (%d Hz)", &m.w, &m.h, &m.depth, &m.hz) == 4) {
			m.needs4 = strstr(line, "needs 4 MB") != NULL;
			mode[nmode++] = m;
		}
	pclose(p);
	return nmode;
}

static int fits(const Mode *m)
{
	return !m->needs4 || card_mb == 0 || card_mb >= 4;
}

/* the last start of Xatw in xdm-errors: "atw: V0205 build ... cpm 202, 2 MB" */
static void card_from_log(void)
{
	FILE *f = fopen(XDMERRORS, "r");
	char line[200], *c;
	int mb;

	if (f == NULL)
		return;
	while (fgets(line, sizeof line, f) != NULL) {
		if (strncmp(line, "atw: ", 5) != 0 || (c = strrchr(line, ',')) == NULL ||
		    sscanf(c, ", %d MB", &mb) != 1)
			continue;
		card_mb = mb;
		*c = '\0';
		strncpy(card_id, line + 5, sizeof card_id - 1);
		card_id[sizeof card_id - 1] = '\0';
	}
	fclose(f);
}

/* the running server's own word: the card's size and its mode */
static void card_from_display(Display *d)
{
	Atom a = XInternAtom(d, "_ATW_FRAMEBUFFER", True), type;
	int format;
	unsigned long n, after;
	unsigned char *data = NULL;
	long *v;

	if (a == None || XGetWindowProperty(d, DefaultRootWindow(d), a, 0, 8, False,
					    XA_INTEGER, &type, &format, &n, &after,
					    &data) != Success || data == NULL)
		return;
	v = (long *)data;
	if (format == 32 && n >= 7) {
		card_mb = (int)(v[1] >> 20);
		run_w = (int)v[4];
		run_h = (int)v[5];
		run_depth = (int)v[6];
	}
	XFree(data);
}

static int read_file(const char *path, char *buf)
{
	int fd = open(path, O_RDONLY), n;

	if (fd < 0)
		return -1;
	n = read(fd, buf, MAXFILE - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	return n;
}

static int write_file(const char *path, const char *buf)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644), n = (int)strlen(buf);

	if (fd < 0)
		return -1;
	if (write(fd, buf, n) != n) {
		close(fd);
		return -1;
	}
	return close(fd);
}

/* the Xatw line: the first that is not a comment and names Xatw */
static char *xatw_line(char *buf, char **end)
{
	char *l = buf, *e;

	for (; *l; l = *e ? e + 1 : e) {
		e = strchr(l, '\n');
		if (e == NULL)
			e = l + strlen(l);
		while (*l == ' ' || *l == '\t')
			l++;
		if (*l != '#' && *l != '!') {
			char c = *e;

			*e = '\0';
			if (strstr(l, "Xatw") != NULL) {
				*e = c;
				*end = e;
				return l;
			}
			*e = c;
		}
	}
	return NULL;
}

static void read_set_mode(void)
{
	char buf[MAXFILE], *l, *e, *t;

	if (read_file(XSERVERS, buf) < 0 || (l = xatw_line(buf, &e)) == NULL)
		return;
	*e = '\0';
	for (t = strtok(l, " \t"); t != NULL; t = strtok(NULL, " \t"))
		if (!strcmp(t, "-mode") && (t = strtok(NULL, " \t")) != NULL)
			sscanf(t, "%dx%d", &set_w, &set_h);
		else if (!strcmp(t, "-depth") && (t = strtok(NULL, " \t")) != NULL)
			set_depth = atoi(t);
}

/* the Xatw line again, with this -mode and -depth; the rest kept */
static int write_mode(const Mode *m, char *err)
{
	char buf[MAXFILE], out[MAXFILE + 64], *l, *e, *t, *o;
	char line[1024];
	int skip = 0;

	if (read_file(XSERVERS, buf) < 0) {
		sprintf(err, "cannot read %s", XSERVERS);
		return -1;
	}
	if ((l = xatw_line(buf, &e)) == NULL || e - l >= (int)sizeof line) {
		sprintf(err, "no Xatw line in %s", XSERVERS);
		return -1;
	}
	memcpy(line, l, e - l);
	line[e - l] = '\0';
	memcpy(out, buf, l - buf);
	o = out + (l - buf);
	for (t = strtok(line, " \t"); t != NULL; t = strtok(NULL, " \t")) {
		if (skip) {
			skip = 0;
			continue;
		}
		if (!strcmp(t, "-mode") || !strcmp(t, "-depth")) {
			skip = 1;
			continue;
		}
		o += sprintf(o, "%s%s", o == out + (l - buf) ? "" : " ", t);
	}
	o += sprintf(o, " -mode %dx%d", m->w, m->h);
	if (m->depth != 8)
		o += sprintf(o, " -depth %d", m->depth);
	strcpy(o, e);
	if (write_file(XSERVERS_NEW, out) < 0) {
		sprintf(err, "cannot write %s", XSERVERS_NEW);
		return -1;
	}
	if (write_file(XSERVERS_PREV, buf) < 0 || rename(XSERVERS_NEW, XSERVERS) < 0) {
		unlink(XSERVERS_NEW);
		sprintf(err, "cannot replace %s", XSERVERS);
		return -1;
	}
	return 0;
}

/* xdm stops its server and its sessions; init's xd line starts it again */
static int restart_x(char *err)
{
	char buf[64];
	long pid;

	sync();
	if (read_file(XDMPID, buf) < 0 || (pid = atol(buf)) <= 1) {
		sprintf(err, "no xdm pid in %s: restart X by hand", XDMPID);
		return -1;
	}
	if (kill((pid_t)pid, SIGTERM) < 0) {
		sprintf(err, "cannot stop xdm (pid %ld): restart X by hand", pid);
		return -1;
	}
	return 0;
}

static const char *colours(int depth)
{
	return depth == 8 ? "256 colours" : "true colour (24-bit)";
}

/* change to this mode: Xservers, the note for -confirm, and X restarts */
static int apply(const Mode *m, char *err)
{
	char note[64];

	if (getuid() != 0) {
		strcpy(err, "only root can change the mode");
		return -1;
	}
	if (write_mode(m, err) < 0)
		return -1;
	sprintf(note, "%d %d %d\n", m->w, m->h, m->depth);
	write_file(PENDING, note);
	return restart_x(err);
}

static int go_back(char *err)
{
	char buf[MAXFILE];

	unlink(PENDING);
	if (read_file(XSERVERS_PREV, buf) < 0) {
		sprintf(err, "no %s: nothing to go back to", XSERVERS_PREV);
		return -1;
	}
	if (write_file(XSERVERS_NEW, buf) < 0 || rename(XSERVERS_NEW, XSERVERS) < 0) {
		sprintf(err, "cannot replace %s", XSERVERS);
		return -1;
	}
	return restart_x(err);
}

/* --- drawing, as ttpanel ------------------------------------------------ */

enum { C_FACE, C_LIGHT, C_SHADOW, C_DARK, C_TEXT, C_BLUE, C_WHITE, C_GREY, C_N };
static const char *cname[C_N] = {
	"#a8b4c4", "#dde4ee", "#6c7888", "#3c4450", "#101418", "#3c5c8c",
	"#ffffff", "#5c6878"
};
static unsigned long col[C_N];

static Display	*dpy;
static int	scr;
static Window	win;
static GC	gc;
static XFontStruct *font, *bold;

static void fg(int c) { XSetForeground(dpy, gc, col[c]); }
static void rect(int c, int x, int y, int w, int h) { fg(c); XFillRectangle(dpy, win, gc, x, y, w, h); }
static void line(int c, int x0, int y0, int x1, int y1) { fg(c); XDrawLine(dpy, win, gc, x0, y0, x1, y1); }

static void bevel(int x, int y, int w, int h, int sunk)
{
	int hi = sunk ? C_SHADOW : C_LIGHT, lo = sunk ? C_LIGHT : C_SHADOW;

	line(hi, x, y, x + w - 1, y);
	line(hi, x, y + 1, x + w - 2, y + 1);
	line(hi, x, y, x, y + h - 1);
	line(hi, x + 1, y, x + 1, y + h - 2);
	line(lo, x + 1, y + h - 1, x + w - 1, y + h - 1);
	line(lo, x + 2, y + h - 2, x + w - 1, y + h - 2);
	line(lo, x + w - 1, y + 1, x + w - 1, y + h - 1);
	line(lo, x + w - 2, y + 2, x + w - 2, y + h - 1);
}

static void text(XFontStruct *f, int c, int x, int y, const char *s)
{
	XSetFont(dpy, gc, f->fid);
	fg(c);
	XDrawString(dpy, win, gc, x, y + f->ascent, s, (int)strlen(s));
}

static int text_w(XFontStruct *f, const char *s)
{
	return XTextWidth(f, s, (int)strlen(s));
}

/* word-wrapped into w pixels; returns the lines drawn (draw 0: count) */
static int para(int c, int x, int y, int w, const char *s, int draw)
{
	char ln[256];
	int n = 0, lh = font->ascent + font->descent + 2;

	while (*s) {
		int len = 0, fit = 0, over = 0;

		while (s[len] && s[len] != '\n') {
			len++;
			if (XTextWidth(font, s, len) > w) {
				over = 1;
				break;
			}
			if (s[len] == ' ' || s[len] == '\0' || s[len] == '\n')
				fit = len;
		}
		if (!over)
			fit = len;		/* the rest of the line fits */
		else if (!fit)
			fit = len > 1 ? len - 1 : 1;	/* one long word: cut */
		if (fit > (int)sizeof ln - 1)
			fit = sizeof ln - 1;
		memcpy(ln, s, fit);
		ln[fit] = '\0';
		if (draw)
			text(font, c, x, y + n * lh, ln);
		n++;
		s += fit;
		while (*s == ' ')
			s++;
		if (*s == '\n')
			s++;
	}
	return n;
}

static int open_x(void)
{
	XColor xc;
	Colormap cm;
	int i;

	if ((dpy = XOpenDisplay(NULL)) == NULL)
		return 0;
	scr = DefaultScreen(dpy);
	cm = DefaultColormap(dpy, scr);
	for (i = 0; i < C_N; i++) {
		col[i] = (i == C_WHITE || i == C_LIGHT || i == C_FACE) ? WhitePixel(dpy, scr) : BlackPixel(dpy, scr);
		if (XParseColor(dpy, cm, cname[i], &xc) && XAllocColor(dpy, cm, &xc))
			col[i] = xc.pixel;
	}
	if ((font = XLoadQueryFont(dpy, "-*-helvetica-medium-r-normal--12-*")) == NULL &&
	    (font = XLoadQueryFont(dpy, "6x13")) == NULL &&
	    (font = XLoadQueryFont(dpy, "fixed")) == NULL)
		return 0;
	if ((bold = XLoadQueryFont(dpy, "-*-helvetica-bold-r-normal--14-*")) == NULL &&
	    (bold = XLoadQueryFont(dpy, "7x13bold")) == NULL)
		bold = font;
	return 1;
}

static int io_error(Display *d)
{
	exit(0);	/* X went away: after Apply, that is the restart */
	return 0;
}

static Window make_window(int w, int h, int override, const char *title)
{
	XSetWindowAttributes wa;
	XSizeHints sh;
	Window r = RootWindow(dpy, scr);

	wa.override_redirect = override ? True : False;
	wa.background_pixel = col[C_FACE];
	wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
			KeyPressMask | StructureNotifyMask;
	win = XCreateWindow(dpy, r, (DisplayWidth(dpy, scr) - w) / 2,
			    (DisplayHeight(dpy, scr) - h) / 2, w, h, 0,
			    CopyFromParent, InputOutput, CopyFromParent,
			    CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	sh.flags = PPosition | PSize | PMinSize | PMaxSize;
	sh.x = (DisplayWidth(dpy, scr) - w) / 2;
	sh.y = (DisplayHeight(dpy, scr) - h) / 2;
	sh.width = sh.min_width = sh.max_width = w;
	sh.height = sh.min_height = sh.max_height = h;
	XSetWMNormalHints(dpy, win, &sh);
	XStoreName(dpy, win, title);
	gc = XCreateGC(dpy, win, 0, NULL);
	XMapRaised(dpy, win);
	return win;
}

/* --- the buttons -------------------------------------------------------- */

#define BTN_H	26

typedef struct {
	int	x, y, w;
	const char *label;
	int	on;		/* can be pressed */
} Btn;

static void draw_btn(const Btn *b, int sunk)
{
	int tw = text_w(font, b->label);

	rect(C_FACE, b->x, b->y, b->w, BTN_H);
	bevel(b->x, b->y, b->w, BTN_H, sunk);
	text(font, b->on ? C_TEXT : C_GREY, b->x + (b->w - tw) / 2 + sunk,
	     b->y + (BTN_H - font->ascent - font->descent) / 2 + sunk, b->label);
}

static int in_btn(const Btn *b, int x, int y)
{
	return b->on && x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + BTN_H;
}

/* --- the dialog --------------------------------------------------------- */

#define DLG_W	520
#define PAD	14
#define ROW_H	24
#define COL_RES	(PAD + 26)
#define COL_COL	(PAD + 130)
#define COL_HZ	(PAD + 300)
#define COL_NOTE (PAD + 360)

static int	sel = -1;		/* the chosen row */
static int	confirming;		/* Apply pressed: "X restarts now" */
static char	message[256];
static int	rows_y, note_y, msg_y, btn_y, dlg_h;
static Btn	b_apply, b_close, b_restart, b_cancel;

static const char *jumper_note =
	"The greyed modes need the card's full 4 MB of video memory; it is set "
	"up for 2 MB. To set it up for 4 MB: shut the system down and switch "
	"the TT off, then on the ATW800/2 close both of its address jumpers, "
	"A0 and A1 (a jumper on each; CPLD firmware 2 or later). Switch the TT "
	"on again: the card is then at 0xFEA00000 with 4 MB, and these modes "
	"can be chosen.";

static int any_greyed(void)
{
	int i;

	for (i = 0; i < nmode; i++)
		if (!fits(&mode[i]))
			return 1;
	return 0;
}

static int is_set(const Mode *m)
{
	return m->w == set_w && m->h == set_h && m->depth == set_depth;
}

static void layout(void)
{
	int lh = font->ascent + font->descent + 2;

	rows_y = PAD + bold->ascent + bold->descent + 8 + 2 * lh + 10;
	note_y = rows_y + nmode * ROW_H + 10;
	msg_y = note_y + (any_greyed() ? para(C_TEXT, 0, 0, DLG_W - 2 * PAD - 16, jumper_note, 0) * lh + 24 : 0);
	btn_y = msg_y + 3 * lh + 8;
	dlg_h = btn_y + BTN_H + PAD;
	b_close.w = 80;
	b_close.x = DLG_W - PAD - b_close.w;
	b_apply.w = text_w(font, "Apply and restart X") + 28;
	b_apply.x = b_close.x - 10 - b_apply.w;
	b_cancel = b_close;
	b_restart.w = text_w(font, "Restart X now") + 28;
	b_restart.x = b_cancel.x - 10 - b_restart.w;
	b_apply.y = b_close.y = b_restart.y = b_cancel.y = btn_y;
	b_apply.label = "Apply and restart X";
	b_close.label = "Close";
	b_restart.label = "Restart X now";
	b_cancel.label = "Cancel";
}

static void draw_row(int i)
{
	const Mode *m = &mode[i];
	int y = rows_y + i * ROW_H, ok = fits(m), c = ok ? C_TEXT : C_GREY;
	char s[64];

	rect(i == sel ? C_LIGHT : C_FACE, PAD, y, DLG_W - 2 * PAD, ROW_H);
	/* the radio box */
	rect(ok ? C_WHITE : C_FACE, PAD + 4, y + 5, 14, 14);
	bevel(PAD + 4, y + 5, 14, 14, 1);
	if (i == sel)
		rect(C_BLUE, PAD + 8, y + 9, 6, 6);
	y += (ROW_H - font->ascent - font->descent) / 2;
	sprintf(s, "%d x %d", m->w, m->h);
	text(font, c, COL_RES, y, s);
	text(font, c, COL_COL, y, colours(m->depth));
	sprintf(s, "%d Hz", m->hz);
	text(font, c, COL_HZ, y, s);
	if (!ok)
		text(font, C_GREY, COL_NOTE, y, "needs 4 MB");
	else if (is_set(m))
		text(font, C_BLUE, COL_NOTE, y, "set now");
}

static void draw_dlg(void)
{
	char s[160];
	int lh = font->ascent + font->descent + 2, y = PAD, i, n;

	rect(C_FACE, 0, 0, DLG_W, dlg_h);
	text(bold, C_TEXT, PAD, y, "ATW800/2 screen mode");
	y += bold->ascent + bold->descent + 8;
	if (card_mb)
		sprintf(s, "Card: %s%s%d MB of video memory", card_id, card_id[0] ? ", " : "", card_mb);
	else
		strcpy(s, "Card: its video memory is not known (no Xatw has run)");
	text(font, C_TEXT, PAD, y, s);
	y += lh;
	if (run_w)
		sprintf(s, "This screen: %d x %d, %s", run_w, run_h, colours(run_depth));
	else
		sprintf(s, "X starts in: %d x %d, %s", set_w, set_h, colours(set_depth));
	text(font, C_TEXT, PAD, y, s);

	bevel(PAD - 2, rows_y - 2, DLG_W - 2 * PAD + 4, nmode * ROW_H + 4, 1);
	for (i = 0; i < nmode; i++)
		draw_row(i);

	if (any_greyed()) {
		n = para(C_TEXT, 0, 0, DLG_W - 2 * PAD - 16, jumper_note, 0);
		bevel(PAD, note_y, DLG_W - 2 * PAD, n * lh + 14, 0);
		para(C_TEXT, PAD + 8, note_y + 7, DLG_W - 2 * PAD - 16, jumper_note, 1);
	}
	if (confirming)
		para(C_TEXT, PAD, msg_y, DLG_W - 2 * PAD,
		     "X restarts now: every program on this screen is closed, and you "
		     "log in again. If the new mode shows nothing, wait 20 seconds.", 1);
	else if (message[0])
		para(C_TEXT, PAD, msg_y, DLG_W - 2 * PAD, message, 1);
	if (confirming) {
		draw_btn(&b_restart, 0);
		draw_btn(&b_cancel, 0);
	} else {
		draw_btn(&b_apply, 0);
		draw_btn(&b_close, 0);
	}
}

static void update_apply(void)
{
	b_apply.on = sel >= 0 && fits(&mode[sel]) && !is_set(&mode[sel]) && getuid() == 0;
	b_close.on = b_restart.on = b_cancel.on = 1;
	if (getuid() != 0)
		strcpy(message, "Only root can change the mode.");
	else if (sel >= 0 && is_set(&mode[sel]))
		strcpy(message, "This is the mode X starts in now.");
	else
		message[0] = '\0';
}

static void select_row(int i)
{
	if (i < 0 || i >= nmode || !fits(&mode[i]))
		return;
	sel = i;
	confirming = 0;
	update_apply();
	draw_dlg();
}

static void move_sel(int d)
{
	int i;

	for (i = sel + d; i >= 0 && i < nmode; i += d)
		if (fits(&mode[i])) {
			select_row(i);
			return;
		}
}

static void do_restart(void)
{
	char err[200];

	if (apply(&mode[sel], err) < 0) {
		confirming = 0;
		strcpy(message, err);
		b_apply.on = 0;
		draw_dlg();
		return;
	}
	confirming = 0;
	strcpy(message, "Restarting X ...");
	draw_dlg();
	XFlush(dpy);
	sleep(10);	/* xdm takes the server down; we go with it */
	exit(0);
}

static int dialog(void)
{
	XEvent ev;
	Btn *down = NULL;
	int i;

	if (!open_x()) {
		fprintf(stderr, "atwconf: cannot open the display (atwconf -l works without X)\n");
		return 1;
	}
	XSetIOErrorHandler(io_error);
	card_from_log();		/* the card's id, and its size if no Xatw is on show */
	card_from_display(dpy);		/* the size from the server itself */
	for (i = 0; i < nmode; i++)
		if (is_set(&mode[i]) && fits(&mode[i]))
			sel = i;
	layout();
	update_apply();
	make_window(DLG_W, dlg_h, 0, "ATW800/2 screen mode");
	for (;;) {
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case MapNotify:		/* a dialog: the keys are ours */
			XSetInputFocus(dpy, win, RevertToPointerRoot, CurrentTime);
			break;
		case Expose:
			if (ev.xexpose.count == 0)
				draw_dlg();
			break;
		case ButtonPress:
			down = NULL;
			if (confirming) {
				if (in_btn(&b_restart, ev.xbutton.x, ev.xbutton.y))
					down = &b_restart;
				else if (in_btn(&b_cancel, ev.xbutton.x, ev.xbutton.y))
					down = &b_cancel;
			} else if (in_btn(&b_apply, ev.xbutton.x, ev.xbutton.y))
				down = &b_apply;
			else if (in_btn(&b_close, ev.xbutton.x, ev.xbutton.y))
				down = &b_close;
			else if (ev.xbutton.y >= rows_y && ev.xbutton.y < rows_y + nmode * ROW_H)
				select_row((ev.xbutton.y - rows_y) / ROW_H);
			if (down)
				draw_btn(down, 1);
			break;
		case ButtonRelease:
			if (down == NULL)
				break;
			draw_btn(down, 0);
			if (in_btn(down, ev.xbutton.x, ev.xbutton.y)) {
				if (down == &b_close)
					return 0;
				if (down == &b_apply) {
					confirming = 1;
					draw_dlg();
				} else if (down == &b_cancel) {
					confirming = 0;
					update_apply();
					draw_dlg();
				} else if (down == &b_restart)
					do_restart();
			}
			down = NULL;
			break;
		case KeyPress: {
			KeySym k = XLookupKeysym(&ev.xkey, 0);

			if (k == XK_Up)
				move_sel(-1);
			else if (k == XK_Down)
				move_sel(1);
			else if (k == XK_Return && confirming)
				do_restart();
			else if (k == XK_Return && b_apply.on) {
				confirming = 1;
				draw_dlg();
			} else if (k == XK_Escape && confirming) {
				confirming = 0;
				update_apply();
				draw_dlg();
			} else if (k == XK_Escape)
				return 0;
			break;
		}
		}
	}
}

/* --- -confirm: after a change, keep it or go back ------------------------ */

/* Xsetup_0 runs this while xdm holds the keyboard (R6.3's greeter grabs it
 * before Xsetup runs): Keep is clicked; the keys work only without xdm. */

static int confirm(void)
{
	char buf[64], s[160], err[200];
	int w, h, depth, left = CONFIRM_SECS, lh, H;
	time_t end;
	Btn keep, back, *down = NULL;
	XEvent ev;

	if (read_file(PENDING, buf) < 0)
		return 0;		/* no change to confirm */
	if (sscanf(buf, "%d %d %d", &w, &h, &depth) != 3 || !open_x()) {
		unlink(PENDING);
		return 1;
	}
	XSetIOErrorHandler(io_error);
	lh = font->ascent + font->descent + 2;
	H = PAD + bold->ascent + bold->descent + 8 + 3 * lh + 12 + BTN_H + PAD;
	make_window(DLG_W - 60, H, 1, "atwconf");
	keep.w = back.w = 90;
	back.x = DLG_W - 60 - PAD - back.w;
	keep.x = back.x - 10 - keep.w;
	keep.y = back.y = H - PAD - BTN_H;
	keep.label = "Keep";
	back.label = "Go back";
	keep.on = back.on = 1;
	sprintf(s, "The screen is now %d x %d, %s.", w, h, colours(depth));
	end = time(NULL) + CONFIRM_SECS;
	for (;;) {
		while (XPending(dpy) == 0) {
			fd_set fds;
			struct timeval tv;

			left = (int)(end - time(NULL));
			if (left <= 0) {
				XCloseDisplay(dpy);
				if (go_back(err) < 0) {
					fprintf(stderr, "atwconf: %s\n", err);
					return 1;
				}
				return 0;
			}
			{
				char c[80];
				int y = PAD + bold->ascent + bold->descent + 8 + 2 * lh;

				rect(C_FACE, PAD, y, DLG_W - 60 - 2 * PAD, lh);
				sprintf(c, "Click Keep, or the mode before comes back in %d s.", left);
				text(font, C_TEXT, PAD, y, c);
				XFlush(dpy);
			}
			FD_ZERO(&fds);
			FD_SET(ConnectionNumber(dpy), &fds);
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			select(ConnectionNumber(dpy) + 1, &fds, NULL, NULL, &tv);
		}
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case MapNotify:
			XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
			break;
		case Expose:
			if (ev.xexpose.count)
				break;
			rect(C_FACE, 0, 0, DLG_W - 60, H);
			bevel(0, 0, DLG_W - 60, H, 0);
			text(bold, C_TEXT, PAD, PAD, "Keep this screen mode?");
			text(font, C_TEXT, PAD, PAD + bold->ascent + bold->descent + 8, s);
			draw_btn(&keep, 0);
			draw_btn(&back, 0);
			break;
		case ButtonPress:
			down = in_btn(&keep, ev.xbutton.x, ev.xbutton.y) ? &keep :
			       in_btn(&back, ev.xbutton.x, ev.xbutton.y) ? &back : NULL;
			if (down)
				draw_btn(down, 1);
			break;
		case ButtonRelease:
			if (down == NULL)
				break;
			draw_btn(down, 0);
			if (in_btn(down, ev.xbutton.x, ev.xbutton.y)) {
				if (down == &keep) {
					unlink(PENDING);
					return 0;
				}
				XCloseDisplay(dpy);
				return go_back(err) < 0;
			}
			down = NULL;
			break;
		case KeyPress: {
			KeySym k = XLookupKeysym(&ev.xkey, 0);

			if (k == XK_Return) {
				unlink(PENDING);
				return 0;
			}
			if (k == XK_Escape) {
				XCloseDisplay(dpy);
				return go_back(err) < 0;
			}
			break;
		}
		}
	}
}

/* --- the command line ---------------------------------------------------- */

static void list(void)
{
	int i;

	if (card_mb)
		printf("card: %s%s%d MB of video memory\n", card_id, card_id[0] ? ", " : "", card_mb);
	else
		printf("card: video memory not known (no Xatw has run)\n");
	for (i = 0; i < nmode; i++) {
		const Mode *m = &mode[i];

		printf("  %c %4d x %-4d  %-21s %d Hz  %s\n", is_set(m) ? '*' : ' ',
		       m->w, m->h, colours(m->depth), m->hz,
		       !fits(m) ? "needs 4 MB (jumpers A0 and A1 closed)" :
		       m->needs4 && card_mb == 0 ? "needs a card set up for 4 MB" : "");
	}
	printf("  (* = the mode X starts in)\n");
}

static int set_cmd(int w, int h, int depth, int yes)
{
	char err[200], ans[16];
	int i;

	for (i = 0; i < nmode; i++)
		if (mode[i].w == w && mode[i].h == h && mode[i].depth == depth)
			break;
	if (i == nmode) {
		fprintf(stderr, "atwconf: Xatw has no %dx%d at depth %d (atwconf -l lists the modes)\n", w, h, depth);
		return 1;
	}
	if (!fits(&mode[i])) {
		fprintf(stderr, "atwconf: %dx%d at depth %d needs 4 MB, and the card is set up for %d MB\n",
			w, h, depth, card_mb);
		return 1;
	}
	if (mode[i].needs4 && card_mb == 0)
		printf("The card's memory is not known (no Xatw has run). Set up for 2 MB, it\n"
		       "cannot show this mode and X does not start: atwconf -back undoes it.\n");
	if (!yes) {
		printf("X restarts in %d x %d, %s: every program on the screen is closed. Go on? [y/n] ",
		       w, h, colours(depth));
		fflush(stdout);
		if (fgets(ans, sizeof ans, stdin) == NULL || (ans[0] != 'y' && ans[0] != 'Y'))
			return 1;
	}
	if (apply(&mode[i], err) < 0) {
		fprintf(stderr, "atwconf: %s\n", err);
		return 1;
	}
	printf("X restarts in %d x %d, %s. On the screen: click Keep, or it goes back by itself in %d s.\n",
	       w, h, colours(depth), CONFIRM_SECS);
	return 0;
}

static void usage(void)
{
	fprintf(stderr, "usage: atwconf                the dialog\n"
			"       atwconf -l             list the modes\n"
			"       atwconf -m WxH [-d 8|32] [-y]   set a mode, restart X\n"
			"       atwconf -back          the mode before the last change\n"
			"       atwconf -confirm       (Xsetup_0) keep a new mode or go back\n");
	exit(2);
}

int main(int argc, char **argv)
{
	int w = 0, h = 0, depth = 8, yes = 0, i;
	char err[200];

	signal(SIGPIPE, SIG_IGN);
	if (argc > 1 && !strcmp(argv[1], "-confirm"))
		return confirm();
	if (argc > 1 && !strcmp(argv[1], "-back")) {
		if (getuid() != 0) {
			fprintf(stderr, "atwconf: only root can change the mode\n");
			return 1;
		}
		if (go_back(err) < 0) {
			fprintf(stderr, "atwconf: %s\n", err);
			return 1;
		}
		return 0;
	}
	if (!load_modes()) {
		fprintf(stderr, "atwconf: %s -listmodes gave no modes\n", XATW);
		return 1;
	}
	read_set_mode();
	if (argc > 1 && !strcmp(argv[1], "-l")) {
		card_from_log();
		list();
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "-m")) {
		for (i = 1; i < argc; i++)
			if (!strcmp(argv[i], "-m") && i + 1 < argc) {
				if (sscanf(argv[++i], "%dx%d", &w, &h) != 2)
					usage();
			} else if (!strcmp(argv[i], "-d") && i + 1 < argc)
				depth = atoi(argv[++i]);
			else if (!strcmp(argv[i], "-y"))
				yes = 1;
			else
				usage();
		card_from_log();
		return set_cmd(w, h, depth, yes);
	}
	if (argc > 1)
		usage();
	return dialog();
}
