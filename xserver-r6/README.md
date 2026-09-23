# Xatw on X11R6.3

The ATW800/2 server moved from MIT X11R4 onto X11R6.3, on top of
isoriano1968's [X11R6.3 port for AMIX](https://github.com/isoriano1968/x11r6.3-amix)
(Amiga UNIX is the same UniSoft SVR4/m68k and uses the same gcc-cross-amix
toolchain). This directory holds only what Atari System V adds:

| Path | What |
|---|---|
| `config/asv.cf` | imake platform file: cross compiler, static server over TCP, no XKB/SHM/PEX/XIE/LBX/Xprint/Xnest |
| `patches/x11r6.3-asv.patch` | `Imake.cf` selects `asv.cf` for `-DASV`; host imake passes `-undef` so the PC's predefines stay out; `servermd.h` takes the AMIX (big-endian, MSB-first) block; `Xatw` server target |
| `hw/atw/` | the ddx, ported from `../xserver`: R6's `mieq` event queue and `miPointerScreenFuncRec`; keymap and libc shims are shared with the R4 server |
| `build.sh` | fetch the pinned overlay, let its `install.sh` download and verify the X.Org sources, apply the above, build `Xatw` |

## Build

```sh
sh build.sh            # -> work/xc/programs/Xserver/Xatw (~920 KB)
```

Needs gcc-cross-amix at `~/opt/asv-cross` (or `ASV_CROSS=`), with the
ASV sysroot, and a host gcc, cpp, curl and python3. imake runs on the PC
and generates Makefiles that call the cross compiler. The `fpset*`
objects `libm` needs are extracted from the sysroot's `libc.a` during the build.

## Run

```sh
Xatw :0 -mode 1024x768 &      # add -ac to allow any host while testing
DISPLAY=noname:0 xterm &
```

Until R6 fonts are installed, the server uses the system's X11R4 SNF
fonts and `rgb` database (`DefaultFontPath` in `asv.cf`). Tested in
Hatari with the emulated card: xdpyinfo from the PC reports vendor release
6300 with BIG-REQUESTS, DOUBLE-BUFFER, SHAPE, SYNC, XC-MISC and XTEST. The
system's own R4 twm, xterm and xclock, and a modern xlogo from the PC over
TCP, all run. Keyboard input and focus-follows-mouse through the IKBD both work.

## Traps found on the way

- **Never define `m68k` when compiling for ASV.** With `m68k` set, ASV's
  `<stdio.h>` maps `stderr` to `__stderrb`, a copy that libc's stdio never
  updates. Every write to stderr then fails with `EINVAL` and a garbage
  length, so the server exits with status 1 and no message. This affects
  anything built with gcc-cross-amix for ASV; `asv.cf` passes `-Um68k`.
- The cross wrapper passes `-ansi` on to `ld`, which rejects it, so
  `asv.cf` keeps `-ansi` in the defines (compile only), not in `CCOPTIONS`.
- R6.3 builds `Xprt`, `Xnest` and `Xvfb` by default; the last two need
  the client libraries, which are the next stage.

## Next

The client side: Xlib/Xt/Xaw/Xmu for ASV. The host helper tools
(`makestrs`, `makekeys`) must be built with the host compiler. Then XView
and olwm for OpenLook.
