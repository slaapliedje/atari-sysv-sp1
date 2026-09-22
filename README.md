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
| `tools/` | `ufs.py` (read/patch files inside an ASV image on the PC), `asvsh.py` / `asvput.py` (run commands / push files over telnet and ftp) | Working on the machine without a floppy drive |
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

## Driver notes (the parts that cost the most to learn)

- The kernel's SCSI core calls a driver's START routine with flag 1 for
  "start the queue head"; the core itself dequeues and `biodone()`s a
  finished request. Flag 0 means "retry the same request", not completion.
- The DaynaPORT raises no interrupt; `dp` polls from a STREAMS service
  procedure at 50 Hz while traffic flows, 10 Hz when idle. Received frames
  carry a 4-byte CRC trailer that the driver strips.
- spl levels on this kernel are absolute MFP masks (`spl4` SCSI,
  `splstr` = `spl5`); a timeout callback should never start SCSI work.

## Status

Working, in daily use on real hardware: the patched kernel, the network
driver (telnet/ftp/ping in both directions), the second disk. Not done: X on
the ATW800/2 (the stock `XatariServer` is bitplane-only); a kernel driver for
the card was written (`atw.c`, not shipped) but proved unnecessary because
`/dev/mem` maps the card.
