/*
 * xfuji - show the Atari Fuji from the TOS boot screen on the root window,
 * for xdm's login screen (Xsetup_0 starts it, Xstartup stops it).
 *
 *   xfuji [-file fuji.xbm] [-scale n] [-fg color] [-y y]
 *
 * The bitmap is fuji-from-tos.py's extract from the user's own TOS ROM.
 * The window is shaped to the logo (SHAPE extension), so only the logo is
 * drawn over the root. Unless -y is given it waits (up to 15 s) for xdm's
 * login box and centres itself in the space above it, dropping to scale 1
 * if the larger logo would not fit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <poll.h>

/* xdm's greeter: a top-level window of class Xlogin, once it is mapped */
static Bool
find_login(dpy, root, x, y, w, h)
    Display *dpy;
    Window root;
    int *x, *y;
    unsigned int *w, *h;
{
    Window r, p, *kids;
    unsigned int n, i, bw, d;
    XClassHint ch;
    Bool found = False;

    if (!XQueryTree(dpy, root, &r, &p, &kids, &n))
	return False;
    for (i = 0; i < n && !found; i++) {
	if (XGetClassHint(dpy, kids[i], &ch)) {
	    if (ch.res_class && !strcmp(ch.res_class, "Xlogin")) {
		XWindowAttributes wa;
		if (XGetWindowAttributes(dpy, kids[i], &wa) &&
		    wa.map_state == IsViewable) {
		    XGetGeometry(dpy, kids[i], &r, x, y, w, h, &bw, &d);
		    found = True;
		}
	    }
	    XFree(ch.res_name);
	    XFree(ch.res_class);
	}
    }
    if (kids)
	XFree((char *)kids);
    return found;
}

#define DEFAULT_FILE	"/usr/x11r6/lib/X11/xdm/fuji.xbm"

int
main(argc, argv)
    int argc;
    char **argv;
{
    char *file = DEFAULT_FILE, *fg = "white";
    int scale = 2, y = -1, i, cx, gap = 0;
    Display *dpy;
    int scr;
    unsigned int w, h, sw, sh;
    int hx, hy;
    Pixmap src, big;
    XImage *in, *out;
    GC gc;
    Window win;
    XSetWindowAttributes attr;
    XColor col, exact;
    unsigned int x, yy, dx, dy;
    char *data;

    for (i = 1; i < argc; i++) {
	if (!strcmp(argv[i], "-file") && i + 1 < argc)
	    file = argv[++i];
	else if (!strcmp(argv[i], "-scale") && i + 1 < argc)
	    scale = atoi(argv[++i]);
	else if (!strcmp(argv[i], "-fg") && i + 1 < argc)
	    fg = argv[++i];
	else if (!strcmp(argv[i], "-y") && i + 1 < argc)
	    y = atoi(argv[++i]);
	else {
	    fprintf(stderr, "usage: xfuji [-file xbm] [-scale n] [-fg color] [-y y]\n");
	    return 2;
	}
    }
    if (scale < 1)
	scale = 1;
    if ((dpy = XOpenDisplay((char *)0)) == (Display *)0) {
	fprintf(stderr, "xfuji: cannot open display\n");
	return 1;
    }
    scr = DefaultScreen(dpy);
    cx = DisplayWidth(dpy, scr) / 2;
    if (y < 0) {
	/* wait for the login box, then use the space above it */
	int lx, ly, t;
	unsigned int lw, lh;

	gap = DisplayHeight(dpy, scr) / 3;
	for (t = 0; t < 75; t++) {
	    if (find_login(dpy, RootWindow(dpy, scr), &lx, &ly, &lw, &lh)) {
		gap = ly;
		cx = lx + (int)lw / 2;
		break;
	    }
	    poll((struct pollfd *)0, 0, 200);
	}
    }
    if (XReadBitmapFile(dpy, RootWindow(dpy, scr), file, &w, &h, &src,
			&hx, &hy) != BitmapSuccess) {
	fprintf(stderr, "xfuji: cannot read %s\n", file);
	return 1;
    }

    /* the larger logo only if it fits the space above the login box */
    while (y < 0 && scale > 1 && (int)(h * scale) + 16 > gap)
	scale--;

    /* scale the bitmap up, pixel by pixel */
    sw = w * scale;
    sh = h * scale;
    in = XGetImage(dpy, src, 0, 0, w, h, 1, XYPixmap);
    data = (char *)calloc((sw + 7) / 8 * sh, 1);
    out = XCreateImage(dpy, DefaultVisual(dpy, scr), 1, XYBitmap, 0, data,
		       sw, sh, 8, (sw + 7) / 8);
    for (yy = 0; yy < h; yy++)
	for (x = 0; x < w; x++)
	    if (XGetPixel(in, x, yy))
		for (dy = 0; dy < (unsigned)scale; dy++)
		    for (dx = 0; dx < (unsigned)scale; dx++)
			XPutPixel(out, x * scale + dx, yy * scale + dy, 1);
    big = XCreatePixmap(dpy, RootWindow(dpy, scr), sw, sh, 1);
    {
	/* an XYBitmap image draws its 1 bits in the GC's foreground, which
	 * is 0 in a default GC */
	XGCValues gv;
	gv.foreground = 1;
	gv.background = 0;
	gc = XCreateGC(dpy, big, GCForeground | GCBackground, &gv);
    }
    XPutImage(dpy, big, gc, out, 0, 0, 0, 0, sw, sh);

    if (!XAllocNamedColor(dpy, DefaultColormap(dpy, scr), fg, &col, &exact))
	col.pixel = WhitePixel(dpy, scr);
    if (y < 0)
	y = (gap - (int)sh) / 2;
    if (y < 0)
	y = 0;
    attr.override_redirect = True;
    attr.background_pixel = col.pixel;
    win = XCreateWindow(dpy, RootWindow(dpy, scr),
			cx - (int)sw / 2, y, sw, sh, 0,
			CopyFromParent, InputOutput, CopyFromParent,
			CWOverrideRedirect | CWBackPixel, &attr);
    XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, big, ShapeSet);
    XMapWindow(dpy, win);
    XFlush(dpy);

    /* stay until killed; the background pixel does the drawing */
    for (;;) {
	XEvent ev;
	XNextEvent(dpy, &ev);
    }
}
