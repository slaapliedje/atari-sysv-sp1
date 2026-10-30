# A dual-boot TOS + Atari System V disk

How the 2 GiB `HD0.bin` with two TOS partitions and a bootable Unix
partition was made — entirely with ASV's own tools, from a running
system, with the blank disk attached as a second SCSI target (`c1d0`
here; in Hatari `--scsi 1=new.img`).

## Limits that shape the layout

- **Every Unix slice, the whole-disk slice `sf` included, must be under
  2 GiB.** `physiock()` turns the slice size into bytes in a signed 32-bit
  int (`size << 9`) and compares it with the offset; from 4194304 sectors
  up that is negative and every raw read fails with `ENXIO` — the symptom
  is `partinit: error reading physical block 0 ... No such device or
  address` on a disk the driver otherwise likes.  More space = more disks
  (ZuluSCSI serves IDs 0-6).
- TOS 3.06 partitions: keep each BGM partition at or below 512 MB (the
  32767-cluster limit).  384 MiB is used here.
- The root sector holds at most four entries and one `UNX`.  TOS
  partitions come first (from sector 2), Unix after.  The Unix partition
  starts with `U0boot` (block 0), the pdsector (1) and the VTOC (2); VTOC
  sector numbers are **absolute** disk sectors.

## Steps

```sh
# 1. root sector + both bootstraps (T0boot in block 0, U0boot at the UNX start)
partinit -I -i -b -Z -P 0:2:786432:BGM:STBOOT -P 1:786434:786432:BGM:NONBOOT \
        -P 2:1572866:FILL:UNX:UNIXBOOT -o /dev/rdsk/c1d0sf
partinit -p -o /dev/rdsk/c1d0sf            # exit code 10 + partitions
# 2. pdsector: the driver's own default (cyl/heads/sectors from the drive,
#    allocatable = UNX start + 3 .. disk end) is fine; just write it
format -w /dev/rdsk/c1d0sf
# 3. VTOC (disk/vtoc-2g-dualboot.dat; prtvtoc-style fields, absolute sectors)
setvtoc -s vtoc.dat /dev/rdsk/c1d0sf ; prtvtoc /dev/rdsk/c1d0sf
# 4. file systems
mkfs -F bfs /dev/rdsk/c1d0s3 24000 128
mkfs -F ufs -o nsect=32,ntrack=128,bsize=8192,fragsize=1024 /dev/rdsk/c1d0s1 1048576
mkfs -F ufs -o nsect=32,ntrack=128,bsize=8192,fragsize=1024 /dev/rdsk/c1d0s5 262144
mkfs -F ufs -o nsect=32,ntrack=128,bsize=8192,fragsize=1024 /dev/rdsk/c1d0s6 1004371
# 5. copy (mount needs -F: nothing in vfstab to infer the type from)
mount -F ufs /dev/dsk/c1d0s1 /mnt && (cd / && find . -mount -print | cpio -pdmu /mnt)
#    ... same for /var -> s5, /home -> s6; /stand (bfs) with cp; then the loader
#    slice and the second boot slice raw:
dd if=/dev/rdsk/c0d0s0 of=/dev/rdsk/c1d0s0 bs=512 count=196
dd if=/dev/rdsk/c0d0se of=/dev/rdsk/c1d0se bs=512 count=20000
```

`/etc/vfstab` needs no change: the new disk becomes `c0d0` once it is
SCSI ID 0.  The boot slice (`s0`, 196 sectors, tag V_BOOT) holds the
loader that prints `Boot:`; `partinit -i` installs only the two tiny
block-zero bootstraps, which is why `s0` is cloned raw.

## Choosing the OS

`/etc/T0boot` (block 0; replaced by HDDriver's loader once HDDriver is
installed, which behaves the same way) takes the NVRAM boot preference the ROM hands it
in `d5`, and compares its low byte with the partition flag bytes: `0x80`
St-boot, `0x40` Unix-boot.  Zero means "first bootable partition", which
on this layout is TOS.  `tools/setboot` sets it from Unix through the rtc
driver's `RTCNVMACCESS` ioctl (`setboot tos|unix|none`); under TOS Atari's
`SETBOOT.PRG` does the same.  That is Atari's SetBoot 1.1 (H.-M. Kroeber,
1992; GEM, XBIOS `NVMaccess`), shipped on **disk 11 of the ASV floppy set**
together with its `SETBOOT.RSC` and the Mark Williams C source.  Copy both
`SETBOOT.PRG` and `SETBOOT.RSC` to C:.  *Boot → Set boot preference →
Atari System V → OK → Reboot* comes back up at the Unix login; tested in
Hatari on the HDDriver disk below.  Uwe Seimet's BOOTCONF.APP 1.30 (1996, freely
distributed; mirrored in the `SYSTEM/BOOTCONF` folder of milan.kovac.cc's
Atari archive) does the same from a single dialog, with a *TT SVR4* button
next to MagiC/Linux/NetBSD, and also sets the SCSI initiator ID and bus
arbitration. Its video, language and date fields only matter on a
Falcon.  In Hatari the NVRAM lives in
`~/.config/hatari/hatari.nvram` (user bytes 14..63; the word at file
offset 0 is the preference, `00 40` for Unix, checksum `~sum, sum` of the
first 48 bytes at 48..49).

## The TOS side: HDDriver

`partinit` leaves the two BGM partitions unformatted.  Format them on
the host with the Atari variant of `mkfs.fat` (8 KiB logical sectors,
2 per cluster, 24575 clusters — inside TOS 3.06's limits) and splice
them in:

```sh
truncate -s $((786432*512)) C.fs && mkfs.fat -A -F 16 -n TOS C.fs
truncate -s $((786432*512)) D.fs && mkfs.fat -A -F 16 -n SPARE D.fs
dd if=C.fs of=HD0.bin bs=512 seek=2      conv=notrunc
dd if=D.fs of=HD0.bin bs=512 seek=786434 conv=notrunc
```

Then boot TOS from the HDDriver floppy (its `AUTO\HDDRIVER.PRG` mounts
C: and D:), run `HDDRUTIL.APP`, select C: and use *File → Install
HDDRIVER…*.  That writes `C:\HDDRIVER.SYS` and **replaces the boot code
in the root sector** (bytes 5–445; the partition table at 0x1C0 and the
`$1234` checksum are kept).  T0boot is gone after this, and nothing is
lost: HDDriver's root loader follows the same NVRAM convention.  Tested
in Hatari (TT, TOS 3.06, HDDriver 13.01):

| NVRAM preference | result |
|---|---|
| `00 00` (none) | HDDriver loads from C:, desktop with C: and D: |
| `00 40` (Unix) | chains to U0boot on the UNX partition, `Console login:` |

HDDRUTIL wants at least 640x200 and its menu bar does not fit ST-Medium;
start Hatari with `--tos-res ttmed`.

To update a card whose Unix partition is already in use, write only the
TOS region (sectors 0 – 1572865) over the card's `HD0.bin`, so the live
Unix file systems are left alone:

```sh
dd if=HD0-tos-side.bin of=/path/to/card/HD0.bin bs=512 conv=notrunc
```

## Hatari

Hatari's SCSI emulation answered `READ CAPACITY` with PMI set with the
full capacity and had no mode page 3, so ASV's driver computed a broken
geometry for an unlabelled disk; the fork's `hdc: a consistent disk
geometry` commit fixes that.  With it the recipe above runs unchanged in
the emulator, which is where this one was done and boot-tested before the
image went on the card.

## MagiC on the TOS side

Andreas Kromke released MagiC for Atari as GPLv3 in October 2026
(version 6.30 with MagxDesk 5: `MagiC/Releases/Atari-MagiC-*.zip` in
gitlab.com/AndreasK/Atari-Mac-MagiC-Sources).  It is on C: since
2026-10-23, put there with `tools/magic-install.sh` from the release's
`MAGIC.A` folder after localising it on the PC the way `LANG/EN.BAT`
does (MAGIC.RAM to the root, the EN resources over GEMSYS, EXTRAS and
XTENSION; `LOCALISE.SH` is for MagicOnLinux and does nothing useful
here).  The boot is HDDriver → `AUTO\MAGXBOOT.PRG` → MagiC, which runs
HDDriver again and then the rest of the AUTO folder.  Two things that
cost an afternoon in Hatari:

- **`AUTO\AUTOEXEC.BAT` switches the AUTO folder off.**  MagiC's kernel
  runs that file *instead of* `AUTO\*.PRG` when it exists
  (`KERNEL/BIOS/COMMON/AUTO.S`), and the release ships an empty one:
  MagiC came up on the TT's own screen with XVDI, HNETDEV and the rest
  never run.  Without the file XVDI takes the ATW800/2 as under TOS.
- **The XVDI set must come after `MAGXBOOT.PRG`.**  Loaded under TOS
  first, MagiC's booter hangs after its banner.

Tested in the ATW800/2-emulating Hatari (`--vme atw800 --vme-base
0xA00000`): MagxDesk at 1024x768 on the card, C: and D: from HDDriver.
Both shift keys held at boot skip MagiC ("Shift-Shift: MagiC not
installed").

## Three ways to boot: `setboot tos|magic|unix`

The NVRAM preference has a MagiC value, 0x08 (NetBSD's `bootpref`:
TOS 0x80, System V 0x40, NetBSD 0x20, Linux 0x10, MagiC 0x08), and
BOOTCONF.APP's *MagiC* button writes it, but nothing on a stock disk
acts on it: HDDriver's root-sector loader boots the partition whose
flag byte (masked 0xf8) equals the preference, so 0x08 matches nothing
and the TT comes up on the ROM desktop without a hard disk; and
`MAGXBOOT.PRG` never looks at the NVRAM (its source is in the MagiC
repository).  Two small pieces make the choice real, with MagiC and
TOS sharing C: and the Unix side untouched:

- **D: is flagged 0x09** (`partinit`'s flag byte 0x01 plus the MagiC
  bit) and carries HDDriver's partition boot sector, copied from C:
  with D:'s own serial number, plus `HDDRIVER.SYS`.  For 0x08 the
  loader boots D:, HDDriver loads from there and keeps C: as the boot
  drive, so the boot goes on exactly as for TOS.  HDDriver is Uwe
  Seimet's commercial software: both pieces come from your own licensed
  copy on C:, and nothing of HDDriver is in this repository or in the
  release image (whose loader is Atari's `T0boot`).
- **`C:\AUTO\MAGXNVM.PRG`** (`tools/magxnvm`, 150 bytes, first in the
  folder) reads the preference with NVMaccess and runs `C:\MAGXBOOT.PRG`
  only when it is 0x08.  MagiC then re-runs the AUTO folder itself;
  MAGXBOOT, started a second time under MagiC, just returns.

| NVRAM | `setboot` | boot |
|---|---|---|
| 0x0080 | `tos` | C:, plain TOS |
| 0x0008 | `magic` | C:, MagiC 6.30 on the ATW800/2 |
| 0x0040 | `unix` | the UNX partition, Atari System V |
| 0x0000 | `none` | first bootable partition: C:, plain TOS |

Tested in Hatari with all four; `setboot` on the Unix side knows
`magic` since the same commit, BOOTCONF.APP and SETBOOT.PRG on C: set
it from TOS.  The AUTO order stays MAGXNVM, HNETDEV, the XVDI set.

## The boot menu: `tools/osmenu`

`OSMENU.PRG`, first in `C:\AUTO`, replaces MAGXNVM with a menu on the
console after HDDriver:

```
Boot:  1 TOS   2 MagiC   3 FreeMiNT   4 System V
       (MagiC in 5 s)
```

`\AUTO\OSMENU.INF` lists the entries and holds the default, the
countdown and the paths: `menu=tos,magic,mint,sysv` (also `netbsd`,
`linux`; `unix` is `sysv`), `default=`, `timeout=SECONDS` (0 for no
menu), `mint=` (the FreeMiNT kernel) and `magic=` (MagiC's booter, when
not `\MAGXBOOT.PRG`).  This disk's file:

```
menu=tos,magic,mint,sysv
default=magic
timeout=8
mint=C:\MINT\MTLOAD.PRG
```

Without `menu=` the entries are TOS, MagiC when its booter is there and
FreeMiNT when its kernel and `MINTBOOT.PRG` are: an NVRAM entry has to be
listed.  They are left out on a machine without NVRAM (an ST, where
NVMaccess answers with its opcode).  A MagiC preference in the NVRAM
still makes MagiC the default, so BOOTCONF.APP and `setboot magic` keep
working.  What each choice does:

- **TOS**: the program returns and the AUTO folder goes on.
- **MagiC**: `\MAGXBOOT.PRG` is run.  MagiC then runs the AUTO folder
  itself; OSMENU sees the `MagX` cookie on that pass and returns.
- **FreeMiNT**: the kernel's path goes into `\AUTO\OSMENU.OUT`, and
  `MINTBOOT.PRG`, last in AUTO, runs the kernel after the rest of the
  folder (the VDI must be in before MiNT, as with NVDI) and deletes
  the file.
- **System V** (and NetBSD, Linux): the NVRAM preference is set to 0x40
  (0x20, 0x10), and the program says that the machine must be switched
  off and on, waits for a key and goes on with TOS.  No reset does it:
  HDDriver's root-sector loader is handed the preference at power-up
  only; after a warm or a cold reset (memvalid cleared, a jump through
  `_sysbase`, the memory test and all) TOS comes up again, seen on the
  TT.  From then on the TT boots Unix at every power-up; `setboot tos`
  or `setboot magic` on the Unix side (and BOOTCONF.APP on the TOS
  side) switch back.

So the menu is for the TOS-side systems, which it starts in place; Unix
is reached through the NVRAM preference and the power switch, in both
directions.  (An earlier one-shot, an rc script putting TOS back at
every Unix boot, is gone with the reset that it needed.)
Both programs are assembled by `build.sh` with the SVR4 GNU as of the
ASV cross toolchain, position-free (no relocation table).  Each sets
up a stack inside the memory it keeps before `Mshrink`: the one TOS
hands over is at the top of the TPA, which `Mshrink` gives back, and a
program started next is loaded over it (the real MAGXBOOT and MiNT never
come back, so only a failed start showed it).  `test-hatari.py` boots
both from a GEMDOS-emulated C: on a TT, a Falcon030, an STE and an ST,
with stand-ins for MagiC's booter and the kernel, and checks every entry,
the countdown, Return, the NVRAM writes and the entries left out without
NVRAM.  `mkzip.py` makes the package to hand out (the programs,
`README.TXT`, a sample `OSMENU.INF`, the source, the GPL).

## FreeMiNT on C: too

`tools/mint-install.sh` takes the FreeMiNT OS Distro's card image
(github.com/rootadm79/freemint_os_distro, 26.2, a byte-swapped CF
image) and puts its kernel, XaAES, TeraDesk and applications on C:
beside MagiC: `C:\MINT` (kernel 1.19 and drivers, the distro's loader
as `MINT\MTLOAD.PRG`, which picks the kernel for the CPU), `APPS`,
`ACC`, `CPX`, `BTFONTS`, `CLIPBRD`, `GUIDES`.  The menu starts it
through MINTBOOT after the AUTO folder, so XVDI is the VDI XaAES draws
with: TeraDesk at 1024x768 on the ATW800/2 in Hatari.  Left out, each
on purpose: NVDI and WDIALOG (commercial; XVDI does the job), the demo
HDDriver and its CPX, the CT60/Falcon-only pieces (maprom, fVDI,
Videlity, the Videl and CT60 CPXs), the logs.  `MINT.CNF` keeps the
distro's layout: its ext2 userland is drive E: (`sln e:/bin u:/bin`
and so on), which is the next step; without it the GEM side still
boots, with the `exec u:/sbin/...` lines failing harmlessly.

Two traps from the Hatari runs: `ENEC3.XIF`, the EtherNEC driver (the
NetUSBee's Ethernet under MiNT), probes the cartridge port when loaded
and never returns on a machine without the card, which is every Hatari
so far (the loop reads `0xFB0FFE`/`0xFB0044`/... and waits on `_hz_200`
with the MFP masked); take it out of `MINT\1-19-CUR` for emulator tests
and the same goes for `exec u:/sysdir/usb/loader.prg`.  And XaAES's
`XA_BOOT.LOG` is only as long as its directory entry says while the
file is open: read the clusters, or boot with `FS_WB_ENABLE` off.
