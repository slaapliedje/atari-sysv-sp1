# Xatw — X11R4 server for the ATW800/2 under Atari System V

A new ddx for the MIT X11R4 server: the ATW800/2 "Seurat" card as an
8-bit-per-pixel PseudoColor framebuffer (cfb), mapped through `/dev/mem`,
its hardware LUT driven by the installed colormap; keyboard and mouse from
`/dev/ikbd`, the raw IKBD stream the stock `XatariServer` also reads. The
TT's own screen keeps the console; X lives on the card's monitor.

VESA 60 Hz timings: `-mode 640x480` (default), `800x600`, `1024x768`,
`1280x1024`.

## Building (on the PC, with gcc-cross-amix)

1. Unpack MIT X11R4 tape-1 (xorg.freedesktop.org/archive/X11R4/) to
   `../mit` and apply `patches/x11r4-atari.patch` (SVR4 `sys/time.h`, an
   `atari` block in `servermd.h`, a typo in `mispritest.h`, two SVR4
   headers).
2. `ln -s include ../mit/X11; ln -s ../extensions/include ../mit/include/extensions`
3. The cross sysroot needs, from the image: `/usr/ccs/lib/{libgen.a,libm.a,libc.a}`
   in `usr/lib`, and `/usr/ucbinclude/{ndbm.h,dbm.h}` + `/usr/ucblib/libucb.a`
   (`tools/ufs.py … get`). The Makefile links libc's `fpset*` objects and
   libucb's `ndbm.o` explicitly.
4. `make` → `Xatw` (~560 KB). `fixneeded.py` runs at the end: the cross
   wrapper hands `ld` absolute library paths and ASV's shared libraries
   have no SONAME, so the NEEDED entries are rewritten to bare names.

`atwCompat.c` provides the BSD names the server and ASV's libsocket use
(`select`, `gettimeofday`, `bcopy`, …) over libc's `_abi_*` exports —
ASV's `select` writes back a 1024-bit fd_set, so it is marshalled.

## Running

```
Xatw :0 &                    # on the TT
DISPLAY=noname:0 xterm &     # clients over TCP; put client hosts in /etc/X0.hosts
```

Use the machine's name (`noname:0`), not `:0`: the unix-domain path the
R4 os layer creates on SVR4 does not take connections yet.
