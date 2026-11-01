#!/usr/bin/env python3
"""mkfloppy.py OUT.ST: a 720 KB floppy for a TT that comes up on the TOS
desktop when it should boot Atari System V. \\AUTO\\OSMENU.PRG (build.sh
first) with an OSMENU.INF offering TOS and System V, and a README.TXT.
Choosing System V sets the NVRAM boot preference; the TT then boots Unix
from the next power-up on.

The image is FAT12 with a PC-style boot sector (720 KB, 2 sectors a
cluster, 112 root entries), which TOS reads and PCs and Gotek drives
take; its boot sector is not executable (the word sum is not 0x1234)."""
import os, struct, sys

here = os.path.dirname(os.path.abspath(__file__))

INF = """menu=tos,sysv
default=tos
timeout=15
; Press 2 for Atari System V: the TT's NVRAM boot preference is set to it,
; and the TT boots Unix from its next power-up on (switch it off and on;
; a reset is not enough).  setboot tos on the Unix side, or BOOTCONF.APP
; on TOS, switches back.
"""

README = """OSMENU boot floppy for the Atari TT030

For a TT that comes up on the TOS desktop although its hard disk (or
SCSI card) holds Atari System V: its NVRAM boot preference says TOS, so
the disk's boot loader passes over the Unix partition.

1. Put this floppy in drive A: and switch the TT on.
2. TOS runs \\AUTO\\OSMENU.PRG from the floppy, which shows
       Boot:  1 TOS   2 System V
3. Press 2.  The NVRAM boot preference is now Atari System V.
4. Switch the TT off and on (a reset is not enough).  It boots Unix,
   with or without the floppy in the drive.

This needs Atari's TOS in the TT's ROM (TOS 3.06).  EmuTOS gives the
Unix partition a drive letter and then does not run the disk's boot
loader at all, whatever the boot preference says: under EmuTOS neither
this floppy nor BOOTCONF.APP can start System V.

To go back to TOS: `setboot tos` as root on the Unix side, or
BOOTCONF.APP on TOS.  If you do not press a key, the menu goes on to
TOS after 15 seconds and changes nothing.

\\AUTO\\OSMENU.INF holds the menu.  OSMENU.ZIP, next to this floppy on
the release page, has the full boot menu for a TOS partition (TOS,
MagiC, FreeMiNT, System V, NetBSD, Linux), its README and the source.

OSMENU is free software under the GNU GPL version 2:
github.com/slaapliedje/atari-sysv-sp1, tools/osmenu.
"""

SEC, SPC, RES, NFAT, NROOT, TOTAL, MEDIA, SPF, SPT, HEADS = 512, 2, 1, 2, 112, 1440, 0xF9, 3, 9, 2
ROOT0 = RES + NFAT * SPF                    # first root directory sector
DATA0 = ROOT0 + NROOT * 32 // SEC           # first data sector (cluster 2)
CLUS = SEC * SPC
DATE, TIME = (2026 - 1980) << 9 | 11 << 5 | 1, 12 << 11     # 2026-11-01 12:00

def crlf(s):
    return s.replace('\n', '\r\n').encode('ascii')

img = bytearray(SEC * TOTAL)
fat = [0xF00 | MEDIA, 0xFFF]                # entries 0 and 1
def alloc(data):
    """data into fresh contiguous clusters; returns the first cluster"""
    n = max(1, -(-len(data) // CLUS))
    first = len(fat)
    for i in range(n):
        fat.append(first + i + 1 if i < n - 1 else 0xFFF)
    off = (DATA0 + (first - 2) * SPC) * SEC
    img[off:off + len(data)] = data
    return first

def entry(name, ext, attr, clus, size):
    return struct.pack('<8s3sB10sHHHI', name.ljust(8).encode(), ext.ljust(3).encode(),
                       attr, bytes(10), TIME, DATE, clus, size)

prg = open(os.path.join(here, 'OSMENU.PRG'), 'rb').read()
auto = len(fat); fat.append(0xFFF)          # AUTO's one cluster, filled below
c_prg = alloc(prg)
c_inf = alloc(crlf(INF))
c_rd = alloc(crlf(README))
autodir = (entry('.', '', 0x10, auto, 0) + entry('..', '', 0x10, 0, 0)
           + entry('OSMENU', 'PRG', 0x20, c_prg, len(prg))
           + entry('OSMENU', 'INF', 0x20, c_inf, len(crlf(INF))))
off = (DATA0 + (auto - 2) * SPC) * SEC
img[off:off + len(autodir)] = autodir
root = (entry('OSMENU', '', 0x08, 0, 0) + entry('AUTO', '', 0x10, auto, 0)
        + entry('README', 'TXT', 0x20, c_rd, len(crlf(README))))
img[ROOT0 * SEC:ROOT0 * SEC + len(root)] = root

f = bytearray(SPF * SEC)                    # FAT12: two entries in three bytes
for i, v in enumerate(fat):
    o = i * 3 // 2
    if i & 1:
        f[o] = (f[o] & 0x0F) | (v << 4 & 0xF0); f[o + 1] = v >> 4 & 0xFF
    else:
        f[o] = v & 0xFF; f[o + 1] = (f[o + 1] & 0xF0) | (v >> 8 & 0x0F)
for k in range(NFAT):
    img[(RES + k * SPF) * SEC:(RES + (k + 1) * SPF) * SEC] = f

boot = bytearray(SEC)
boot[0:3] = b'\xeb\x3c\x90'
boot[3:11] = b'OSMENU  '
boot[11:30] = struct.pack('<HBHBHHBHHH', SEC, SPC, RES, NFAT, NROOT, TOTAL, MEDIA, SPF, SPT, HEADS)
boot[0x27:0x2B] = b'\x20\x26\x11\x01'       # volume serial
boot[0x2B:0x36] = b'OSMENU     '
boot[0x36:0x3E] = b'FAT12   '
boot[510:512] = b'\x55\xaa'
if sum(struct.unpack('>256H', boot)) & 0xFFFF == 0x1234:
    boot[0x3E] ^= 1                         # not an executable boot sector
img[0:SEC] = boot
open(sys.argv[1], 'wb').write(img)
print('%s: %d bytes, %d clusters used' % (sys.argv[1], len(img), len(fat) - 2))
