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
Hatari on the HDDriver disk below.  In Hatari the NVRAM lives in
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
