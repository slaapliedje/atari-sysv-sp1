# AMIX programs on Atari System V

Commodore's Amiga UNIX (AMIX 2.1) and Atari System V are both SVR4.0 for the
68030, with the same system call numbers, so AMIX's binaries can run on the
TT. This directory, with the kernel module in `../driver-amix`, makes that
work: AMIX's own programs (ls, sh, awk, sed, ps, cpio, stty…), the
community's AMIX builds (GNU grep, gzip, tar, perl 4), and anything
cross-built for AMIX. That includes **GNU bash 5.2**, built here.

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

Nothing that comes out of the AMIX packages belongs in a repository or on a
public server. That includes a static bash, which carries code from AMIX's
`libc.a`.

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
