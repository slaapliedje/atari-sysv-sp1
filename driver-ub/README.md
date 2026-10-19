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
transfer there and then, in the caller's process context, through the
class driver in chunks (the stack sleeps while the chip works, which
the caller may); `ubtool daemon` sits in UBIOC_WORK, which starts the
stack if need be, polls the hubs and sleeps half a second (interruptible,
so the daemon can be killed).  Without the daemon the units are never
enumerated and a disk open fails with ENXIO.  b_blkno counts 512-byte
blocks; a request past the end is cut short.  ubsize gives the size for
swap; ubprint names the unit.  The raw device's data goes through a
16 KB kernel buffer with copyin/copyout: on this kernel user space is
its own address space (copyin is a moves loop), so a physio buffer's
address means nothing to the kernel, and bp_mapin is no help - this
kernel's maps page lists (B_PAGEIO) only and panics on a B_PHYS buffer
(ub 0.5 died of that; ub 0.6, which read the user address directly,
died of a bus error).  Test: ubtool daemon &;
dd if=/dev/rdsk/usb0 bs=512 count=1 | od -x (the MBR, 55aa at the end);
dd if=/dev/dsk/usb0 bs=8k count=16 of=/dev/null (through the buffer
cache); compare with ubtool read 0 0 1.  Next: mtools for the FAT side.

The 2 GB limit: physck and the block-special read multiply the size in
512-byte blocks by 512 in 32-bit arithmetic, so ubsize/physck are told
at most 0x3fffff blocks; the raw and block devices reach the first 2 GB
of a bigger stick, the ioctls (UBIOC_RD/WR) all of it.

THE CRASHES, FOUND 2026-10-18 evening (crash(1M) on the live kernel,
`od -x cdevsw`, `ts` on the entries): a driver whose master entry has
the b flag is wired by cunix through its OLD-STYLE GATEWAY, as the
kernel's own hd and ffd are: cdevsw/bdevsw row 0 holds gen_copen,
gen_cclose, gen_read, gen_write, gen_ioctl, gen_bopen, gen_strategy,
which call the driver's real entry points (kept in a shadow table) with
the SVR3 conventions - dev by value as major<<8|minor (cmpdev), read
and write with the request in u.u_base/u_count/u_offset and no uio,
ioctl with four arguments and the result taken from u.u_rval1, the
error from u.u_error.  A character-only driver (ub 0.1, snd, midi,
tlk) is wired directly with the SVR4 conventions.  ub 0.2/0.3, written
SVR4-style, read the device number as a pointer and passed garbage to
physiock: every open of a disk minor reset the machine, silently (no
panic: a jump through garbage).  ub 0.4 uses the old conventions
(ubopen(dev, flag, otyp), ubread(dev) = physck + physio, ubioctl(dev,
cmd, arg, mode), u_error/u_rval1/u_procp by their offsets 0x4d1/0x4e4/
0x4dc in u (u_cred is u_procp->p_cred, at 16: what the kernel's own
suser calls pass), since sys/user.h does not compile with GCC 2.7.2's
headers).
