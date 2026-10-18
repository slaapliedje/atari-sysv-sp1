# driver-ub - USB for Atari System V

FreeMiNT's USB stack (U-Boot's core and hub code, the ISP1160 host
driver; all GPL-2) on Atari System V, for the Lightning VME and the
NetUSBee, which both carry a Philips ISP1160.

    ub.h, ub_io.c      the platform: the two buses, delays, printing
    isp116x.[ch]       the host controller driver (from ucd/netusbee)
    usb.c hub.c ...    the core, ucd.c/udd.c the registration glue
    ubuser.c           user-space harness: ubuser [-l|-n] [SECONDS]
    ubprobe.c          the NetUSBee's chip found from user space
    lightprobe.c       the Lightning VME's

Build the harness on the PC: m68k-cbm-sysv4-gcc -O -I. -o ubuser
ubuser.c usb.c hub.c ucd.c udd.c isp116x.c ub_io.c (-DUB_DEBUG for the
trace).  It talks to the chip through /dev/mem, so it runs from TT-RAM
and the cartridge port sees clean cycles; the kernel driver will use the
nu driver's frozen-cache accesses for the NetUSBee.

Byte order, measured: register words come swapped on the Lightning's
VME bus and as they are on the NetUSBee (ub_io_init reads HcChipID both
ways); FIFO data words go the other way round from register words, both
directions, on both.

2026-10-18: the Lightning VME enumerates the root hub and a SanDisk
Cruzer Fit on port 2 from user space.  Next: the storage class driver
(read a sector), then the kernel driver and a block device.
