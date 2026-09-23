# Atari System V — Service Pack 1 (unofficial)

Licensed under the GNU General Public License v2 (see LICENSE). The network
driver's DLPI half was re-created from the disassembly of the stock UniSoft
`la` driver so that it behaves identically toward the TCP/IP stack; the code
itself is original.

Fixes and additions for Atari System V Release 4.0 (UniSoft SVR4, 1991) on a
TT030 fitted with modern hardware. Developed and tested on a TT030 with a
256 MB TT-RAM board, an ATW800/2 graphics card and a ZuluSCSI Blaster.

Nothing here contains Atari or UniSoft code. You need your own ASV disk image
(Richard's `asv_boot.img` from atariunix.com is the one this was built on).

## What's in it

| Directory | What | Why |
|---|---|---|
| `kernel/` | `patch-asv-image.py` — patches a stock image on the PC before writing it to the disk | Stock kernels reset-loop with more than ~100 MB of RAM, and hang if an ATW800/2 is fitted (its video RAM answers the ethernet-card probe) |
| `driver-dp/` | `dp.c`, a kernel driver for the DaynaPORT SCSI/Link ethernet emulation of ZuluSCSI / BlueSCSI | ASV's only network driver is for the Riebl VME card; this gives TCP/IP (telnet, ftp, rsh, NFS) with no VME slot needed |
| `atw800/` | `atwfb.c` — sets 640x480x256 on the ATW800/2 and draws a test pattern, from user space via `/dev/mem` | First light for the card under Unix; the basis for any further driver work |
| `disk/` | `mklabel.py` — makes the label sectors for a new data disk | ASV's own `format` tool cannot label a blank disk (its description-file parser is broken) |
| `tools/` | `ufs.py` (read/patch files inside an ASV image on the PC), `asvsh.py` / `asvput.py` (run commands as root over telnet / push files over ftp as `dev`; both take the machine's address in `ASV_HOST` and the passwords from `.asvpass` / `.asvpass-dev` beside them, never committed) | Working on the machine without a floppy drive |
| `xserver/` | `Xatw`, an X11R4 server for the ATW800/2 (cfb, 8-bit PseudoColor, keyboard and mouse from `/dev/ikbd`) | The stock `XatariServer` drives only the TT's own screen |
| `xserver-r6/` | X11R6.3 for ASV: `Xatw` on R6.3, the shared client libraries, R6 fonts, xterm/twm/xdm and friends, and an xdm login screen with the Fuji from the TOS boot screen | A current X: R6 extensions, shared libraries, and the base the OpenLook toolkit needs |
| `xview/` | XView 3.2p1.4 (the OPEN LOOK toolkit) with olwm, cmdtool and clock, on top of `xserver-r6` | Sun's OpenLook desktop on the TT |
| `driver-amix/`, `amix/` | `amx`, a kernel module that runs AMIX (Amiga UNIX) binaries: a `trap #0` gate plus translation of signals, `stat`, termios, errno and the rest of the two ABIs' differences; tools for AMIX's packages and libraries; a build of GNU bash 5.2 | AMIX is the same SVR4 for the 68030, with a larger software collection; and a current bash |
| `doc/` | Design notes for the driver and the kernel facts they rest on | |

## Install

### 1. The image (on the PC, before the first boot)

```
cp asv_boot.img HD0.bin
python3 kernel/patch-asv-image.py HD0.bin          # 96 MB TT-RAM cap + Lance probe move
```

Put `HD0.bin` on the ZuluSCSI card (SCSI id 0). With a 4 MB ST-RAM machine
keep the cap at 96 MB; with 10 MB of ST-RAM the stock kernel needs no cap.
Root has no password. The image boots into run level 4 (xdm); to get a text
login instead, edit `/etc/inittab` (`is:4:` → `is:2:`), which can be done from
the PC with `tools/ufs.py`.

### 2. Networking

On the card: an empty file named `NE4.img` (SCSI id 4 = a DaynaPORT), and
`WiFiSSID` / `WiFiPassword` under `[SCSI]` in `zuluscsi.ini`.

On the TT, as root, with `driver-dp/` copied somewhere:

```
sh install.sh        # cc, mkboot, INCLUDE:DP, buildsys — takes a few minutes
init 6
```

The install re-points `/dev/en0` at the new driver, so the stock network
startup (`slink`, `ifconfig en0`) works unchanged. Set the machine's address
in `/etc/inet/hosts.net` (the `noname` line); it has no DHCP client — DHCP is
two years younger than this system.

### 3. More disk

Add a blank `HD1.bin` to the card (`dd if=/dev/zero … bs=1M count=1024`).
Then, from the PC, `python3 disk/mklabel.py 1024 768 > label.bin`, and on the
TT follow the commands at the top of `mklabel.py`. Add the slices to
`/etc/vfstab`. Do NOT mount over `/usr/local`: it has content on the stock
image.

### 4. AMIX programs and bash (optional)

`driver-amix/` installs like `driver-dp/` (`sh install.sh`, then `init 6`).
Then see `amix/README.md`: AMIX's shared libraries go in `/usr/amx`, AMIX
programs are pointed there with `amixify.py`, and `amix/bash/build.sh`
cross-builds bash 5.2 as a static AMIX program.

## Driver notes (the parts that cost the most to learn)

- The kernel's SCSI core calls a driver's START routine with flag 1 for
  "start the queue head"; the core itself dequeues and `biodone()`s a
  finished request. Flag 0 means "retry the same request", not completion.
- The DaynaPORT raises no interrupt; `dp` polls from a STREAMS service
  procedure at 50 Hz while traffic flows, 10 Hz when idle. Received frames
  carry a 4-byte CRC trailer that the driver strips.
- spl levels on this kernel are absolute MFP masks (`spl4` SCSI,
  `splstr` = `spl5`); a timeout callback should never start SCSI work.

## Running it in Hatari

A stock Hatari boots the kernel and stops after the banner. Nine emulator
bugs (all in code that only a Unix exercises) are fixed in the Hatari
fork this was developed with — branch `et4000` of
<https://github.com/slaapliedje/hatari> (it also carries the ATW800/2 and
DaynaPORT emulation). Fixes 1-5 are open upstream as hatari/hatari PRs
#38-#40 and tonioni/WinUAE #499-#500; until they are merged, build that:

```sh
git clone -b et4000 https://github.com/slaapliedje/hatari.git
cmake -S hatari -B hatari/build && cmake --build hatari/build -j
HATARI=$PWD/hatari/build/src/hatari tools/hatari-asv.sh HD0.bin 256
```


| Symptom | Cause | Fix |
|---|---|---|
| Banner, then nothing | A `MOVES` that page-faults is completed on RTE with the previous instruction's write data (68030 cycle-exact table never recorded its data output buffer); `copyout` of the icode corrupted `moveq #59` and init's exec returned EINVAL | `cpu: 68030 MMU — MOVES records its data output buffer…` |
| Every dynamically linked program segfaults, only with 256 MB TT-RAM | 68030 data-cache burst fill translated with the CPU privilege level, not the access's function code; a `copyin` (`MOVES` SFC=user) filled user-tagged lines from physical memory at the logical address | `cpu: 68030 data cache — burst fill translates with…` |
| First DMA read never completes | TT-MFP interrupt line never dropped after the reset-interrupt register read; no phase-mismatch interrupt when DMA mode is armed late | `ncr5380: TT interrupt line drops…` |
| fsck stalls in a timed wait, `lbolt` stays 0 | The MC146818 periodic interrupt was not emulated; ASV's clock is that interrupt at 128 Hz on TT-MFP GPIP6 | `nvram: MC146818 periodic interrupt…` |
| inetd dies at start, no telnet/ftp | The bus-error handler's "complete the access in software" (read of NULL returns 0, which 4.3BSD's inetd relies on) was discarded by the RTE, so the read re-faulted forever | `cpu: 68030 MMU — keep the access replay state…` |
| Random `PANIC: unexpected kernel trap` and reboot, any time a `copyin` buffer ends 2 bytes before an unmapped kernel page | The RTE that replays a frame $A write left its stage B opcode armed; when the replayed write faulted again the new handler's first instruction was replaced by it and a push was lost, so `trap()` read the vector from the wrong offset | `cpu: 68030 MMU — drop the stage B opcode when an exception is taken` (WinUAE 6115d64, Toni Wilen) |
| Same panic, now as a format error | The nested frame recorded the RTE's own opcode as the instruction to resume; the kernel's RTE then executed an RTE at copyin's PC on an empty stack | `cpu: 68030 MMU — a frame $A RTE leaves the next opcode in IRC…` |
| `SIGBUS` at a PLT call: a `bsr.l` whose displacement straddles a page boundary branches to PC+1 | A prefetch fault deferred behind a branch was never taken when the branch's own displacement word was consumed; it read back as $FFFF | `cpu: 68030 MMU — a deferred prefetch fault is taken when the word is consumed` |
| XFaceMaker2 `SIGSEGV` in `XrmStringToQuark`: an extra, unwritten slot in an argument list | `MOVE.L …,-(SP)` at the last words of a page prefetches the next opcode after the pre-decrement; the fault discarded the register fixup and the restart decremented SP twice | `cpu: 68030 MMU — undo address register fixups when a prefetch fault restarts…` |

Then `tools/hatari-asv.sh HD0.bin 256` (the image is written to; boot a
copy). The stock image, unpatched, boots with `16` and reaches multi-user.
`tools/hdbg.py` drives the debugger through `--cmd-fifo` and reads kernel
memory through the page tables.

Networking: the fork also emulates a DaynaPORT (`--scsi-net 4=tap0`, or
`TAP=tap0 tools/hatari-asv.sh …`) on a host TAP interface, so the `dp`
driver comes up in the emulator exactly as on the card. With the host at
192.168.30.1/24 on the TAP and the guest at 192.168.30.20 in
`/etc/inet/hosts.net`, `ASV_HOST=192.168.30.20 tools/asvsh.py 'uname -a'`
gives a root shell over telnet, and `asvput.py` pushes files over ftp.
Everything that needed the real TT switched on can now run on the PC.

## Status

Working, in daily use on real hardware: the patched kernel, the network
driver (telnet/ftp/ping in both directions), the second disk. Not done: X on
the ATW800/2 (the stock `XatariServer` is bitplane-only); a kernel driver for
the card was written (`atw.c`, not shipped) but proved unnecessary because
`/dev/mem` maps the card.
