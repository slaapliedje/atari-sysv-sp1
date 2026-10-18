# driver-ub - USB for Atari System V

FreeMiNT's USB stack (U-Boot's core and hub code, the ISP1160 host
driver; all GPL-2) on Atari System V, for the Lightning VME and the
NetUSBee, which both carry a Philips ISP1160.

    ub.h, ub_io.c      the platform: the two buses, delays, printing
    isp116x.[ch]       the host controller driver (from ucd/netusbee)
    usb.c hub.c ...    the core, ucd.c/udd.c the registration glue
    ub.c, ubioc.h      the kernel driver, major 46: /dev/ub0 (minor 0, the
                       control device: ioctls that start the stack, poll the
                       hubs, list devices and storage, read/write blocks) and
                       the storage units as disks, /dev/dsk/usbN (block) and
                       /dev/rdsk/usbN (raw), minor 16+N; master.d-ub
                       (obcsn), install.sh (makes the nodes)
    ubtool.c           its user side: start, poll, devs, stor, read, daemon
    ubuser.c           user-space harness: ubuser [-l|-n] [SECONDS]
    ubstor.[ch]        mass storage, bulk-only: probe, CBW/CSW transport,
                       TEST UNIT READY, INQUIRY, READ CAPACITY, READ/WRITE 10
    ubprobe.c          the NetUSBee's chip found from user space
    lightprobe.c       the Lightning VME's

Build the harness on the PC: m68k-cbm-sysv4-gcc -O -I. -o ubuser
ubuser.c usb.c hub.c ucd.c udd.c isp116x.c ub_io.c ubstor.c (-DUB_DEBUG for
the trace).  It talks to the chip through /dev/mem, so it runs from TT-RAM
and the cartridge port sees clean cycles; the kernel driver will use the
nu driver's frozen-cache accesses for the NetUSBee.

Byte order, measured: register words come swapped on the Lightning's
VME bus and as they are on the NetUSBee (ub_io_init reads HcChipID both
ways); FIFO data words go the other way round from register words, both
directions, on both.

Trap: ASV user programs built with the AMIX cross toolchain must keep
to printf, perror and system calls: the AMIX headers' FILE layout is
not ASV's libc's, so putchar, fwrite, fprintf(stderr) and friends wreck
stdio (printf then prints the format's first character for every
conversion); malloc/strtoul crashed too.  ubtool uses write(2) and poll.

2026-10-18: the kernel driver is in (/dev/ub0): ubtool start enumerates
the stick, ubtool read 0 0 1 shows its MBR.  Earlier that night, from
user space, a SanDisk Cruzer Fit
(16 GB) enumerates and its block 0 reads back (an MBR, one FAT32
partition).  Next: the kernel driver (tick-polled) and a block device,
then mtools for the FAT side, then HID.

2026-10-18, later: the block device.  /dev/dsk/usbN and /dev/rdsk/usbN
are the whole stick (no slices: mtools takes an offset, a file system
on a stick would be made over the whole device).  ubstrategy does the
transfer in the caller's process context through ubstor in 64 KB
chunks under a sleep lock that every use of the stack takes (ioctls
included); opening a unit starts the stack and polls the hubs first, so a
stick plugged in since the last poll is found.  b_blkno counts 512-byte
blocks; a request past the end is cut short.  ubsize gives the size for
swap; ubprint names the unit in buffer-cache messages.  Try: dd
if=/dev/rdsk/usb0 bs=512 count=1 | od -x (the MBR, 55aa at the end),
dd if=/dev/dsk/usb0 bs=8k count=16 of=/dev/null (through the buffer
cache), ubtool read 0 0 1 for comparison.  Next: mtools for the FAT side.
