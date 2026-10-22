# The release image

`build.py` makes the Atari System V disk image that is handed out: the
stock image plus everything in this repository that a plain TT030 with a
ZuluSCSI or BlueSCSI can use, built fresh in Hatari so that nothing from
anybody's own machine is in it.  The output is `HD0.bin` (2 GiB, sparse),
`NE4.img` and the card's `.ini` templates, in `~/dev/asv-release/out`.

```
python3 release/build.py              # all stages; ~1-2 h
python3 release/build.py --from stage2
python3 release/build.py --only pack
```

## What goes in

| | |
|---|---|
| kernel | `kernel/patch-asv-image.py`: the 96 MB TT-RAM cap and the Lance probe moved off the ATW800/2's address |
| kernel drivers | `dp` (DaynaPORT, as `/dev/en0`), `nu` (NetUSBee Ethernet), `ub` (USB on the NetUSBee or the Lightning VME: `/dev/ub0`, `/dev/dsk/usbN`, keyboard and mouse), `amx` (AMIX binaries), `snd` (`/dev/audio`), `midi` (`/dev/midi`) |
| loader probes | `EDTP.dp`, `EDTP.nu` and their `edt_data` rows (`driver-nu/edt/mkedtp.py`, from the stock `/stand`) |
| boot | `dhcpc` on `/dev/en0`: the address, name servers and default route from DHCP; `ubtool daemon` (`/etc/rc2.d/S75ub`, on the NetUSBee per `/etc/default/ub`; nothing when there is no controller) |
| AMIX userland | `/usr/amx` (AMIX's shared C library), `apkg` with gzip, less, grep, make, GNU tar and zlib installed (patch and acompat want /usr/local, which the hook refuses), bash 5.2 (`/usr/local/bin/bash`), vim 9.2, nano 9.2, mtools (with the `ub:` drive) |
| Manx | `manx`, `manxmail`, `manxtrust`, `ufetch` in `/usr/bin`, `xmanx` in `/usr/bin/X11` (beside the stock X11R4 clients); the CA bundle in `/usr/local/share/manx/cacert.pem`, pointed at by `/etc/profile.manx` |
| tools | the `rsync` and `ttntp` programs (no daemon, no clock configuration) in `/usr/local/bin`; `nuup.sh` and `strcf.nu` in `/usr/local/lib/netusbee` |

Left out on purpose: X11R6.3 and XView (they need the ATW800/2), `tlk`
and the transputer tools, the rsync daemon setup, `patch-ttram.py` (a
TT without TT-RAM must keep the stock KERNEL).

## How it is built

The stock image has no network driver Hatari can serve, so the build
bootstraps itself from the disk: `tools/ufs.py` replaces
`/etc/rc2.d/S75rpc` on a throw-away copy with `hook.sh` (same size,
padded), which at the copy's first boot extracts `stage1.sh` from a tar
on SCSI 2 (`payload.img`, made by `mkpayload.sh`) and logs to the raw
disk on SCSI 3 (`exchange.img`), which `build.py` watches.

- **stage 1** (on the stock copy): the loader probes into `/stand`, the
  six drivers built into the kernel (the `INCLUDE:DP` and `INCLUDE:NU`
  lines the installers write are taken out of `/stand/system` again:
  hardware drivers come from the loader's device table, and an INCLUDE
  for a card that is not there stops the boot's auto-configure, which
  is what makes the image boot on a TT without either card),
  `/dev/en0` re-pointed at `dp`,
  `dhcpc` installed, the hook put back, then the 2 GiB disk on SCSI 1:
  `partinit`, `format -w`, `setvtoc` with `vtoc-2g.dat` (root 768 MiB,
  swap 128, `/stand`, `/var` 128, `/home` the rest), `mkfs`, `cpio`
  copies of `/`, `/var`, `/home`, `/stand`, the two boot slices cloned.
  The kernel is built here so that the new disk's first boot finds the
  hardware its probes report already configured.
- **stage 2** (the new disk, root over telnet through slirp):
  `stage2.sh` installs the userland, `cleanup.sh` puts the stock network
  files back, points apkg at a public name server, clears logs and the
  build's files, and the system is halted.
- **pack**: a sparse copy as `HD0.bin`, its SHA-256, `NE4.img` and
  `card/`, and all of it in `~/dev/asv-release/asv-sp1-card.zip`.

The emulated clock matters: Hatari's TT clock chip is read two years
ahead by Atari System V, so `build.py` sets it two years back, and the
scripts never set the date themselves.  A guest clock in 1970 (what an
ASV `date` with a two-digit year 26 gives) makes `login` demand a new
root password, and the files of the build carry the wrong year.

The loader probe templates are read from the stock `/stand` (BFS) with
`~/dev/asv-release/bfs.py`; the stock root slice starts at sector 200
(`ufs.py` base 102400).

## Before shipping

Boot `out/HD0.bin` in Hatari once more, with and without `--scsi-net`
(copies: a boot writes to the disk): without it the boot must rebuild
the kernel and come up with "The system is ready" (`/dev/en0` missing,
`slink failed` are expected); with it `dhcpc` must get the address
from slirp and `ufetch http://example.com/` must fetch the page.  Then
write the card and boot the real machine.
