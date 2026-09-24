# XView and OpenLook for Atari System V

Sun's XView 3.2 toolkit (the OPEN LOOK look and feel) with its window
window managers `olwm` and `olvwm` (olwm with a virtual desktop), the
shell window `cmdtool`, `textedit`, `props`, `clock` and the 97 XView example
programs, cross-built for
Atari System V on top of the X11R6.3 build in `../xserver-r6`. The source
is the Linux patchlevel 4 of XView 3.2p1-X11R6 (ibiblio), whose SVR4 code
paths date from Sun's Solaris 2 port: that is the side ASV follows.

```sh
sh ../xserver-r6/build.sh           # first: X, its libraries and config
sh build.sh                         # -> work/xview-asv.tar (/usr/openwin)
```

Install both tarballs from `/` (`tar xf`); the programs find their
libraries through an RPATH of `/usr/openwin/lib:/usr/x11r6/lib`. The xdm
session in `../xserver-r6/xdm/Xsession` starts `cmdtool`, `clock` and
`olvwm` (or `olwm`, from a `~/.xsession`); the Workspace menu is
`openwin-menu` (Shell Tool, Text Editor, Clock, xterm, XView Demos,
Properties, Refresh, Exit). The examples from `contrib/examples` are in
`/usr/openwin/demo/xview/<category>/`, each beside its source. Also installed: the Help
key's texts in `/usr/openwin/lib/help` (the session sets `HELPPATH`; XView's
default is the system's `/usr/lib/help`), a text Extras menu for ASV
(`text_extras_menu`: Sun's ran SunOS filters, this one `fmt`, `tr`, `sed`,
`sort`, `expand`), and the Localization data `props` reads from
`share/locale/<locale>/props` (from Sun's OpenWindows, not in the free
source; `build.sh` makes it for seven of ASV's locales).

## What ASV needed

`patches/xview-3.2p1.4-asv.patch` (16 files) and two compat headers:

- `config/XView.cf`: an ASV branch (`-DX11R6`, `OPENWINHOME=/usr/openwin`);
  `XView.rules`: shared libraries get a SONAME (`ld -h`).
- `asv/include/sys/rusage.h`: ASV has no `struct rusage`; the notifier
  only passes one around. `asv/include/sys/bufmod.h`: no STREAMS buffer
  module; left empty, XView does not push it.
- `ntfy.h`/`ndet_loop.c`: ASV's `sigset_t` is `{ s[2] }` and its
  `struct sigaction` is `{ handler, mask, flags }` (Solaris differs in both).
- `sys_select.c`: ASV's timers tick at 1/128 s, so the time left to the
  next itimer expiry can come out as 1000000 us or slightly negative;
  the notifier rejected both with EINVAL. Normalised.
- `tty_init.c`: ASV autopushes `ptem` and `ldterm` on a pty; push them
  only if `I_FIND` says they are not there (a second push failed with EIO).
- `sel_agent.c`: R6 hides `display->fd` (`ConnectionNumber`); `tty.c`: no
  Sun console redirection; `file_list.c`: ASV's `<regexp.h>` defines the
  regexp state itself; `txt_e_menu.c`: `<sys/types.h>` before
  `<sys/file.h>`; `wckind.c`: libc has the dl calls as `_dlopen` etc.;
  `linux_select.c`: Linux only; `images`/`bitmaps`: `all::` for GNU make.
- `olvwm-4.1`: its Imakefile hard-wires Linux (`-DXPM`, `-lbsd -lXpm`,
  `-lfl`); an ASV branch uses `-DSVR4 -DSYSV`, no XPM, and `$(LEXLIB)`
  (`asv.cf` now names ASV's `libl.a` and `libcurses.a` by path: the
  wrapper does not search `usr/ccs/lib`); its `gettext.c` was compiled out
  for glibc and is compiled in on ASV; `<sys/utsname.h>` before
  `<sys/systeminfo.h>`. Not in `clients/Imakefile`: `build.sh` makes it on
  its own.
- contrib: `disp_fonts.c` defined `random` as a one-argument macro for
  SVR4 (asvcompat has the real one); `ttycurses` links `CursesLibrary`.
- `textedit.c`, `l10n_read.c` (props): SVR4 branches beside the Linux ones
  (`<string.h>`, `<dirent.h>` for `MAXNAMLEN`, `sigaction`/`signal` for the
  BSD `sigvec`, libc's own `malloc` declarations). `asvcompat.o` gained
  `index`, `rindex` and `getwd`.
- `-lintl -ldl` are dropped (XView carries its own gettext), and
  `environ` is `_environ`.

Two problems were not XView's and are fixed below it, for every program:

- **Pointer results.** On the m68k SVR4 ABI integers come back in d0 and
  pointers in a0. XView defines functions as returning `Xv_opaque` (an
  integer) and calls them through pointer-returning prototypes (Sun's
  compilers used one register for both), so `cmdtool` crashed reading a
  stale a0 (`ts_create`). The gcc-cross-amix wrapper's
  `AMIX_RETURN_D0_TO_A0` copies d0 into a0 at every return; `asv.cf`
  turns it on.
- **`syscall()` and ERESTART.** The notifier interposes `select`/`read`
  and calls the kernel through `syscall()`. `libc.a`'s stub reports errors
  through a libc-internal routine that leaves `errno` holding an address
  when called from a program, and through the indirect entry the kernel's
  internal ERESTART (91) reaches the caller. `asvcompat.o` provides
  `syscall()` on libc's `_abi_syscall` and turns ERESTART into EINTR.

## Status (real TT with the ATW800/2, and Hatari)

`olwm` with its Workspace menu and exit notice, `cmdtool` (a shell as the
logged-in user, OPEN LOOK scrollbar and pop-up menus), `clock`; five clocks
started together all run, and the whole session starts from xdm and exits
back to it. `textedit` and `props` start clean (menu, help and locale data
found). `olvwm` manages windows and shows its Virtual Desktop (Hatari);
all 97 examples build. `contrib/misc` is not installed: `owplaces` needs
`xtoolplaces`, `openwin` is Sun's xinit starter, and the alternative menus
start Sun applications ASV does not have.
