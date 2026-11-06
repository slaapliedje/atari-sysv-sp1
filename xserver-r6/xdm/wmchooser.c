/*
 * wmchooser - the window manager, picked on xdm's login screen: a strip of
 * buttons under the greeter, one for each window manager installed (the
 * list: /etc/X11/wms) and "Usual" for the one the user had last time.
 * Xsetup_0 starts it and Xstartup ends it. A click writes the name to FILE
 * ("Usual" removes it); the session (Xsession) finds it there, starts that
 * window manager and keeps it in the user's ~/.wm for the next time.
 *
 *   wmchooser -o FILE [-l LIST]
 *
 * LIST lines: name, program, the button's label (the rest of the line);
 * '#' starts a comment. Entries whose program is missing are left out, and
 * with fewer than two left there is nothing to choose: no window.
 *
 * Plain Xlib, drawn like ttpanel and atwconf. Clicks only: xdm holds the
 * keyboard while its greeter is up.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define MAX_WM	8
#define BTN_H	26
#define PAD	8
#define GAP	6

static struct {
	char	name[32];
	char	label[40];
	int	x, w;
} wm[MAX_WM + 1];		/* [0] is "Usual" */
static int	nwm, sel;
static const char *out;
static const char *title = "Window manager:";

enum { C_FACE, C_LIGHT, C_SHADOW, C_DARK, C_TEXT, C_BLUE, C_WHITE, C_N };
static const char *cname[C_N] = {
	"#a8b4c4", "#dde4ee", "#6c7888", "#3c4450", "#101418", "#3c5c8c", "#ffffff"
};
static unsigned long col[C_N];

static Display	*dpy;
static int	scr;
static Window	win;
static GC	gc;
static XFontStruct *font, *bold;
static int	win_w, win_h;

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

static void read_list(const char *path)
{
	FILE *f = fopen(path, "r");
	char ln[256], name[64], prog[200], *lab;
	int n;

	strcpy(wm[0].name, "");
	strcpy(wm[0].label, "Usual");
	nwm = 1;
	if (f == NULL)
		return;
	while (nwm <= MAX_WM && fgets(ln, sizeof ln, f) != NULL) {
		n = 0;
		if (sscanf(ln, "%63s %199s %n", name, prog, &n) < 2 || name[0] == '#')
			continue;
		if (access(prog, X_OK) != 0)
			continue;	/* not installed */
		lab = n ? ln + n : name;
		lab[strcspn(lab, "\r\n")] = '\0';
		if (*lab == '\0')
			lab = name;
		strncpy(wm[nwm].name, name, sizeof wm[nwm].name - 1);
		strncpy(wm[nwm].label, lab, sizeof wm[nwm].label - 1);
		nwm++;
	}
	fclose(f);
}

/* the name for the session, written whole (aside, then renamed over) */
static void pick(int i)
{
	char tmp[300];
	FILE *f;

	sel = i;
	if (i == 0) {
		unlink(out);
		return;
	}
	if (strlen(out) > sizeof tmp - 8)
		return;
	sprintf(tmp, "%s.new", out);
	if ((f = fopen(tmp, "w")) == NULL)
		return;
	fprintf(f, "%s\n", wm[i].name);
	fclose(f);
	chmod(tmp, 0644);
	if (rename(tmp, out) < 0)
		unlink(tmp);
}

static void layout(void)
{
	int i, x;

	x = PAD + text_w(bold, title) + PAD;
	for (i = 0; i < nwm; i++) {
		wm[i].w = text_w(font, wm[i].label) + 24;
		if (wm[i].w < 64)
			wm[i].w = 64;
		wm[i].x = x;
		x += wm[i].w + GAP;
	}
	win_w = x - GAP + PAD;
	win_h = BTN_H + 2 * PAD;
}

static void draw(void)
{
	int i, sunk, ty = PAD + (BTN_H - font->ascent - font->descent) / 2;

	rect(C_FACE, 0, 0, win_w, win_h);
	bevel(0, 0, win_w, win_h, 0);
	text(bold, C_TEXT, PAD, PAD + (BTN_H - bold->ascent - bold->descent) / 2, title);
	for (i = 0; i < nwm; i++) {
		sunk = i == sel;
		rect(sunk ? C_LIGHT : C_FACE, wm[i].x, PAD, wm[i].w, BTN_H);
		bevel(wm[i].x, PAD, wm[i].w, BTN_H, sunk);
		text(font, sunk ? C_BLUE : C_TEXT,
		     wm[i].x + (wm[i].w - text_w(font, wm[i].label)) / 2 + sunk,
		     ty + sunk, wm[i].label);
	}
	XFlush(dpy);
}

static int io_error(Display *d)
{
	exit(0);	/* the greeter's server went away */
	return 0;
}

static void usage(void)
{
	fprintf(stderr, "usage: wmchooser -o FILE [-l LIST]   (LIST: /etc/X11/wms)\n");
	exit(2);
}

int main(int argc, char **argv)
{
	const char *list = "/etc/X11/wms";
	XSetWindowAttributes wa;
	XColor xc;
	Colormap cm;
	XEvent ev;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			out = argv[++i];
		else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
			list = argv[++i];
		else
			usage();
	}
	if (out == NULL)
		usage();
	read_list(list);
	if (nwm < 3)
		return 0;	/* one window manager or none: nothing to pick */
	unlink(out);		/* nothing picked yet: the usual one */

	if ((dpy = XOpenDisplay(NULL)) == NULL) {
		fprintf(stderr, "wmchooser: cannot open the display\n");
		return 1;
	}
	XSetIOErrorHandler(io_error);
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
		return 1;
	if ((bold = XLoadQueryFont(dpy, "-*-helvetica-bold-r-normal--12-*")) == NULL &&
	    (bold = XLoadQueryFont(dpy, "7x13bold")) == NULL)
		bold = font;
	layout();

	/* under the greeter, which xdm centres on the screen */
	wa.override_redirect = True;
	wa.background_pixel = col[C_FACE];
	wa.event_mask = ExposureMask | ButtonPressMask;
	win = XCreateWindow(dpy, RootWindow(dpy, scr), (DisplayWidth(dpy, scr) - win_w) / 2,
			    DisplayHeight(dpy, scr) - win_h - DisplayHeight(dpy, scr) / 8,
			    win_w, win_h, 0, CopyFromParent, InputOutput, CopyFromParent,
			    CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	XStoreName(dpy, win, "wmchooser");
	gc = XCreateGC(dpy, win, 0, NULL);
	XMapRaised(dpy, win);

	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.count == 0)
			draw();
		else if (ev.type == ButtonPress)
			for (i = 0; i < nwm; i++)
				if (ev.xbutton.x >= wm[i].x && ev.xbutton.x < wm[i].x + wm[i].w &&
				    ev.xbutton.y >= PAD && ev.xbutton.y < PAD + BTN_H) {
					pick(i);
					draw();
					break;
				}
	}
}
