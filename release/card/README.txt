Atari System V for the TT030 - ready-to-run card image
=======================================================

What this is
------------
Atari System V Release 4.0 (UniSoft's SVR4 for the TT030, 1991-92) with
the unofficial Service Pack 1 already installed:

  - drivers for today's hardware: the DaynaPORT network device that
    ZuluSCSI and BlueSCSI emulate (WiFi), the NetUSBee (Ethernet and USB:
    sticks, keyboard, mouse), the Lightning VME's USB, the TT's own
    sound and MIDI ports
  - the address, name servers and route from DHCP at boot
  - a kernel that uses all the TT-RAM in the machine, up to 256 MB,
    with swap sized to match at the first boot
  - for an ATW800/2: the transputer link driver, X11R6.3 with the Xatw
    server, XView, all switched on when the card is found
  - Manx, a web browser with HTTPS (text mode and X11), and its mail
    program
  - bash 5.2, vim 9.2, nano 9.2, GNU tar, gzip, less, grep, make, zlib,
    mtools, rsync, and apkg (the AMIX package client) for more

The card
--------
On a FAT32 card for a ZuluSCSI or BlueSCSI:

  HD0.bin        the system disk, SCSI ID 0 (2 GiB)
  NE4.img        an empty file: it makes the DaynaPORT appear at SCSI ID 4
  zuluscsi.ini   or bluescsi.ini: put your WiFi name and password in
                 (2.4 GHz networks only)

The TT's NVRAM boot preference must be "Atari System V" or none.  With
TOS (or another system) preferred, the root-sector loader passes over the
Unix partition and the TT comes up on the TOS desktop with no hard disk.
Set it from TOS with BOOTCONF.APP (its "TT SVR4" button) or Atari's
SETBOOT.PRG (System V floppy disk 11), then switch the TT off and on.

PiSCSI (RaSCSI), untested so far: attach HD0.bin as a hard disk at
SCSI ID 0 (by type, or rename it HD0.hds), and its DaynaPORT emulation
at any ID; the driver finds a "Dayna" device wherever it is.  The
network is then the Pi's; NE4.img and the .ini files are not used.

First boot
----------
The boot takes about seven minutes.  The kernel on the image was built
for a TT with the DaynaPORT and the NetUSBee; when the loader finds a
different set of devices the boot rebuilds the kernel first, which adds
a few minutes and prints "/stand/unix is being created".  Log in as
root at the console; root has no password - set one with `passwd` and
add users with `useradd`.

What the boot finds out (/etc/init.d/sp1hw, every boot, one line on
the console and the details in /etc/sp1):
  - memory: the kernel takes the TT-RAM there is (256 MB at most); a
    machine with more than 160 MB gets a swap file of its memory's
    size on /home at the first boot (a few minutes, once)
  - the network card: the one that reaches a network gets DHCP.  With
    more than one (a NetUSBee and the DaynaPORT, say), each is asked
    for a lease, the NetUSBee first, then the DaynaPORT, then a Riebl
    card, and the first that answers is used; what each said is in
    /etc/sp1/netprobe.  Put nu, dp or la in /etc/inet/sp1net to choose
    the card yourself, or `static` to keep your own network setup
  - an ATW800/2: X11R6.3's Xatw server takes over from the stock X11R4
    one, the T425 is wiped at boot, and tprobe's census of the
    transputers is in /etc/sp1/transputers.  Its screen is 1024x768 in
    256 colours, which every card can show.  For 24-bit colour, with
    the card's full 4 MB (jumpers A0 and A1 closed), add "-depth 32" to
    the Xatw line in /usr/x11r6/lib/X11/xdm/Xservers; on a card set up
    for 2 MB, "-mode 800x600 -depth 32" fits.  `/usr/x11r6/bin/Xatw
    -listmodes` lists the modes.  Without the card the
    loader's Riebl probe is put back at its real address from the next
    boot on (the image ships it moved, as the ATW800/2's video memory
    answers there).
The stock kernel objects stay in /stand (KERNEL.stock, PLATFORM.stock,
CLOCK.pre-1968) and the stock kernel itself as /stand/unix.stock: boot
it from the loader's prompt if ever needed.
The system comes up in X (the stock X11R4 server on the TT's screen);
`init 2` from a console login gives a plain text system, and editing
/etc/inittab (is:4: -> is:2:) makes that the default.

Time
----
The clock is read as TOS sets it (the stock driver was two years
ahead), and at every boot with a network the time is set from
pool.ntp.org (/etc/rc2.d/S99ttntp).  The time zone that comes with the
stock system is US Eastern, so SET YOUR ZONE in /etc/TIMEZONE first -
with the wrong zone the clock shows the right moment in the wrong
place, hours off your wall clock.  Change the file's TZ= line, as in

    TZ=MST7MDT

(standard-zone name, hours west of Greenwich, daylight name: PST8PDT,
CST6CDT, GMT0BST, MET-1DST...).  It counts from the next boot or login;
`date` shows the result.  Without a network ttntp is skipped and the
clock stays as TOS set it.

Network: /etc/inet/hosts.net gets the address from DHCP at every boot;
`ifconfig en0` shows it, `netstat -rn` the route.  Without a DHCP server
the static line in hosts.net stays in force.  Without a DaynaPORT the
boot says so ("/dev/en0: No such device", "slink failed") and goes on.
telnet and ftp are open for your own LAN as the stock system had them.
A /core in the root directory after every boot is the stock NFS mountd
crashing at start: harmless, and as it was.

Memory
------
A process may have 16 MB of data and 16 MB of address space by default,
both as hard limits.  The display manager raises both to 128 MB for the
sessions it starts, and /etc/profile does the same at root's logins;
another user's console or telnet login keeps the 16 MB (only root may
raise a hard limit), so run big programs such as xmanx under X.

Manx
----
  manx URL           the text browser (q quits, ? help)
  xmanx URL          the same under X (/usr/bin/X11)
  manxmail           IMAP/SMTP mail, see manxmail -h
HTTPS works once a user has run `manxtrust seed` once (type for a while:
the machine has no hardware randomness).  The CA bundle is
/usr/local/share/manx/cacert.pem, named by /etc/profile.manx.

NetUSBee
--------
Ethernet: the driver is in the kernel as nu0; `sh
/usr/local/lib/netusbee/nuup.sh ADDRESS NETMASK` brings it up by hand
(or `dhcpc -n /dev/nu0` to ask DHCP).  USB: the stack starts on the
card at boot (/etc/default/ub; `ubtool start lightning` moves it to a
Lightning VME); `ubtool devs` lists what it found, `ubtool hid` the
keyboard and mouse.  Without a card the USB driver stays idle and takes
no memory.  A USB stick appears as
/dev/dsk/usb0 (`mdir u:` with mtools).  Power-hungry devices such as a
backlit keyboard need a powered hub: the card feeds them from the
cartridge port.

Tests and tools
---------------
/usr/local/lib/sp1/tests holds the service pack's test programs (sound,
MIDI, the ATW800/2's video and 2D engine, the transputer links) with a
README that says what each one does and touches.  `setboot tos|unix|none`
sets the TT's NVRAM boot preference; `xatwrun -mode 640x480 PROGRAM`
runs one X program full screen on a second Xatw server (ATW800/2).

More software
-------------
  apkg update; apkg search NAME; apkg install NAME
installs AMIX packages from pkg.amigaux.org into /opt/amix.

Source and notes: github.com/slaapliedje/atari-sysv-sp1 (GPL-2.0).
