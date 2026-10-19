# AMIX programs on Atari System V

Commodore's Amiga UNIX (AMIX 2.1) and Atari System V are both SVR4.0 for the
68030, with the same system call numbers, so AMIX's binaries can run on the
TT. This directory, with the kernel module in `../driver-amix`, makes that
work: AMIX's own programs (ls, sh, awk, sed, ps, cpio, stty…), the
community's AMIX builds (GNU grep, gzip, tar, perl 4), and anything
cross-built for AMIX. That includes **GNU bash 5.2** and **Vim 9.2**, built
here.

## What differs, and where it is handled

The two systems use different kernel ABIs. ASV follows the 88open BCS, while
AMIX is plain AT&T SVR4:

| | AMIX | ASV |
|---|---|---|
| system call | `trap #0`, arguments on the stack | `trap #10`, arguments in a0,d1,a1,d2… |
| signals 21-22, 28-31 | URG, POLL, VTALRM, PROF, XCPU, XFSZ | 33-38 (and 21 unused) |
| signal sets | 4 words, `1 << (n-1)` | 2 words, most significant bit first |
| `struct sigaction` | flags, handler, mask, 2 spare | handler, mask, flags; other `SA_*` bits |
| `siginfo` | signo, code, errno | signo, errno, code |
| `struct stat` (xstat v2) | SVR4's 136 bytes | 512 bytes, fields packed differently |
| `struct termios` | `c_cc` after the four flags | a pad byte first (BCS) |
| `utsname` | 257-byte fields | 256 |
| `ucontext` | 16-byte `uc_sigmask` | 8 bytes, so `uc_mcontext` moves |
| errno | networking errors 93-151 | 128-162, 235-242 |
| `_PC_*` | NO_TRUNC 7, VDISABLE 8, CHOWN_RESTRICTED 9 | 8, 9, 7 |

The kernel module `amx` (`../driver-amix/amx.c`) translates all of this at
the kernel boundary, like iBCS on Linux. ASV's kernel dispatches every trap
of an ELF process through a per-process table (`p_syscallops`). The module
fills its `trap #0` slot with a gate, and gives AMIX processes their own copy
of the table, whose signal-delivery entry fixes up the handler's frame. The
gate moves the stack arguments into the registers that ASV's own `trap #10`
path reads, then runs that path. Restart, `/proc`, tracing and signals are
therefore ASV's code. Structures the kernel writes back go to scratch space
below the user's stack pointer and are converted into the caller's buffer.

Native programs are not affected. ASV's libc never enters through `trap #0`;
its one stub that did got SIGSYS. Not translated yet: the System V IPC
`*_ds` structures (padding differs) and `I_RECVFD`.

The library side is simpler. A dynamically linked AMIX program brings its
own libc. `libc.so.1` is also the dynamic linker, and AMIX's libc lives in
`/usr/amx`. `amixify.py` rewrites a program's interpreter path to point
there. The same script patches AMIX's libc so it knows its own new name
(otherwise it loads itself twice, and malloc finds no heap) and searches
`/usr/amx` rather than `/usr/lib` for libraries. Static AMIX programs (such
as bash below) need neither.

## Install

1. The kernel module, on the TT as root, with `../driver-amix` copied over:

   ```
   sh install.sh        # cc, mkboot, INCLUDE:AMX, buildsys
   init 6
   ```

2. AMIX's packages, on the PC. `mksysroot.sh` unpacks `core-2.1.pkg` and
   `Cdev-2.1.pkg` from Commodore's AMIX 2.1. With `FETCH=1` it downloads
   them from the AMIX package server <https://pkg.amigaux.org> and checks
   them against its catalog. It builds `work/sysroot` (for the
   cross-compiler) and `work/amx.tar`, AMIX's shared libraries patched for
   `/usr/amx`:

   ```sh
   FETCH=1 sh mksysroot.sh          # -> work/sysroot, work/amx.tar
   ```

   On the TT: `cd /usr && tar xf amx.tar`.

3. AMIX programs: `python3 amixify.py PROGRAM…` on the PC before copying
   them over (static programs need nothing). `unpkg.py PKG DIR` unpacks any
   AMIX package datastream. It walks the cpio headers rather than searching
   for the trailer, because AMIX's own `cpio` binary contains the string
   `TRAILER!!!`.

4. Packages from pkg.amigaux.org: see [apkg](#packages-apkg) below.

Nothing that comes out of the AMIX packages belongs in a repository or on a
public server. That includes a static bash or vim, which carry code from
AMIX's `libc.a`.

## Packages: apkg

`apkg` is the AMIX community's package client. It fetches packages from
<http://pkg.amigaux.org> and installs them with its own SVR4 package engine
(`APKGENG`: pkgadd, pkgrm, pkgchk…). Both are dynamically linked AMIX
programs, so they run on the TT once they point at `/usr/amx`, and so do the
programs they install. Three pieces here do that on the TT itself:

- `amixify.c` is `amixify.py` in C (the same bytes out, and a second run
  changes nothing). `-q` skips anything that is not a 68k ELF file.
- `apkg-pkgadd` is the `pkgadd` that `apkg` runs. It unpacks the package,
  refuses it if it would install anything outside `/opt/amix` or apkg's own
  files, amixifies its programs and puts their new checksums in the pkgmap,
  and only then hands it to the engine. Afterwards it sets owners and
  groups, which the engine leaves as root's. The installed-software
  database therefore matches the files on disk, and `pkgchk` agrees.
- `apkg-pkgrm` is the `pkgrm` that `apkg` runs. It refuses to remove a
  package with files outside `/opt/amix`: ASV has packages of its own called
  `core`, `lp`, `man` and `sysadm`, the names of AMIX's stock packages, and
  `apkg list` shows them as installed.

To install, get `APKGENG-1.12.0.pkg` and `APKG-0.2.2.pkg` from the server
(`base/` in its `catalog`, which also has their MD5 sums). Then, as root on
the TT, in a directory with them, `amixify.c`, `apkg-pkgadd`, `apkg-pkgrm` and
`apkg-setup.sh`:

```sh
sh apkg-setup.sh APKGENG-1.12.0.pkg APKG-0.2.2.pkg   # [NAMESERVER]
apkg update
apkg install gzip less
PATH=/opt/amix/bin:$PATH
```

`apkg-setup.sh` builds `amixify` with the TT's `cc` into `/usr/amx/bin`,
next to the two hooks. ASV's own `pkgadd` installs the engine, and the engine
then installs `apkg`. Finally it adds `pkgadd=`, `pkgrm=` and `nameserver=`
to `/etc/apkg.conf`, the last taken from `/etc/resolv.conf`. AMIX has no
resolver, so apkg asks that server itself. Packages are unpacked in
`$TMPDIR` (default `/var/tmp`); set it to a roomier filesystem if `/var` is
small.

What installs: the community's ports, which live in `/opt/amix`. That means
gzip, less, grep, make, tar, zlib and amix-benchmark. What is refused: AMIX's
stock packages (`core`, `bsd`, `text`, `xcore`…), which would replace ASV's
own commands and libraries. Also refused are `patch` and `acompat`, which
write to `/usr/bin` and `/usr/local`, where `acompat`'s `stdint.h` would sit
in front of the TT's gcc, and `cdfs`, an AMIX filesystem driver.
`APKG_ANYWHERE=1` overrides either hook.

Caveats:

- apkg fetches over plain HTTP. The catalog's MD5 sums catch damaged
  downloads, not altered ones.
- Neither `installf -f` survives on the TT: ASV's dies with a bus error, and
  APKGENG's with a segmentation fault on some packages. That is why
  `apkg-pkgadd` fixes packages before they are installed, not after.
- `pkgchk APKG` reports `/etc/apkg.conf` as changed, as it would after any
  edit: `apkg-setup.sh` adds its lines to it.
- APKGENG's `pkgchk` can crash (segmentation fault) while reporting a
  directory whose group differs from the package's. It prints the first
  mismatch, then a group of `(null)` for the next. A package installed with
  `APKG_ANYWHERE=1` into existing directories such as `/usr/local/bin`
  leaves exactly such differences; `chgrp` the directories to what the
  package declares (`sys`) and `pkgchk` passes.
- The TT's own compiler is GCC 1.40, which knows `signed` only with
  `-ansi`. acompat's `stdint.h`, written for the AMIX cross-compiler, needs
  `cc -ansi` there. With it, `stdint.h` (`long long` included), `strings.h`
  and `-lacompat` all work.

## AMIX itself, in an emulator

`emu/` runs AMIX 2.1 in an emulated A3000, headless, for testing AMIX
programs on the real system: `emu/uaeamix.py` starts it, and then runs
commands over telnet and copies files over ftp. See `emu/README.md`.

## bash

```sh
AMIX_SYSROOT=../work/sysroot sh bash/build.sh     # -> bash/work/bash
```

This builds GNU bash 5.2.37 (the tarball's SHA-256 is pinned) as a static
AMIX program: about 830 KB stripped, and it needs no libraries on the TT.
Copy it to, say, `/usr/local/bin/bash`. It works in scripts and
interactively: line editing, history, completion, job control
(Ctrl-Z/`bg`/`fg`), traps, coprocesses, arrays and extended globs.

What it took:

- **Link statically against `libc.a`.** `bash/amix-static-cc` drives the
  link: gcc-cross-amix links only against `libc.so.1`, which, as on ASV,
  lacks much of the static library (`select`, `getpwent`, `siglongjmp`…).
  Because configure runs with it too, it sees what the link will see. A
  configure run against the shared libc concluded there was no `getpwent`,
  so bash never called `endpwent()` and `~user` stopped expanding.
- **Keep the host tools apart.** `bash/hostfix.h` is forced into the
  build-host tools (`mksignames`, `mksyntax`…). They include the target's
  `config.h`, whose `#define ssize_t int` and the like collide with the PC's
  typedefs.
- **Patch bash** (`bash/bash-5.2.37-amix.patch`, 11 files, all where bash
  meets a system without `<stdint.h>`, `gettimeofday()` or multibyte
  support). The fixes cover a missing `INTMAX_MIN`, the prototypes of the
  `getcwd`/`gethostname` replacements, `sigset_t` for the pselect prototype,
  `PARAMS` before `stdc.h`, and a multibyte-only line in `parse.y`.
- **Fix the wrapper.** gcc-cross-amix put `-c dir/foo.c`'s object next to
  the source instead of in the current directory, which broke bash's
  out-of-tree build. This is fixed in the wrapper.

Time comes from `gettimeofday()`, which AMIX's libc implements with
`hrtcntl()`. On ASV that ticks at 1/128 s. A short `time sleep 1` is ASV's
own `sleep`, whose `alarm` wakes at the next whole second.

## vim

```sh
AMIX_SYSROOT=../work/sysroot sh vim/build.sh
# -> vim/work/vim, and vim/work/vim-9.2.1189-amix.tar to unpack in /
```

This builds Vim 9.2.1189 (the tarball's SHA-256 is pinned) with the
"normal" features and no GUI, as a static AMIX program: 2.9 MB stripped.
The tar holds it with its runtime files, a system vimrc and a quicker
colouring of mail, all under `/usr/local` (26 MB unpacked; no spelling
dictionaries, translations or tutor). Manx's `manxmail` writes mail in it. The tar is over the 16 MB
that ASV lets a process write to a file (`ulimit -f`, ftpd's too), so
bring it over gzipped (8 MB) and unpack it with apkg's gzip:
`cd / && /opt/amix/bin/gzip -dc vim-9.2.1189-amix.tar.gz | tar xf -`.

What it took:

- **A C99 compiler.** vim 9 needs C99, and gcc-cross-amix's gcc 2.7.2 is
  C89. `vim/amix-cc` compiles with Manx's `toolchain/sysv4-cc` (the mint
  GCC 15 to SVR4 assembly, gcc-cross-amix's assembler) and links with its
  `sysv4-ld`, so `MANX=` must name a checkout of Manx (default
  `~/dev/unix-browser`). configure runs with it, and is told what it can't
  run on the PC through cache variables. It also finds the PC's Wayland
  through pkg-config and `/proc/self/exe` on the PC's disk; both are kept
  out.
- **Doubles come back in fp0.** The mint GCC returns a double in d0:d1
  even with `-m68881`, but AMIX's libc and libm return one in fp0. So
  every float literal read as 0, and the math functions returned garbage.
  `vim/rt.c` wraps the 19 that vim calls (`strtod`, `sqrt`, `pow`, the
  trigonometry...) to move fp0 to d0:d1, and `vim/amixfix.h`, forced
  ahead of each source, renames vim's calls to them.
- **No `%lld`.** AMIX's printf has no `ll` length, and vim formats each
  number with `sprintf(buf, "%lld", n)`. `rt.c`'s `sprintf` does
  `%ll[duoxX]` itself and passes the rest to AMIX's `vsprintf`.
- **What SVR4.0 doesn't have**: prototypes for `getrlimit`, `select` and
  `fchmod`, `ssize_t` (`amixfix.h`), and `trunc` (`rt.c`, the FPU's
  `fintrz`). configure's own tests run without `amixfix.h`, since they
  declare each function as `char f();` to see whether it links.
- **Memory.** AMIX and ASV give a process 16 MB of address space. When a
  file is opened, vim compiles the file name patterns of `filetype.vim`
  (over a thousand); as programs of its NFA regexp engine they take some
  18 MB, which brought "E342: Out of memory" and then crashes. The system
  vimrc (`vim/vimrc`) picks the backtracking engine instead: about 4 MB
  in all with syntax highlighting on.
- **UTF-8 inside, Latin-1 outside.** Neither system has a UTF-8 locale.
  The vimrc sets 'encoding' to utf-8 (mail is UTF-8) and leaves
  'termencoding' at what the locale says, Latin-1, which vim converts to
  and from itself.
- **Colouring on a 68030.** On the TT a screen of quoted mail took 3-4.5 s
  to colour, past 'redrawtime' (2 s), after which vim stops colouring the
  buffer; a jump to the end of a long mail took 8 s. `vim/mail.vim`,
  installed as `vimfiles/after/syntax/mail.vim`, drops mail.vim's emoji
  pattern on a Latin-1 terminal (it always runs on the NFA engine: 2.6 s
  of 4.4), tries addresses only where a word starts, and reads back 30
  lines after a jump instead of 100. The vimrc gives a redraw 6 s. A
  screen of quoted mail now takes 2-3 s, a jump 4 s. Over telnet, colours
  need a TERM that has them: ASV's terminfo `xterm` has none, while
  `xterm-xfree86` (its XFree86 xterm's) and `xterm-color` have 8.
- **The tar.** ASV's tar is from 1991: the archive is ustar with every name
  under 100 bytes, and its files have owners named `root`. AMIX's tar
  crashed on empty owner names.

The assembler warns "Bignum too big for f format; truncated" for negative
single-precision constants. That warning is harmless: the bits come out
right.

## mtools

GNU mtools 4.0.49 as a static AMIX program, for the USB stick's FAT file
system (ASV has no FAT driver): `amix/mtools/build.sh`, built like vim
(the mint GCC through Manx's sysv4-cc, vim's amix-cc wrapper, rt.a with
Manx's snprintf and a usleep; amixfix.h ahead of each source for the
prototypes AMIX's headers lack and S_ISLNK, after configure, whose
`char f();` probes it would contradict; SVR4's `-lucb` taken out of the
link).  Output: work/mtools-4.0.49-asv.tar, unpacked in / on the TT:
/usr/local/bin/mtools with its 24 command names hard-linked, and
/usr/local/etc/mtools.conf.

A drive type of its own, `ub:N` (ub_io.c, hooked into OpenImage): the
kernel USB driver's storage unit N read and written by block number
through /dev/ub0's UBIOC_RD/UBIOC_WR, since a 32-bit lseek caps
/dev/rdsk/usbN at 2 GB and a 16 GB stick's FAT32 lives beyond it.
mtools.conf: `drive u: file="ub:0" partition=1` (and v: the raw device,
first 2 GB).  Needs `ubtool daemon` running.  On the TT 2026-10-19:
mdir/minfo/mtype/mcopy/mdu/mshowfat all work on a 16 GB FlashFloppy
stick (15.6 GB free shown, 64-bit offsets in order); a 170 KB file
copied off it matched the public FlashFloppy 3.43 release byte for byte;
43 KB/s.  Writes untested (the stick is the user's).
