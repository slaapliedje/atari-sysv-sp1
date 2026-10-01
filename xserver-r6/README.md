# X11R6.3 for Atari System V

The ATW800/2 server and the X11R6.3 client libraries and clients, cross-built
for Atari System V on top of isoriano1968's
[X11R6.3 port for AMIX](https://github.com/isoriano1968/x11r6.3-amix)
(Amiga UNIX is the same UniSoft SVR4/m68k and uses the same gcc-cross-amix
toolchain). This directory holds only what Atari System V adds:

| Path | What |
|---|---|
| `build.sh` | fetch the pinned overlay (its `install.sh` downloads and verifies the X.Org sources), apply the rest, build everything |
| `mksysroot.sh` | a "fixincluded" shadow of the ASV sysroot for the compiler (see below) |
| `config/asv.cf` | imake platform file: the cross compiler, shared X libraries, TCP, no XKB server/SHM/PEX/XIE/LBX/Xprint/Xnest/Xvfb |
| `config/asviob.c` | `stdin`/`stdout`/`stderr` for every program (see below) |
| `libcextra.py` | picks the `libc.a` members a program needs that ASV's shared libc lacks |
| `xdm/` | the login screen: ASV xdm configuration, `xfuji.c`, and `fuji-from-tos.py` |
| `patches/x11r6.3-asv.patch` | `Imake.cf` selects `asv.cf` for `-DASV`; host imake passes `-undef`; `servermd.h` takes the AMIX (big-endian) block; the `Xatw` target; `twm` back in the build; `xterm` without utmp, and pushing `ptem`/`ldterm`/`ttcompat` only when missing (ASV autopushes `ptem gls ldterm` onto every pts: a second `ldterm` echoed every line twice) |
| `hw/atw/` | the ddx, ported from `../xserver` to R6's `mieq` event queue and `miPointerScreenFuncRec`; the keymap and the BSD-name shims (`atwCompat.c`) are shared with the R4 server |

## Build

```sh
sh build.sh     # -> work/xc: programs/Xserver/Xatw, lib/*, programs/{xterm,twm,...}
```

Needs gcc-cross-amix at `~/opt/asv-cross` (or `ASV_CROSS=`) with the ASV
sysroot, plus git, curl, python3 and a host gcc and cpp. imake runs on the
PC and generates Makefiles that call the cross compiler. The two
generators that the build runs (`makestrs`, `makekeys`) are compiled with the host gcc.
Built so far: libX11, Xext, Xt, Xaw, Xmu, ICE, SM, Xi, Xtst, Xp, XIE,
oldX and PEX5 as shared libraries (2.1 MB, SONAMEs `libX11.so.6.1` etc.,
plus static archives), the server (static), and xdpyinfo, xclock, xlogo,
xset, xlsfonts, xfd, xrdb, xauth, xdm, xwd,
xterm (XFree86 3.3.6's, with colours; R6.3's as xterm-r63), resize, twm and
xsetroot (15-250 KB each). `work/dist/usr/x11r6` is the
install tree. The clients carry an RPATH of `/usr/x11r6/lib`, so the
system's X11R4 `libX11.so` and friends in `/usr/lib` are left alone.

## The login screen

xdm shows the Atari Fuji from the TOS boot screen above an "Atari System
V" login box, and logs you into an OpenLook session (`../xview`). The Fuji
is Atari's art, so it is not in this repository: `build.sh` takes it from
your own TOS ROM (`TOS=/path/to/tos306us.img sh build.sh`, or a Hatari
install's ROM if there is one). TOS 2.06, 3.06 and 4.04 all carry the
same 96x86 bitmap uncompressed (0x35FE8 in 3.06 US); `fuji-from-tos.py`
finds it by content. `xfuji` waits for the login box and draws the logo,
doubled, in the space above it, shaped (SHAPE extension) so that only the
logo covers the root.

The session (`xdm/Xsession`) opens an xterm and `ttpanel`, a front panel
along the bottom of the screen in the manner of CDE's and IRIX's: a button
per program (terminal, the file manager - Atari's `wish`, not Tcl's - an
editor, a calculator, the
manuals, a load meter, OpenUA) and a clock. Its buttons come from
`~/.ttpanelrc`, one `Label : icon : command` per line (see `xdm/ttpanel.c`
for the icons). The OpenLook Workspace menu (right button on the
background) has the terminal and the file manager too.

Start it with `/usr/x11r6/bin/xdm -config /usr/x11r6/lib/X11/xdm/xdm-config`
(from an rc script, instead of the system's X11R4 xdm). Two details:
resources load through `xrdb -nocpp` (there may be no cpp), and `Xstartup`
copies the server's cookie into the user's `.Xauthority`: xdm keys its
user entries by local interface addresses it cannot list on ASV, and left
the file empty, so every client in the session was refused.

## Run

```sh
Xatw :0 -mode 1024x768 &      # -ac to allow any host while testing
DISPLAY=noname:0 twm &        # or the system's own R4 clients
Xatw -listmodes               # the modes this card offers; safe while X runs
Xatw :0 -mode 1024x768 -depth 32 &   # 24-bit colour (4 MB card)
```

### Two servers, Ctrl+Alt+F1..F9

Atari System V has no virtual terminals for the card, so switchable
servers arrange it among themselves (`hw/atw/atwVt.c`): each is started
with `-vt N` and its own `-fboffset` (where its screen sits in video
memory), and Ctrl+Alt+F`N` switches, as under XFree86. A server keeps
drawing into its own screen while hidden, so a switch copies and redraws
nothing: the one taking over sets its mode and display start and loads its
colours. Only the server on show touches the hardware - the registers, the
LUT, the 2D engine and `/dev/ikbd`; the others draw with the CPU. The
servers find each other through `/tmp/.atwvt` (which is on show) and
`/tmp/.atwvtN` (server N's pid), and pass the card on with SIGUSR2. A
server that exits while on show hands the card back to the one it took it
from.

The display start is in bytes and is taken only as the timing generator
goes from off to on (measured with `tools/atw/atwstart`; writing it alone
does nothing), so each switch blanks the screen for a moment.

```sh
# xdm's Xservers: the desktop as F1
noname:0 local /usr/x11r6/bin/Xatw :0 -mode 1024x768 -depth 32 -vt 1
# a game full screen at 640x480 as F2, the server gone when it exits
xatwrun -mode 640x480 /work/openua/openua
```

`xatwrun.sh` starts the second server in video memory just past the
desktop's screen and its 2D-engine fill source, which follows each screen
(it reads where that is from the desktop's `_ATW_FRAMEBUFFER`): 832 KB in
under an 8-bit 1024x768 desktop, 3 MB + 64 KB under a 32-bit one, where a
640x480 or 800x600 8-bit server still fits the 4 MB card.
Tested in Hatari: switching both ways with the screens intact (an `xclock`
kept drawing on the hidden one), the keyboard back on F1 afterwards, and
the card handed back when the F2 server exits.

### Modes and the 4 MB card

Built in are the VESA 60 Hz modes 640x480, 800x600, 1024x768 and
1280x1024 at 8 bpp, and the first three at 32 bpp; the PLL settings are
computed from each mode's pixel clock (`atwPll` in `hw/atw/atwInit.c`).

At startup Xatw finds the card's memory (measured on a real card). With both
jumpers A0/A1 closed (TT, CPLD firmware 2+) the card decodes 4 MB at
0xFEA00000 but starts in a 2 MB layout, the upper half mirroring the
lower; register 15 = 3 switches to the full 4 MB, with the FPGA structures
at the top of it. A mode that does not fit the card is refused.

Depths: 8 bpp PseudoColor through the LUT (the default), or `-depth 32`:
TrueColor over cfb32, at the modes offered in 32 bit (up to 1024x768,
which needs the 4 MB card). The card's 32 bpp pixel is the bytes
R, G, B, x (measured, `tools/atw/`), which the 68030 reads as 0xRRGGBBxx:
legal for a depth 32 visual (masks 0xff000000 / 0xff0000 / 0xff00) but
not for depth 24, whose colour must sit in the low 24 bits - so the screen
is depth 32 and cfb32 draws straight into the card. R6.3 clients, XView,
olvwm and OpenUA all run on it. 16 bpp is not offered: the card's
little-endian RGB565 splits green across both bytes from the 68030's side,
which no TrueColor visual can describe; it would need a shadow framebuffer.

The card's 2D engine does on-screen copies (moving windows, scrolling)
and solid fills (window backgrounds, clears) at 8 and 32 bpp:
`hw/atw/atwAccel.c`, called from cfb through two hooks the patch adds
(`cfbScreenBlitHook`, `cfbScreenFillHook` in `cfb.h`; NULL in any other
server). Measured on a V0205 card the engine is ~40x the CPU over the VME
bus. `-noaccel` turns it off; in Hatari a scripted scene of fills,
overlapping scrolls and window moves renders pixel-identical with and
without it (and a deliberately broken copy direction does not).

Fonts: `build.sh` compiles R6.3's BDF sources (misc, 75dpi, 100dpi: 472
fonts, 15 MB) to PCF with the host's `bdftopcf`/`mkfontdir`; the server's
default path puts them first and keeps the system's X11R4 SNF fonts after
them, and uses the system's `rgb` database. Everything ships as
`work/x11r6-asv.tar` and `work/x11r6-fonts-asv.tar`, in the V7 tar format
(ASV's `tar` cannot read GNU archives) and each under 16 MB (the most a
process may write by default: a larger file is cut off there):
`cd / && tar xf x11r6-asv.tar && tar xf x11r6-fonts-asv.tar`. Tested in Hatari with the emulated card. The first
session was R6.3 end to end: `xsetroot`, `twm`, `xterm` (typing, with
`$TERM` passed to the shell), `xclock` and `xlogo`. `xdpyinfo` reports
vendor release 6300. The system's R4 clients and a modern `xlogo` from the
PC also work against the server.

## xterm with colours: XFree86 3.3.6's

R6.3's xterm has no ANSI colours: they came with XFree86's xterm. `build.sh`
builds XFree86 3.3.6's (XFree86 3.3 was based on R6.3, so its xterm drops
into this tree) as `xterm`, and keeps R6.3's as `xterm-r63`. The source is
`X336src-1.tgz` from ftp.xfree86.org, pinned by SHA-256. That hash was
checked against the copy inside Debian's archived `xfree86-1` 3.3.6 source,
which snapshot.debian.org serves over HTTPS; ftp.xfree86.org has only HTTP.

What it took (`xterm-quote-includes.py`, then `patches/xterm-xf86-asv.patch`):

- xterm's own headers are included with quotes. It writes `#include
  <menu.h>`, and the cross compiler finds SVR4's curses `<menu.h>` first.
- The same ASV changes as R6.3's xterm: no utmp, `environ` as `_environ`,
  and `ptem`/`ldterm`/`ttcompat` pushed only when missing.
- `sigsetjmp` becomes `setjmp`. SVR4 hides `sigjmp_buf` from a strict ANSI
  compiler, and ASV's shared libc has no `siglongjmp`. The one use is a
  timeout in a `signal()` handler, which leaves nothing blocked.
- No `setegid` in the shared libc: the saved-IDs code is off, since this
  xterm isn't setuid.
- The 16 colours are on without app-defaults. XFree86's `XTerm-color`
  values are compiled in as `rgb:` numbers, so no colour database is
  needed; resources still override them.
- TERM is `xterm-xfree86` when the terminfo has it (the first name tried),
  else `xterm` as before.

The install tree has the app-defaults (`lib/X11/app-defaults/XTerm`,
`XTerm-color`) and `lib/terminfo/xterm-xf86.ti`: XFree86's `xterm-xfree86`
(alias `xterm-new`), `xterm-color`, `xterm-16color`, `xterm-vt220` and
`xterm-r6`. Compile them once on the TT, as root:

```
tic /usr/x11r6/lib/terminfo/xterm-xf86.ti     # 4 warnings: meml/memu, harmless
```

The system's own `xterm` entry is not replaced, so the R4 and R6.3 xterms
and anything else that says TERM=xterm behave as before. Anyone who
telnets in from a colour terminal can use `TERM=xterm-color`.

Three things that looked like bugs and weren't:
- `xwd` of an idle display gives solid black. After 10 minutes the screen
  saver covers the screen with a black window; `xset s reset` wakes it.
- The system's R4 `xwd` can't open `:0` (see ../xserver); `build.sh`
  builds R6.3's.
- xterm ignores SIGTERM on System V, so that the shell's process group
  can't kill it. Close it from the window manager, or send SIGKILL.

## Why a shadow sysroot

- **`__STDC__`**: ASV's headers were written for AT&T cc in `-Xa` mode,
  where `__STDC__` is 0. gcc sets it to 1, so every
  `#if __STDC__ - 0 == 0` block disappears: `sigset_t` (and with it
  `<setjmp.h>`), and many extended declarations. A native gcc install gets
  this rewritten by fixincludes; `mksysroot.sh` does the same to a copy of
  `usr/include` (the other directories are symlinks), and `asv.cf` points
  the wrapper at it with `AMIX_SYSROOT`. `m68k` stays defined and there is
  no `-ansi`: the headers lay out `regset`/`ucontext`/`jmp_buf` by `m68k`.
  (`-D__STDC__=0` instead would also switch X's own sources to their K&R
  paths.)
- **stdio**: `libc.so.1` works on its own `__iob`. Any reference to `__iob`
  from an executable, even through the GOT, makes GNU ld copy the table into
  the program, which leaves the program and libc with two `stdout`s over one
  buffer. Writes to stderr fail with `EINVAL`, and `printf` mixed with
  `putchar` scrambles the output. (With `m68k` defined, `stdio.h` names 16-byte aliases,
  `__stderrb`, which fail the same way.) The fixed `stdio.h` defines the
  three streams as `__asv_iob()[n]`, and `asviob.c` asks the dynamic
  linker for libc's `__iob` (`_dlsym`), so the executable never names it.
  This applies to anything built with gcc-cross-amix for ASV.

## Shared libraries

- gcc-cross-amix needs its PIC fixes: gcc's SGS output labels string
  constants `LC%N` in PIC code and writes calls as a sizeless
  `bsr foo@PLTPC`. GNU as rejects the first and makes the second a
  16-bit branch, too short for libX11. The wrapper's `fix_asm` rewrites
  both; `build.sh` refuses to run without it.
- `-fPIC`, not `-fpic`, and `ld -shared`, not the SVR4 `-G -z text`.
- **No DT_NEEDED in our libraries**, like ASV's own: the system's
  `dlopen`/`dlsym` (in `libc.so.1`, which is also the dynamic linker)
  fault as soon as any loaded library has NEEDED entries. They seem to read
  the names with the wrong string table. Clients name every library they
  use (imake's client macros already do; `XMULIB` also pulls in Xt).
- `-rpath-link` to the build's library directory lets GNU ld check
  the libraries' symbols at link time.
- Copy relocations of the libraries' data (widget class records,
  `XtStrings`) are fine; the data the dynamic linker relocates is copied
  after it is relocated. `errno` and `environ` from libc are fine as well;
  only `__iob` is special (see above).

## Other traps

- The wrapper passes `-ansi` on to `ld`; asv.cf does not use it anyway.
- ASV's libc exports the BSD calls only as `_abi_*`, and `libsocket`/`libnsl`
  need the plain names: `asvcompat.o` (the R4 server's `atwCompat.c`) goes
  into every link. So do `libc.a`'s `fpset*` objects that `libm` needs, as
  plain objects, because the wrapper orders archives before `-l` libraries.
- utmp(x) functions and `environ` exist only in the static `libc.a`. The
  shared libc exports `_environ`, so xterm is built with
  `-Denviron=_environ` and without `-DUTMP` for now.
- `fixneeded.py` must run after the last link: a later `make` relinks and
  brings back the host library paths.
- Much of the system's C library exists only in the static `libc.a`:
  `setitimer`, `sys_errlist`, `cfree`, `crypt`, the shadow and utmp calls,
  libm's `fpset` helpers. `libcextra.py` picks those members and whatever
  they need in turn (stopping at what `libc.so.1` exports), and `build.sh`
  joins them into one `libcextra.o` (`ld -r`) that every program links.
  Two members are left out on purpose: `libc.a`'s `syscall()` and `vfork()`
  report errors through a libc-internal routine that corrupts `errno` when
  called from a program. `asvcompat.o` provides them instead, over libc's
  `_abi_syscall`, and turns the kernel's internal ERESTART into EINTR.
- Return values: see `../xview/README.md` (AMIX_RETURN_D0_TO_A0); the X
  tree is built with it too.

## Next

`resize` is built too. On SVR4 it prints nothing: it sets the tty's window
size (`TIOCSWINSZ`) from xterm's cursor report, which is where SVR4 programs
look. (`resize -s` is Sun emulation, for cmdtool, and times out in xterm.)
(xterm used to echo every typed line: it
pushed a second `ldterm` onto ASV's autopushed pts stack. `strconf` inside
the window shows the stack.) xdm replaces the system's R4 xdm only when started
by hand or from your own rc script.
