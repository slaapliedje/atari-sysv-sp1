OSMENU - a boot menu for the AUTO folder
========================================

For the Atari TT030, Falcon030, Mega STE, STE and ST.  OSMENU.PRG runs
first in the AUTO folder of the boot drive, shows

    Boot:  1 TOS   2 MagiC   3 FreeMiNT   4 System V
           (MagiC in 5 s)

and starts the system whose digit you press.  Return, or the end of the
countdown, takes the default.  OSMENU.INF says which entries there are.

The entries
-----------
TOS        Nothing more: the AUTO folder goes on, then the desktop.

MagiC      Runs \MAGXBOOT.PRG, MagiC's booter (magic= names another
           place).  MagiC then runs the AUTO folder again by itself;
           OSMENU sees that MagiC is already there and steps aside.

FreeMiNT   Leaves the kernel's path (mint=) in \AUTO\OSMENU.OUT.
           MINTBOOT.PRG, the last program in AUTO, starts that kernel
           after the rest of the folder, so NVDI or another VDI is
           loaded first.

System V   Set the NVRAM boot preference: 0x40 Atari System V, 0x20
NetBSD     NetBSD, 0x10 Linux.  The hard disk's root-sector loader
Linux      (HDDriver's does this, and Atari System V's) uses it at
           power-up to boot the partition marked for that system.
           OSMENU then asks you to switch the computer off and on, and
           goes on with TOS until you do; on a TT a reset is not
           enough.  From then on the machine boots that system at
           every power-up, until the preference is set back to TOS:
           setboot under Atari System V, bootpref under NetBSD, or
           SETBOOT.PRG or BOOTCONF.APP under TOS.

           These entries appear only on a machine with NVRAM (TT,
           Falcon030, Mega STE).  List only systems that are installed:
           a preference that matches no partition leaves the machine
           without its hard disk at the next power-up, until SETBOOT
           or BOOTCONF from a floppy sets it back.

OSMENU.INF
----------
In the AUTO folder, plain text, one setting a line (only the first 512
bytes are read; lines that are not settings are ignored):

    menu=tos,magic,mint,sysv     the entries, in this order.  The names:
                                 tos, magic, mint (or freemint),
                                 sysv (or unix), netbsd, linux
    default=magic                taken on Return or when the time is up;
                                 without it, the first entry
    timeout=5                    seconds, at most 99.  0: no menu, the
                                 default is started at once
    mint=C:\MINT\MINT030.PRG     the FreeMiNT kernel, or a loader for it
    magic=C:\MAGXBOOT.PRG        MagiC's booter, if not \MAGXBOOT.PRG

Without menu= (or without OSMENU.INF) the entries are TOS, MagiC if its
booter is there, and FreeMiNT if mint= names a file that is there and
\AUTO\MINTBOOT.PRG is there too.  The NVRAM entries are never added by
themselves.  With only one entry there is no menu: it is started at
once.  If the NVRAM preference is MagiC's (0x08, as BOOTCONF's MagiC
button sets it), MagiC is the default.

Installing
----------
1. OSMENU.PRG goes into C:\AUTO as the first program.  TOS runs the
   AUTO folder in the order the files were copied in: move the other
   programs out, copy OSMENU.PRG in, then copy them back.
2. MagiC: MAGXBOOT.PRG at the top of C: (out of AUTO, or MagiC starts
   every time), MAGIC.RAM where it was.
3. FreeMiNT: the kernel out of AUTO (into C:\MINT, say) and named with
   mint=, and MINTBOOT.PRG copied into C:\AUTO last.
4. OSMENU.INF into C:\AUTO, edited to suit.

Source
------
SRC holds the source and build.sh (GNU as for m68k in its SVR4
syntax).  Both programs are position-independent, with no relocation
table.  OSMENU comes from https://github.com/slaapliedje/atari-sysv-sp1
(tools/osmenu), where Atari System V's setboot is too.

Tested in Hatari on a TT (TOS 3.06), a Falcon030 (TOS 4.04), an STE
(TOS 2.06) and an ST (TOS 1.04): every entry, the countdown, Return, the
NVRAM writes, and the entries left out without NVRAM.

Copyright 2026 Slaapliedje.  GNU General Public License version 2: see
COPYING.
