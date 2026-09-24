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
xset, xlsfonts, xfd, xrdb, xauth, xdm,
xterm, resize, twm and xsetroot (15-200 KB each). `work/dist/usr/x11r6` is the
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
```

### Modes and the 4 MB card

Built in are the VESA 60 Hz modes 640x480, 800x600, 1024x768 and
1280x1024 at 8 bpp, and the first three at 32 bpp; the PLL settings are
computed from each mode's pixel clock (`atwPll` in `hw/atw/atwInit.c`).

At startup Xatw finds the card's memory (measured on a real card). With both
jumpers A0/A1 closed (TT, CPLD firmware 2+) the card decodes 4 MB at
0xFEA00000 but starts in a 2 MB layout, the upper half mirroring the
lower; register 15 = 3 switches to the full 4 MB, with the FPGA structures
at the top of it. A mode that does not fit the card is refused.

Only 8 bpp for now. The 16/32 bpp modes need their pixel byte order
confirmed on a real card first: the manual gives 16 bpp as a little-endian
RGB565 word, which from the 68030's side splits green across both bytes -
not something cfb16 can draw directly. `tools/atw/` has the test programs.

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
