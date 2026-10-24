/*
 * ttpanel - a front panel for the Atari System V desktop, in the manner of
 * CDE's and IRIX's: a bevelled bar along the bottom of the screen with a
 * button per program and a clock. A click starts the program.
 *
 * The buttons come from ~/.ttpanelrc, one per line:
 *
 *     Label : icon : command
 *
 * where icon is one of term, files, edit, calc, book, load, game, clock,
 * mail, paint, tool, and the command runs through /bin/sh. '#' starts a
 * comment. Without the file the panel offers a terminal, the file manager
 * (Atari's wish - not Tcl's: its desktop with wx2/wxd),
 * an editor, a calculator, the manuals, a load meter and OpenUA.
 *
 * With olvwm (as patched for Atari System V) the panel also has a
 * workspace switcher - a button per screen of the virtual desktop, read
 * from the root's _OLVWM_DESKTOP and chosen with an _OLVWM_GOTO message -
 * and a Log out button, which sends _OLVWM_EXIT: olvwm then asks to
 * confirm, as its Workspace menu's Exit does.
 *
 * The right button opens a menu on the panel: on a launcher, Edit...,
 * Add after..., Remove, Move left and Move right; elsewhere, Add... and
 * Reload. Edit and Add open a small dialog - the label, the command, an
 * icon to pick - and every change is written to ~/.ttpanelrc (comments
 * in a hand-written file do not survive that) and shown at once.
 *
 * Options: -top (at the top of the screen instead), -fn FONT.
 *
 * Plain Xlib, so it runs on the R6.3 libraries and draws in a handful of
 * colours; the icons are drawn, not bitmaps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#define BTN_W	64
#define BTN_H	58
#define PAD	5
#define MAXBTN	24

typedef struct {
	char	label[32];
	char	icon[16];
	char	cmd[256];
	int	x;
} Button;

static Display *dpy;
static int	scr;
static Window	win;
static GC	gc;
static XFontStruct *font, *bigfont;
static int	panel_w, panel_h;
static Button	btn[MAXBTN];
static int	nbtn;
static int	pressed = -1;
static int	clock_x;
static int	ws_x, ws_cols, ws_rows, ws_cur;	/* the switcher; ws_cols 0 = none */
static int	exit_x;
static Atom	a_desktop, a_goto, a_exit;
static int	sw, sh, top;			/* the screen, and -top */
static Window	draw_on;			/* rect, line, icon draw here */

/* the Indigo Magic / CDE greys and blues, and the icons' colours */
enum { C_FACE, C_LIGHT, C_SHADOW, C_DARK, C_TEXT, C_BLUE, C_WHITE,
       C_YELLOW, C_RED, C_GREEN, C_SCREEN, C_TAN, C_N };
static const char *cname[C_N] = {
	"#a8b4c4", "#dde4ee", "#6c7888", "#3c4450", "#101418", "#3c5c8c",
	"#ffffff", "#e8c860", "#c83c3c", "#4c9c50", "#1c3c2c", "#d8c49c"
};
static unsigned long col[C_N];

static const char *defaults =
	"Terminal : term  : xterm\n"
	"Files    : files : wish\n"
	"Editor   : edit  : textedit\n"
	"Calc     : calc  : xcalc\n"
	"Manuals  : book  : xman\n"
	"Load     : load  : xload\n"
	"OpenUA   : game  : uagame\n";

static void trim(char *s)
{
	char *p = s, *e;

	while (*p == ' ' || *p == '\t')
		p++;
	memmove(s, p, strlen(p) + 1);
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		*--e = 0;
}

static void add_line(char *line)
{
	char *a, *b;
	Button *bt;

	if (nbtn >= MAXBTN)
		return;
	trim(line);
	if (line[0] == 0 || line[0] == '#')
		return;
	a = strchr(line, ':');
	b = a ? strchr(a + 1, ':') : NULL;
	if (b == NULL)
		return;
	*a = *b = 0;
	bt = &btn[nbtn];
	strncpy(bt->label, line, sizeof bt->label - 1);
	strncpy(bt->icon, a + 1, sizeof bt->icon - 1);
	strncpy(bt->cmd, b + 1, sizeof bt->cmd - 1);
	trim(bt->label);
	trim(bt->icon);
	trim(bt->cmd);
	if (bt->cmd[0])
		nbtn++;
}

static void load_config(void)
{
	char path[512], line[512];
	const char *home = getenv("HOME");
	FILE *f;

	sprintf(path, "%s/.ttpanelrc", home ? home : "");
	f = fopen(path, "r");
	if (f != NULL) {
		while (fgets(line, sizeof line, f) != NULL)
			add_line(line);
		fclose(f);
	}
	if (nbtn == 0) {		/* no file, or nothing in it */
		const char *p = defaults, *q;

		while (*p) {
			q = strchr(p, '\n');
			if (q == NULL)
				q = p + strlen(p);
			if (q - p < (int)sizeof line) {
				memcpy(line, p, (size_t)(q - p));
				line[q - p] = 0;
				add_line(line);
			}
			p = *q ? q + 1 : q;
		}
	}
}

/* ~/.ttpanelrc from the buttons, after a change made on the panel */
static void save_config(void)
{
	char path[512];
	const char *home = getenv("HOME");
	FILE *f;
	int i;

	sprintf(path, "%s/.ttpanelrc", home ? home : "");
	if ((f = fopen(path, "w")) == NULL)
		return;
	fprintf(f, "# ttpanel's buttons, one per line:  Label : icon : command\n"
		   "# icons: term files edit calc book load game mail paint tool clock exit\n"
		   "# (written by the panel's right-button menu)\n");
	for (i = 0; i < nbtn; i++)
		fprintf(f, "%s : %s : %s\n", btn[i].label, btn[i].icon, btn[i].cmd);
	fclose(f);
}

/* run cmd through the shell, detached: the child's child is the program,
 * and the child exits at once (so nothing is left to reap) */
static void launch(const char *cmd)
{
	pid_t p = fork();

	if (p == 0) {
		setsid();
		if (fork() == 0) {
			close(ConnectionNumber(dpy));
			execl("/bin/sh", "sh", "-c", cmd, (char *)0);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		int st;
		waitpid(p, &st, 0);
	}
}

/* --- drawing -------------------------------------------------------------- */

static void fg(int c) { XSetForeground(dpy, gc, col[c]); }
static void rect(int c, int x, int y, int w, int h) { fg(c); XFillRectangle(dpy, draw_on, gc, x, y, w, h); }
static void line(int c, int x0, int y0, int x1, int y1) { fg(c); XDrawLine(dpy, draw_on, gc, x0, y0, x1, y1); }

/* a bevelled box: raised or sunk, 2 pixels */
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

/* the icons, in a 32x32 box at (x, y) */
static void icon(const char *name, int x, int y)
{
	int i;

	if (!strcmp(name, "term")) {
		rect(C_DARK, x + 2, y + 3, 28, 21);
		rect(C_SCREEN, x + 4, y + 5, 24, 17);
		line(C_GREEN, x + 6, y + 8, x + 9, y + 11);
		line(C_GREEN, x + 9, y + 11, x + 6, y + 14);
		line(C_GREEN, x + 11, y + 15, x + 17, y + 15);
		rect(C_SHADOW, x + 12, y + 24, 8, 3);
		rect(C_DARK, x + 7, y + 27, 18, 3);
	} else if (!strcmp(name, "files")) {
		rect(C_DARK, x + 2, y + 7, 12, 4);
		rect(C_YELLOW, x + 3, y + 8, 10, 3);
		rect(C_DARK, x + 2, y + 10, 28, 18);
		rect(C_YELLOW, x + 3, y + 11, 26, 16);
		line(C_WHITE, x + 4, y + 12, x + 27, y + 12);
	} else if (!strcmp(name, "edit")) {
		rect(C_DARK, x + 6, y + 2, 20, 28);
		rect(C_WHITE, x + 7, y + 3, 18, 26);
		for (i = 0; i < 6; i++)
			line(C_BLUE, x + 9, y + 7 + 4 * i, x + (i == 5 ? 17 : 22), y + 7 + 4 * i);
		line(C_RED, x + 26, y + 10, x + 16, y + 26);
		line(C_RED, x + 27, y + 11, x + 17, y + 27);
	} else if (!strcmp(name, "calc")) {
		rect(C_DARK, x + 6, y + 2, 20, 28);
		rect(C_SHADOW, x + 7, y + 3, 18, 26);
		rect(C_SCREEN, x + 9, y + 5, 14, 6);
		line(C_GREEN, x + 16, y + 8, x + 21, y + 8);
		for (i = 0; i < 9; i++)
			rect(i == 8 ? C_RED : C_LIGHT, x + 9 + 5 * (i % 3), y + 13 + 5 * (i / 3), 4, 4);
	} else if (!strcmp(name, "book")) {
		rect(C_DARK, x + 4, y + 4, 24, 24);
		rect(C_BLUE, x + 5, y + 5, 22, 22);
		rect(C_WHITE, x + 24, y + 6, 3, 20);
		rect(C_YELLOW, x + 9, y + 10, 12, 3);
		rect(C_YELLOW, x + 9, y + 15, 8, 2);
	} else if (!strcmp(name, "load")) {
		rect(C_DARK, x + 2, y + 4, 28, 24);
		rect(C_SCREEN, x + 3, y + 5, 26, 22);
		{
			static const int h[] = { 4, 7, 5, 11, 15, 9, 13, 18, 12, 8, 14, 17, 10 };
			for (i = 0; i < 13; i++)
				line(C_GREEN, x + 4 + 2 * i, y + 26, x + 4 + 2 * i, y + 26 - h[i]);
		}
	} else if (!strcmp(name, "game")) {
		/* a shield, as on FRUA's cursor */
		rect(C_DARK, x + 6, y + 3, 20, 16);
		rect(C_BLUE, x + 7, y + 4, 18, 15);
		for (i = 0; i < 10; i++) {
			line(C_DARK, x + 6 + i, y + 19 + i, x + 25 - i, y + 19 + i);
			if (i < 9)
				line(C_BLUE, x + 7 + i, y + 19 + i, x + 24 - i, y + 19 + i);
		}
		rect(C_YELLOW, x + 15, y + 6, 2, 16);
		rect(C_YELLOW, x + 10, y + 11, 12, 2);
	} else if (!strcmp(name, "mail")) {
		rect(C_DARK, x + 3, y + 8, 26, 17);
		rect(C_WHITE, x + 4, y + 9, 24, 15);
		line(C_DARK, x + 4, y + 9, x + 16, y + 18);
		line(C_DARK, x + 28, y + 9, x + 16, y + 18);
	} else if (!strcmp(name, "paint")) {
		rect(C_DARK, x + 3, y + 6, 26, 20);
		rect(C_TAN, x + 4, y + 7, 24, 18);
		rect(C_RED, x + 7, y + 10, 5, 4);
		rect(C_BLUE, x + 14, y + 10, 5, 4);
		rect(C_GREEN, x + 21, y + 10, 5, 4);
		rect(C_YELLOW, x + 7, y + 17, 5, 4);
	} else if (!strcmp(name, "clock")) {
		fg(C_DARK); XFillArc(dpy, draw_on, gc, x + 3, y + 3, 26, 26, 0, 360 * 64);
		fg(C_WHITE); XFillArc(dpy, draw_on, gc, x + 5, y + 5, 22, 22, 0, 360 * 64);
		line(C_DARK, x + 16, y + 16, x + 16, y + 8);
		line(C_DARK, x + 16, y + 16, x + 22, y + 16);
	} else if (!strcmp(name, "exit")) {
		/* a door ajar, and an arrow out of it */
		rect(C_DARK, x + 5, y + 3, 16, 26);
		rect(C_TAN, x + 6, y + 4, 14, 24);
		rect(C_DARK, x + 8, y + 5, 10, 24);
		rect(C_SHADOW, x + 9, y + 6, 8, 22);
		rect(C_YELLOW, x + 15, y + 16, 2, 2);
		rect(C_RED, x + 19, y + 14, 8, 4);
		for (i = 0; i < 5; i++)
			line(C_RED, x + 26 + i, y + 11 + i, x + 26 + i, y + 20 - i);
	} else {			/* tool, or anything unknown: a gear-ish box */
		rect(C_DARK, x + 8, y + 8, 16, 16);
		rect(C_SHADOW, x + 9, y + 9, 14, 14);
		for (i = 0; i < 4; i++) {
			rect(C_DARK, x + 14, y + 3 + 23 * (i & 1), 4, 5);
			rect(C_DARK, x + 3 + 23 * (i & 1), y + 14, 5, 4);
		}
		rect(C_FACE, x + 13, y + 13, 6, 6);
	}
}

static void draw_button(int i)
{
	Button *b = &btn[i];
	int y = PAD, w, sunk = (i == pressed);

	rect(C_FACE, b->x, y, BTN_W, BTN_H);
	bevel(b->x, y, BTN_W, BTN_H, sunk);
	icon(b->icon, b->x + (BTN_W - 32) / 2 + sunk, y + 5 + sunk);
	w = XTextWidth(font, b->label, (int)strlen(b->label));
	fg(C_TEXT);
	XDrawString(dpy, win, gc, b->x + (BTN_W - w) / 2 + sunk,
		    y + BTN_H - 6 + sunk, b->label, (int)strlen(b->label));
}

static void draw_clock(void)
{
	char t[16], d[32];
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	static const char *wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char *mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
				    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	int w = 110, tw, dw;

	sprintf(t, "%2d:%02d", tm->tm_hour, tm->tm_min);
	sprintf(d, "%s %d %s", wd[tm->tm_wday], tm->tm_mday, mo[tm->tm_mon]);
	rect(C_SCREEN, clock_x, PAD, w, BTN_H);
	bevel(clock_x, PAD, w, BTN_H, 1);
	tw = XTextWidth(bigfont, t, (int)strlen(t));
	dw = XTextWidth(font, d, (int)strlen(d));
	XSetFont(dpy, gc, bigfont->fid);
	fg(C_GREEN);
	XDrawString(dpy, win, gc, clock_x + (w - tw) / 2, PAD + 8 + bigfont->ascent,
		    t, (int)strlen(t));
	XSetFont(dpy, gc, font->fid);
	fg(C_LIGHT);
	XDrawString(dpy, win, gc, clock_x + (w - dw) / 2, PAD + BTN_H - 8, d, (int)strlen(d));
}

/* the workspace switcher: ws_cols x ws_rows cells, the one on show sunk */
#define WS_W	24

static void ws_cell(int n, int *x, int *y, int *w, int *h)
{
	int c = (n - 1) % ws_cols, r = (n - 1) / ws_cols;

	*h = (BTN_H - (ws_rows - 1) * 2) / ws_rows;
	*w = WS_W;
	*x = ws_x + c * (WS_W + 2);
	*y = PAD + r * (*h + 2);
}

static void draw_switcher(void)
{
	int n, x, y, w, h, tw;
	char s[8];

	if (ws_cols == 0)
		return;
	for (n = 1; n <= ws_cols * ws_rows; n++) {
		int on = (n == ws_cur);

		ws_cell(n, &x, &y, &w, &h);
		rect(on ? C_BLUE : C_FACE, x, y, w, h);
		bevel(x, y, w, h, on);
		sprintf(s, "%d", n % 100);
		tw = XTextWidth(font, s, (int)strlen(s));
		fg(on ? C_WHITE : C_TEXT);
		XDrawString(dpy, win, gc, x + (w - tw) / 2 + on,
			    y + (h + font->ascent) / 2 + on, s, (int)strlen(s));
	}
}

static void draw_exit(int sunk)
{
	const char *l = "Log out";
	int w = XTextWidth(font, l, (int)strlen(l));

	rect(C_FACE, exit_x, PAD, BTN_W, BTN_H);
	bevel(exit_x, PAD, BTN_W, BTN_H, sunk);
	icon("exit", exit_x + (BTN_W - 32) / 2 + sunk, PAD + 5 + sunk);
	fg(C_TEXT);
	XDrawString(dpy, win, gc, exit_x + (BTN_W - w) / 2 + sunk,
		    PAD + BTN_H - 6 + sunk, l, (int)strlen(l));
}

static void draw_all(void)
{
	int i;

	rect(C_FACE, 0, 0, panel_w, panel_h);
	bevel(0, 0, panel_w, panel_h, 0);
	for (i = 0; i < nbtn; i++)
		draw_button(i);
	draw_switcher();
	draw_clock();
	draw_exit(0);
}

/* olvwm's _OLVWM_DESKTOP: columns, rows, the screen on show */
static void read_desktop(void)
{
	Atom type;
	int fmt;
	unsigned long n, after;
	unsigned char *p = NULL;

	ws_cols = ws_rows = ws_cur = 0;
	if (XGetWindowProperty(dpy, RootWindow(dpy, scr), a_desktop, 0, 3, False,
			       XA_INTEGER, &type, &fmt, &n, &after, &p) == Success
	    && p != NULL && fmt == 32 && n == 3) {
		long *v = (long *)p;

		/* olvwm goes to screens 1-10 */
		if (v[0] > 0 && v[1] > 0 && v[0] * v[1] <= 10) {
			ws_cols = (int)v[0];
			ws_rows = (int)v[1];
			ws_cur = (int)v[2];
		}
	}
	if (p != NULL)
		XFree((char *)p);
}

/* positions, and the panel's width, from what it holds */
static void layout(int sw)
{
	int i, x = PAD;

	for (i = 0; i < nbtn; i++) {
		btn[i].x = x;
		x += BTN_W + 2;
	}
	x += 6;
	ws_x = x;
	if (ws_cols)
		x += ws_cols * (WS_W + 2) + 6;
	clock_x = x;
	x += 110 + 4;
	exit_x = x;
	panel_w = x + BTN_W + PAD;
	if (panel_w > sw)
		panel_w = sw;
}

static void send_root(Atom what, long arg)
{
	XEvent e;

	memset(&e, 0, sizeof e);
	e.xclient.type = ClientMessage;
	e.xclient.window = RootWindow(dpy, scr);
	e.xclient.message_type = what;
	e.xclient.format = 32;
	e.xclient.data.l[0] = arg;
	XSendEvent(dpy, RootWindow(dpy, scr), False,
		   SubstructureRedirectMask | SubstructureNotifyMask, &e);
	XFlush(dpy);
}

/* what is at (x, y): a launcher's index, 100 + n for switcher screen n,
 * 200 for Log out, -1 for nothing */
static int hit(int x, int y)
{
	int i, cx, cy, cw, ch;

	if (y < PAD || y >= PAD + BTN_H)
		return -1;
	for (i = 0; i < nbtn; i++)
		if (x >= btn[i].x && x < btn[i].x + BTN_W)
			return i;
	for (i = 1; ws_cols && i <= ws_cols * ws_rows; i++) {
		ws_cell(i, &cx, &cy, &cw, &ch);
		if (x >= cx && x < cx + cw && y >= cy && y < cy + ch)
			return 100 + i;
	}
	if (x >= exit_x && x < exit_x + BTN_W)
		return 200;
	return -1;
}

/* --- the configurator: a menu on the right button, and a dialog ---------- */

static const char *icon_names[] = { "term", "files", "edit", "calc", "book",
	"load", "game", "mail", "paint", "tool", "clock", "exit" };
#define NICON		12
#define ICON_COLS	6
#define CELL		44

enum { M_EDIT, M_ADD, M_REMOVE, M_LEFT, M_RIGHT, M_RELOAD };
static const char *menu_text[] = { "Edit...", "Add after...", "Remove",
	"Move left", "Move right", "Reload ~/.ttpanelrc" };
static XFontStruct *dlgfont;
static Window	menu_win, dlg_win;
static int	menu_target;		/* the launcher under the menu, or -1 */
static int	menu_n, menu_sel, menu_w, menu_ih, menu_clicked;
static int	menu_ids[6];

static Button	edit;			/* the button the dialog makes or changes */
static int	edit_index;		/* its slot, or -1: a new one after edit_after */
static int	edit_after;
static int	field, cur[2];		/* the field with the caret, the carets */
static int	ok_down, cancel_down, dlg_focused;
static Window	prev_focus;
static int	prev_revert;
#define DLG_W		440
#define DLG_H		252
#define FLD_X		90
#define FLD_H		24
#define LABEL_Y		40
#define CMD_Y		76
#define ICON_Y		116
#define DBTN_W		80
#define DBTN_H		26
#define DBTN_Y		(DLG_H - DBTN_H - 12)
#define OK_X		(DLG_W - 2 * DBTN_W - 24)
#define CANCEL_X	(DLG_W - DBTN_W - 12)

static void relayout(void)
{
	layout(sw);
	XMoveResizeWindow(dpy, win, (sw - panel_w) / 2, top ? 0 : sh - panel_h,
			  panel_w, panel_h);
	draw_on = win;
	draw_all();
}

static void apply(void)
{
	save_config();
	relayout();
}

static int text_y(int y, int h)
{
	return y + (h + dlgfont->ascent - dlgfont->descent) / 2;
}

/* -- the menu -- */

static void draw_menu(void)
{
	int i, y, h = menu_n * menu_ih + 4;

	draw_on = menu_win;
	rect(C_FACE, 0, 0, menu_w, h);
	bevel(0, 0, menu_w, h, 0);
	XSetFont(dpy, gc, dlgfont->fid);
	for (i = 0; i < menu_n; i++) {
		int on = (i == menu_sel);
		const char *t = menu_text[menu_ids[i]];

		y = 2 + i * menu_ih;
		if (on)
			rect(C_BLUE, 2, y, menu_w - 4, menu_ih);
		fg(on ? C_WHITE : C_TEXT);
		XDrawString(dpy, menu_win, gc, 12, text_y(y, menu_ih), t, (int)strlen(t));
	}
	XSetFont(dpy, gc, font->fid);
	draw_on = win;
}

static int menu_item_at(int x, int y)
{
	if (x < 2 || x >= menu_w - 2 || y < 2)
		return -1;
	y = (y - 2) / menu_ih;
	return y < menu_n ? y : -1;
}

static void open_menu(int target, int xroot)
{
	XSetWindowAttributes wa;
	int i, h, x, y, tw;

	if (dlg_win != None || menu_win != None)
		return;
	menu_target = target;
	menu_n = 0;
	if (target >= 0) {
		menu_ids[menu_n++] = M_EDIT;
		menu_ids[menu_n++] = M_ADD;
		menu_ids[menu_n++] = M_REMOVE;
		menu_ids[menu_n++] = M_LEFT;
		menu_ids[menu_n++] = M_RIGHT;
	} else
		menu_ids[menu_n++] = M_ADD;
	menu_ids[menu_n++] = M_RELOAD;
	menu_ih = dlgfont->ascent + dlgfont->descent + 8;
	menu_w = 0;
	for (i = 0; i < menu_n; i++) {
		tw = XTextWidth(dlgfont, menu_text[menu_ids[i]],
				(int)strlen(menu_text[menu_ids[i]]));
		if (tw + 28 > menu_w)
			menu_w = tw + 28;
	}
	h = menu_n * menu_ih + 4;
	x = xroot - 8;
	if (x + menu_w > sw)
		x = sw - menu_w;
	if (x < 0)
		x = 0;
	y = top ? panel_h + 2 : sh - panel_h - h - 2;
	menu_sel = -1;
	menu_clicked = 0;
	wa.override_redirect = True;
	wa.background_pixel = col[C_FACE];
	wa.save_under = True;
	wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
			PointerMotionMask;
	menu_win = XCreateWindow(dpy, RootWindow(dpy, scr), x, y, menu_w, h, 0,
				 CopyFromParent, InputOutput, CopyFromParent,
				 CWOverrideRedirect | CWBackPixel | CWSaveUnder |
				 CWEventMask, &wa);
	XMapRaised(dpy, menu_win);
	/* every button event comes to the menu while it is up, in its
	 * coordinates: a release on an item picks it, a click elsewhere
	 * takes it down */
	XGrabPointer(dpy, menu_win, False,
		     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
}

static void close_menu(void)
{
	XUngrabPointer(dpy, CurrentTime);
	XDestroyWindow(dpy, menu_win);
	menu_win = None;
}

static void open_dlg(void);

static void menu_act(int id)
{
	int t = menu_target;
	Button tmp;

	switch (id) {
	case M_EDIT:
		edit = btn[t];
		edit_index = t;
		cur[0] = (int)strlen(edit.label);
		cur[1] = (int)strlen(edit.cmd);
		open_dlg();
		break;
	case M_ADD:
		if (nbtn >= MAXBTN)
			break;
		memset(&edit, 0, sizeof edit);
		strcpy(edit.icon, "tool");
		edit_index = -1;
		edit_after = t >= 0 ? t : nbtn - 1;
		cur[0] = cur[1] = 0;
		open_dlg();
		break;
	case M_REMOVE:
		if (nbtn <= 1)		/* an empty panel would come back with the defaults */
			break;
		memmove(&btn[t], &btn[t + 1], (size_t)(nbtn - t - 1) * sizeof btn[0]);
		nbtn--;
		apply();
		break;
	case M_LEFT:
		if (t > 0) {
			tmp = btn[t]; btn[t] = btn[t - 1]; btn[t - 1] = tmp;
			apply();
		}
		break;
	case M_RIGHT:
		if (t < nbtn - 1) {
			tmp = btn[t]; btn[t] = btn[t + 1]; btn[t + 1] = tmp;
			apply();
		}
		break;
	case M_RELOAD:
		nbtn = 0;
		load_config();
		relayout();
		break;
	}
}

static void menu_event(XEvent *e)
{
	int s;

	switch (e->type) {
	case Expose:
		if (e->xexpose.count == 0)
			draw_menu();
		break;
	case MotionNotify:
		s = menu_item_at(e->xmotion.x, e->xmotion.y);
		if (s != menu_sel) {
			menu_sel = s;
			draw_menu();
		}
		break;
	case ButtonPress:
		s = menu_item_at(e->xbutton.x, e->xbutton.y);
		if (s < 0)
			close_menu();
		else if (s != menu_sel) {
			menu_sel = s;
			draw_menu();
		}
		break;
	case ButtonRelease:
		s = menu_item_at(e->xbutton.x, e->xbutton.y);
		if (s >= 0) {
			int id = menu_ids[s];

			close_menu();
			menu_act(id);
		} else if (menu_clicked)
			close_menu();
		else
			menu_clicked = 1;	/* a click opened it: it stays up */
		break;
	}
}

/* -- the dialog -- */

static void draw_field(int f)
{
	const char *s = f ? edit.cmd : edit.label;
	int y = f ? CMD_Y : LABEL_Y, w = DLG_W - FLD_X - 16;
	int len = (int)strlen(s), c = cur[f], start = 0, n, tx;

	rect(C_WHITE, FLD_X, y, w, FLD_H);
	bevel(FLD_X, y, w, FLD_H, 1);
	/* the caret stays in view: the start of a long text goes off the left */
	while (start < c && XTextWidth(dlgfont, s + start, c - start) > w - 12)
		start++;
	n = len - start;
	while (n > 0 && XTextWidth(dlgfont, s + start, n) > w - 12)
		n--;
	XSetFont(dpy, gc, dlgfont->fid);
	fg(C_TEXT);
	XDrawString(dpy, dlg_win, gc, FLD_X + 6, text_y(y, FLD_H), s + start, n);
	XSetFont(dpy, gc, font->fid);
	if (f == field) {
		tx = FLD_X + 6 + XTextWidth(dlgfont, s + start, c - start);
		line(C_BLUE, tx, y + 5, tx, y + FLD_H - 6);
		line(C_BLUE, tx + 1, y + 5, tx + 1, y + FLD_H - 6);
	}
}

static void draw_dbutton(int x, const char *t, int sunk)
{
	int w = XTextWidth(dlgfont, t, (int)strlen(t));

	rect(C_FACE, x, DBTN_Y, DBTN_W, DBTN_H);
	bevel(x, DBTN_Y, DBTN_W, DBTN_H, sunk);
	XSetFont(dpy, gc, dlgfont->fid);
	fg(C_TEXT);
	XDrawString(dpy, dlg_win, gc, x + (DBTN_W - w) / 2 + sunk,
		    text_y(DBTN_Y, DBTN_H) + sunk, t, (int)strlen(t));
	XSetFont(dpy, gc, font->fid);
}

static void draw_dlg(void)
{
	const char *t = edit_index < 0 ? "A new panel button" : "The panel button";
	int i, x, y;

	draw_on = dlg_win;
	rect(C_FACE, 0, 0, DLG_W, DLG_H);
	bevel(0, 0, DLG_W, DLG_H, 0);
	XSetFont(dpy, gc, dlgfont->fid);
	fg(C_TEXT);
	XDrawString(dpy, dlg_win, gc, 12, 10 + dlgfont->ascent, t, (int)strlen(t));
	XDrawString(dpy, dlg_win, gc, 12, text_y(LABEL_Y, FLD_H), "Label", 5);
	XDrawString(dpy, dlg_win, gc, 12, text_y(CMD_Y, FLD_H), "Command", 7);
	XDrawString(dpy, dlg_win, gc, 12, text_y(ICON_Y, CELL), "Icon", 4);
	XSetFont(dpy, gc, font->fid);
	draw_field(0);
	draw_field(1);
	for (i = 0; i < NICON; i++) {
		int on = !strcmp(icon_names[i], edit.icon);

		x = FLD_X + (i % ICON_COLS) * CELL;
		y = ICON_Y + (i / ICON_COLS) * CELL;
		rect(on ? C_BLUE : C_FACE, x, y, CELL - 2, CELL - 2);
		bevel(x, y, CELL - 2, CELL - 2, on);
		icon(icon_names[i], x + (CELL - 2 - 32) / 2 + on, y + (CELL - 2 - 32) / 2 + on);
	}
	draw_dbutton(OK_X, "OK", ok_down);
	draw_dbutton(CANCEL_X, "Cancel", cancel_down);
	draw_on = win;
}

static void open_dlg(void)
{
	XSetWindowAttributes wa;

	if (dlg_win != None)
		return;
	field = 0;
	ok_down = cancel_down = dlg_focused = 0;
	wa.override_redirect = True;
	wa.background_pixel = col[C_FACE];
	wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
			KeyPressMask;
	dlg_win = XCreateWindow(dpy, RootWindow(dpy, scr), (sw - DLG_W) / 2,
				(sh - DLG_H) / 2, DLG_W, DLG_H, 0, CopyFromParent,
				InputOutput, CopyFromParent,
				CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	XStoreName(dpy, dlg_win, "ttpanel");
	XMapRaised(dpy, dlg_win);
}

static void close_dlg(void)
{
	if (dlg_focused)
		XSetInputFocus(dpy, prev_focus, prev_revert, CurrentTime);
	XDestroyWindow(dpy, dlg_win);
	dlg_win = None;
}

static void dlg_ok(void)
{
	trim(edit.label);
	trim(edit.cmd);
	if (edit.cmd[0] == 0) {		/* nothing to run: the caret goes there */
		field = 1;
		draw_dlg();
		return;
	}
	if (edit.label[0] == 0) {	/* the program's name */
		const char *p = edit.cmd, *q;
		int n = (int)strcspn(p, " \t");

		for (q = p; q < p + n; q++)
			if (*q == '/')
				p = q + 1;
		n -= (int)(p - edit.cmd);
		if (n > (int)sizeof edit.label - 1)
			n = (int)sizeof edit.label - 1;
		memcpy(edit.label, p, (size_t)n);
		edit.label[n] = 0;
	}
	if (edit_index < 0) {
		if (nbtn >= MAXBTN) {
			close_dlg();
			return;
		}
		memmove(&btn[edit_after + 2], &btn[edit_after + 1],
			(size_t)(nbtn - edit_after - 1) * sizeof btn[0]);
		btn[edit_after + 1] = edit;
		nbtn++;
	} else
		btn[edit_index] = edit;
	close_dlg();
	apply();
}

static void dlg_key(XKeyEvent *e)
{
	char buf[8], *s = field ? edit.cmd : edit.label;
	int max = field ? (int)sizeof edit.cmd - 1 : (int)sizeof edit.label - 1;
	int len = (int)strlen(s), *c = &cur[field], n;
	KeySym ks = 0;

	n = XLookupString(e, buf, sizeof buf, &ks, NULL);
	switch (ks) {
	case XK_Tab:
		field ^= 1;
		break;
	case XK_Return:
	case XK_KP_Enter:
		dlg_ok();
		return;
	case XK_Escape:
		close_dlg();
		return;
	case XK_BackSpace:
		if (*c > 0) {
			memmove(s + *c - 1, s + *c, (size_t)(len - *c + 1));
			(*c)--;
		}
		break;
	case XK_Delete:
		if (*c < len)
			memmove(s + *c, s + *c + 1, (size_t)(len - *c));
		break;
	case XK_Left:
		if (*c > 0)
			(*c)--;
		break;
	case XK_Right:
		if (*c < len)
			(*c)++;
		break;
	case XK_Home:
		*c = 0;
		break;
	case XK_End:
		*c = len;
		break;
	default:
		if (n == 1 && (e->state & ControlMask) && (buf[0] == 'u' || buf[0] == 21)) {
			s[0] = 0;	/* ^U: the field emptied */
			*c = 0;
		} else if (n == 1 && buf[0] >= ' ' && buf[0] != 127 && len < max &&
			   !(field == 0 && buf[0] == ':')) {	/* ':' ends a label in the file */
			memmove(s + *c + 1, s + *c, (size_t)(len - *c + 1));
			s[*c] = buf[0];
			(*c)++;
		}
		break;
	}
	draw_dlg();
}

static void dlg_event(XEvent *e)
{
	int x, y, i;

	switch (e->type) {
	case Expose:
		if (e->xexpose.count)
			break;
		draw_dlg();
		if (!dlg_focused) {	/* typing goes to the dialog; the focus
					 * is given back when it closes */
			XGetInputFocus(dpy, &prev_focus, &prev_revert);
			XSetInputFocus(dpy, dlg_win, RevertToPointerRoot, CurrentTime);
			dlg_focused = 1;
		}
		break;
	case KeyPress:
		dlg_key(&e->xkey);
		break;
	case ButtonPress:
		x = e->xbutton.x;
		y = e->xbutton.y;
		if (x >= FLD_X && x < DLG_W - 16 && y >= LABEL_Y && y < LABEL_Y + FLD_H) {
			field = 0;
			cur[0] = (int)strlen(edit.label);
		} else if (x >= FLD_X && x < DLG_W - 16 && y >= CMD_Y && y < CMD_Y + FLD_H) {
			field = 1;
			cur[1] = (int)strlen(edit.cmd);
		} else if (x >= FLD_X && x < FLD_X + ICON_COLS * CELL &&
			   y >= ICON_Y && y < ICON_Y + (NICON / ICON_COLS) * CELL) {
			i = (y - ICON_Y) / CELL * ICON_COLS + (x - FLD_X) / CELL;
			strcpy(edit.icon, icon_names[i]);
		} else if (y >= DBTN_Y && y < DBTN_Y + DBTN_H && x >= OK_X && x < OK_X + DBTN_W)
			ok_down = 1;
		else if (y >= DBTN_Y && y < DBTN_Y + DBTN_H && x >= CANCEL_X && x < CANCEL_X + DBTN_W)
			cancel_down = 1;
		else
			break;
		draw_dlg();
		break;
	case ButtonRelease:
		x = e->xbutton.x;
		y = e->xbutton.y;
		if (ok_down) {
			ok_down = 0;
			if (y >= DBTN_Y && y < DBTN_Y + DBTN_H && x >= OK_X && x < OK_X + DBTN_W) {
				dlg_ok();
				return;
			}
			draw_dlg();
		} else if (cancel_down) {
			cancel_down = 0;
			if (y >= DBTN_Y && y < DBTN_Y + DBTN_H && x >= CANCEL_X && x < CANCEL_X + DBTN_W) {
				close_dlg();
				return;
			}
			draw_dlg();
		}
		break;
	}
}

/* an X error (a window gone while the focus went back to it, say) is not
 * worth the panel: Xlib's own handler would exit */
static int xerror(Display *d, XErrorEvent *e)
{
	return 0;
}

int main(int argc, char **argv)
{
	XSetWindowAttributes wa;
	XColor xc;
	Colormap cm;
	int i;
	const char *fn = "-*-helvetica-bold-r-normal--10-*";
	XEvent ev;
	int last_min = -1;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-top"))
			top = 1;
		else if (!strcmp(argv[i], "-fn") && i + 1 < argc)
			fn = argv[++i];
	}
	if ((dpy = XOpenDisplay(NULL)) == NULL) {
		fprintf(stderr, "ttpanel: cannot open the display\n");
		return 1;
	}
	signal(SIGCHLD, SIG_IGN);
	XSetErrorHandler(xerror);
	scr = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, scr);
	sh = DisplayHeight(dpy, scr);
	cm = DefaultColormap(dpy, scr);
	for (i = 0; i < C_N; i++) {
		col[i] = (i == C_WHITE || i == C_LIGHT) ? WhitePixel(dpy, scr) : BlackPixel(dpy, scr);
		if (XParseColor(dpy, cm, cname[i], &xc) && XAllocColor(dpy, cm, &xc))
			col[i] = xc.pixel;
	}
	if ((font = XLoadQueryFont(dpy, fn)) == NULL &&
	    (font = XLoadQueryFont(dpy, "6x10")) == NULL &&
	    (font = XLoadQueryFont(dpy, "fixed")) == NULL) {
		fprintf(stderr, "ttpanel: no font\n");
		return 1;
	}
	if ((bigfont = XLoadQueryFont(dpy, "-*-helvetica-bold-r-normal--24-*")) == NULL &&
	    (bigfont = XLoadQueryFont(dpy, "12x24")) == NULL)
		bigfont = font;
	if ((dlgfont = XLoadQueryFont(dpy, "-*-helvetica-medium-r-normal--12-*")) == NULL &&
	    (dlgfont = XLoadQueryFont(dpy, "6x13")) == NULL)
		dlgfont = font;

	load_config();
	a_desktop = XInternAtom(dpy, "_OLVWM_DESKTOP", False);
	a_goto = XInternAtom(dpy, "_OLVWM_GOTO", False);
	a_exit = XInternAtom(dpy, "_OLVWM_EXIT", False);
	/* the window manager may start after us: watch for its property */
	XSelectInput(dpy, RootWindow(dpy, scr), PropertyChangeMask);
	read_desktop();
	panel_h = BTN_H + 2 * PAD;
	layout(sw);

	/* no frame: the window manager leaves an override-redirect window
	 * alone, and the panel keeps itself on top when covered */
	wa.override_redirect = True;
	wa.background_pixel = col[C_FACE];
	wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
			VisibilityChangeMask;
	win = XCreateWindow(dpy, RootWindow(dpy, scr), (sw - panel_w) / 2,
			    top ? 0 : sh - panel_h, panel_w, panel_h, 0,
			    CopyFromParent, InputOutput, CopyFromParent,
			    CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	XStoreName(dpy, win, "ttpanel");
	draw_on = win;
	gc = XCreateGC(dpy, win, 0, NULL);
	XSetFont(dpy, gc, font->fid);
	XMapRaised(dpy, win);

	for (;;) {
		/* wake at least once a minute for the clock */
		while (XPending(dpy) == 0) {
			fd_set fds;
			struct timeval tv;
			time_t now = time(NULL);
			struct tm *tm = localtime(&now);

			if (tm->tm_min != last_min) {
				last_min = tm->tm_min;
				draw_clock();
				XFlush(dpy);
			}
			FD_ZERO(&fds);
			FD_SET(ConnectionNumber(dpy), &fds);
			tv.tv_sec = 60 - tm->tm_sec;
			tv.tv_usec = 0;
			select(ConnectionNumber(dpy) + 1, &fds, NULL, NULL, &tv);
		}
		XNextEvent(dpy, &ev);
		if (menu_win != None && ev.xany.window == menu_win) {
			menu_event(&ev);
			continue;
		}
		if (dlg_win != None && ev.xany.window == dlg_win) {
			dlg_event(&ev);
			continue;
		}
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				draw_all();
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		case PropertyNotify:
			if (ev.xproperty.atom == a_desktop) {
				int oc = ws_cols, orr = ws_rows;

				read_desktop();
				if (ws_cols != oc || ws_rows != orr)
					relayout();
				else
					draw_switcher();
			}
			break;
		case ButtonPress:
			if (ev.xbutton.button == Button3) {
				int h = hit(ev.xbutton.x, ev.xbutton.y);

				open_menu(h >= 0 && h < MAXBTN ? h : -1, ev.xbutton.x_root);
				break;
			}
			pressed = hit(ev.xbutton.x, ev.xbutton.y);
			if (pressed >= 0 && pressed < MAXBTN)
				draw_button(pressed);
			else if (pressed == 200)
				draw_exit(1);
			break;
		case ButtonRelease:
			if (pressed >= 0) {
				int was = pressed;

				pressed = -1;
				if (was < MAXBTN)
					draw_button(was);
				else if (was == 200)
					draw_exit(0);
				XFlush(dpy);
				if (hit(ev.xbutton.x, ev.xbutton.y) != was)
					break;
				if (was < MAXBTN)
					launch(btn[was].cmd);
				else if (was > 100 && was <= 110)
					send_root(a_goto, (long)(was - 100));
				else if (was == 200)
					send_root(a_exit, 0L);
			}
			break;
		}
	}
}
