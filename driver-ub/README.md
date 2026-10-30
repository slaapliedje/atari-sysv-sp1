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
                       (obcsn), install.sh (the nodes, ubtool, the rc script;
                       the kernel only when its inputs changed)
    ubhid.[ch]         HID keyboards and mice (boot protocol) as a second
                       IKBD: their reports become the keyboard controller's
                       bytes, put into the kernel's ikbd ring (the X server
                       reads /dev/ikbd) or given to the console's termin
    ubtool.c           its user side: start [lightning|netusbee], stop, poll,
                       devs, stor, read, daemon, hid (the keyboards and
                       mice), ikbd (inject bytes)
    ub.init            /etc/init.d/ub: `ubtool daemon` at boot (S75ub, K75ub)
    ub.default         /etc/default/ub: the bus to start on (BUS=netusbee)
    ubuser.c           user-space harness: ubuser [-l|-n] [SECONDS]
    ubstor.[ch]        mass storage, bulk-only: probe, CBW/CSW transport,
                       TEST UNIT READY, INQUIRY, READ CAPACITY, READ/WRITE 10
    ubprobe.c          the NetUSBee's chip found from user space
    lightprobe.c       the Lightning VME's

Build the harness on the PC: m68k-cbm-sysv4-gcc -O -I. -o ubuser
ubuser.c usb.c hub.c ucd.c udd.c isp116x.c ub_io.c ubstor.c (-DUB_DEBUG for
the trace).  It talks to the chip through /dev/mem, so it runs from TT-RAM
and the cartridge port sees clean cycles; the kernel driver uses the
nu driver's frozen-cache accesses for the NetUSBee (ub_io.c, ub_nuacc).

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

2026-10-19: the daemon is an rc script, ub.init = /etc/init.d/ub, in
cron's style (the pid from ps -e), linked as /etc/rc2.d/S75ub (start:
`nohup ubtool daemon` if none runs, /dev/ub0 and /usr/bin/ubtool
present) and /etc/rc0.d/K75ub (stop: kill it).  So the stick's
devices and mtools' u: drive are there after a boot without anyone
logging in, as the network is through dhcpc.  install.sh now does the
nodes, ubtool and the rc links every run and rebuilds the kernel only
when a source, master.d-ub or ub.pc.o changed (install.sh itself is no
longer in the hash: an rc change must not cost a kernel build).  Tested
on the TT: stop kills the hand-started daemon, start brings one back
(and a second start does not add another), the daemon outlives the
shell that started it, mdir u: and the raw MBR read work after it.

2026-10-19, ub 0.8: USB keyboards and mice (ubhid.c).  A HID interface
with the boot protocol (subclass 1, protocol 1 keyboard / 2 mouse) is
put into the boot protocol with an idle rate of 0 and its interrupt IN
pipe tried from the daemon every 2 ticks (ub_work: 44 times a second
measured; the hubs every 25th time); a try is one USB frame, a NAK
when nothing changed.  Each report becomes the bytes the IKBD would
have sent (a US layout to Atari scancodes after FreeMiNT's
usb_keyboard.c: F11/F12 Help/Undo, End/PgUp/PgDn Shift+Home/Up/Down,
PrtScr Alt+Help, both Alts Alt, the wheel Up/Down arrow presses, the
middle button nothing) and ub_ikbd_put gives them to the kernel's
ikbd driver as its interrupt does: into the ik_rdbuf ring with
wakeup/pollwakeup while /dev/ikbd is open (the X server, Xatw, reads it;
all those symbols are global), to ikbd_func (the console's termin)
otherwise; at spl7, a record at a time.  A record of the real mouse
being received at that instant could still be split: not both mice at
once.  On the TT with a Wooting Two HE and a Logitech M-UV55 on the
Lightning: both probe (ubtool hid: polls, reports), the daemon takes
about 3% of the CPU polling them, `ubtool ikbd f8 0 0` (UBIOC_IKBD,
root) goes through to X.  Typing and moving: the user's test.

2026-10-20, ub 0.9: the NetUSBee's USB side from the kernel.  The
cartridge port shows the card the 68030's next instruction fetch on the
address lines it samples (the nu driver's finding, driver-nu/nu.c), so
in the kernel each access to the card is nu_acc's dance (ub_io.c,
ub_nuacc): a 22-byte loop run once on a pad in ST-RAM with the same
A1-A13 as the card address, which brings it into the instruction
cache, then with the cache frozen (CACR FI) on the card, a pad store
right before and after, at spl7; a word form for the 16-bit data read
at 0xFA8000.  The two halves of a register access (the latched byte,
then the command or data read) are at spl7 together, as the Ethernet
side's register reads latch a byte too.  ubinit probes the Lightning
VME first (ioprobe: an empty slot bus-errors), then the NetUSBee (the
cartridge port always answers: the chip ID decides).  One host
controller runs at a time: `ubtool start netusbee` (UBIOC_START with
the bus) stops the stack (ub_stop: every device gone for its class
driver, the controller unregistered), probes the chip on the other bus
and starts again there; `ubtool stop` (UBIOC_STOP) takes it down, the
daemon's next turn brings it up on the same bus; ubtool status says
which bus.  Speed on the NetUSBee: each access costs the dance (tens
of microseconds), a 64-byte packet a few milliseconds; runs of FIFO
words with the cache frozen once (as nu_accrd/nu_accwr) are the next
step if storage through the cartridge port is wanted.

2026-10-22: a TT without a host controller gives the driver no memory.
The stack's big arrays were static: U-Boot's 64 KB configuration
descriptor buffer, the 32-device table (82 KB), the 16 KB raw I/O bounce
buffer and the NetUSBee's 32 KB access pad, 200 KB of kernel bss on
every machine.  Now `ub_mem` takes the first three from the kernel
(kmem_alloc) the first time the stack starts, which only happens after
`ubinit` found a chip, and `ub_io_init` takes the pad when the NetUSBee
is the bus (KM_NOSLEEP: that runs from `ubinit` at boot; kmem_init is in
mlsetup, long before the io_init table).  The descriptor buffer is 4 KB
now (wTotalLength is 16 bits, but nothing on this bus comes near).  The
harness keeps the arrays static (no malloc in AMIX-built ASV programs).
What is left static: 5 KB.  The daemon follows the same rule: `ubtool
status` exits 1 when there is no controller, `ubtool daemon` exits at
once then, and /etc/init.d/ub does not start it.  The Lightning is
probed with ioprobe (an empty VME slot bus-errors), the NetUSBee by its
chip ID; the kernel's own choice when both answer is the Lightning,
/etc/default/ub (`BUS=netusbee`) makes the rc script start the other one
first.  install.sh puts the file in only when there is none.

2026-10-29: hotplug.  On the TT with the NetUSBee, a hub (keyboard
behind it) was unplugged, a mouse and the hub plugged back in, and
nothing came up again: `ubtool devs` showed the root hub alone, while
the kernel's message buffer showed the nu driver finding the Ethernet
chip without its setup twice after the unplug.  The card had moved in
the cartridge slot as the plug was pulled: it sits loosely there and is
easily bumped, and the cartridge port is not made to be hot plugged.  A
card that loses contact and comes back has its chips reset; the ISP1160
is then in USBReset with its ports unpowered, and the hub polls see
nothing (the new message below says so when it happens).  `ubtool stop;
ubtool start netusbee` found everything.  Plug and unplug USB devices
through a hub, not at the card's own ports: the card is not touched, and
the hub's ports are what the hotplug below is for.  A card that comes
out of the slot altogether is beyond any driver.
Now the daemon reads HcControl before each hub poll (ub_check_hcd, two
reads that agree): out of the operational state, the stack goes down
(every device gone for its class driver) and starts again, and keeps
trying each second if the chip does not answer.  Three more weak spots
of the U-Boot hub code, mended on the way:
- no debounce: the port was reset the moment the connection showed,
  while the plug bounces.  hub_port_debounce (after Linux's) waits for
  100 ms of steady connection first.
- one try per plug: the change bit is cleared before the reset, so a
  device that failed to enumerate was never tried again.  A port that
  shows a connection and has no device is tried on the next polls,
  three times in all (hub->tries), then a message.
- devgone: a disconnect on a root port marked whatever device the
  transfer in hand was for as pulled, on either port, and for good;
  device slots are reused, so the next device in that slot failed
  every transfer, and a gone keyboard or mouse fails the HID poll of
  both.  Now only a device on the port that lost its device is marked,
  and the mark goes with the device's slot (usb_disconnect,
  usb_free_device) and at controller init.
The speed of a new device is the one its port shows after the reset.
Kernel messages print %ld as cmn_err knows it (%d): "New USB device
(d)" was the l modifier cmn_err does not have.
On the real TT (2026-10-29, the NetUSBee): the low-speed mouse behind
the Genesys USB 2.0 hub (05e3:0610), which before this never enumerated
(the hub disabled the port right after its reset, and a garbled reply
to the second reset left the ATL buffer full: "FIFO not empty" on every
later transfer to the hub), now comes up at boot and comes back within a
second when unplugged from the hub and plugged in again.  Which of the
changes above did it was not traced.
