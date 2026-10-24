# ufs.xfs: Atari System V's file systems under FreeMiNT (read-only)

A FreeMiNT file system module that mounts the UFS slices of the Unix
side of a dual-boot TT disk, so that MiNT programs read the System V
root, /var and /home directly: no USB stick, no network.

`UFS.XFS` goes into the kernel directory (`C:\MINT\1-19-CUR\` on the
distro's layout; the kernel loads every `*.XFS` there).  At boot it
looks at every XHDI device, finds the `UNX` entry in the Atari root
sector, reads the SVR4 VTOC two sectors in, and mounts each slice that
carries a UFS superblock (swap, the whole-disk slice and unmountable
ones skipped) as a drive, `V:` onwards in slice order, skipping letters
the BIOS already uses:

    ufs: V: root W: var X: home Y: ufs

The label is the VTOC volume name or, when that is empty, the slice's
tag (root, var, home, usr).  Everything is read-only: the drive is
write-protected in the buffer cache and every writing call answers
EROFS.  Permissions, owners, times (UTC, converted by the kernel),
device numbers and symbolic links come through; an absolute link
target (`/usr/bin`) is given the root slice's drive (`V:/usr/bin`), so
it resolves where System V resolves it.

## How

The on-disk format is 4.3BSD's fast file system as SVR4 shipped it, big
endian like the 68030, so the structures (superblock, 128-byte inodes
with the EFT uid/gid fields at 116, `struct direct` entries) are used as
they lie.  HDDriver gives a `UNX` partition no drive letter, but XHDI's
XHReadWrite addresses the whole device by SCSI id with absolute
sectors, which is what the VTOC holds, so the module reserves a buffer
cache descriptor (`bio.res_di`) for each drive with an I/O vector of
its own that reads through XHDI from the slice's start.  Blocks are
cached by the kernel in full blocks (8 KB) and, for a file's fragment
tail, in fragments (1 KB).

Written from the FFS paper and the layouts as the system's headers
state them; no AT&T code.  The MiNT side follows FreeMiNT's ext2.xfs
and skeleton.  GPL-2.

## Build

Needs the FreeMiNT source tree (headers and `libkern`) and Vincent
Rivière's `m68k-atari-mint-gcc`:

    git clone --depth 1 https://github.com/freemint/freemint ~/dev/usb-ref/freemint
    make -C ~/dev/usb-ref/freemint/sys/buildinfo
    make -C ~/dev/usb-ref/freemint/sys/libkern 030
    make            # FREEMINT=... CROSS=... CPU=030 -> UFS.XFS (8.5 KB)

Modules compile with `-mshort` (16-bit int), as the kernel does.

## Tested

In Hatari (EmuTOS 1.3 TT, FreeMiNT 1.19 from the distro) with the
release's Unix image as a second SCSI disk: the four slices mount;
listing, `Fopen`/`Fread`, `Freadlink`, `Dfree` and labels work;
`/etc/passwd`, `/usr/bin/sh` (127 KB) and `/usr/lib/libc.so.1` (337 KB,
through the indirect block) read byte for byte as `tools/ufs.py` reads
them from the image.  `test/ufstest.c` is the program used (mintlib;
`exec c:\ufstest.prg V:` from MINT.CNF, Hatari `--conout 2`).

Not yet: on the real TT with HDDriver's XHDI, writing, files over 16 MB
(double indirect blocks: the code is there, untested), slices on a
second disk with their own VTOC (any XHDI device is scanned, so it
should just work).
