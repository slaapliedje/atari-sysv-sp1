#!/usr/bin/env python3
"""Generate the four label sectors for a NEW data disk under Atari System V.

    mklabel.py SIZE_MB [big_slice_MB] > label.bin

then on the TT (as root, DISK MUST BE BLANK - this destroys it):

    dd if=label.bin of=/dev/rdsk/cNd0sf bs=512 count=4
    prtvtoc /dev/rdsk/cNd0s1
    mkfs -F ufs -o nsect=64,ntrack=16,bsize=8192,fragsize=1024 /dev/rdsk/cNd0s1 <sectors of s1>
    mkfs -F ufs -o nsect=64,ntrack=16,bsize=8192,fragsize=1024 /dev/rdsk/cNd0s2 <sectors of s2>

N = the disk's SCSI id (/dev/dsk/c1d0.. c3d0 exist on the stock image).
Layout: s0 = 1020-sector boot slice (unused), s1 = big_slice (default 3/4 of
the disk), s2 = the rest, sf = whole disk. ASV's own `format -w -d` cannot do
this (its description-file parser is broken), so the label is written raw:
sector 0 Atari root sector, 1 empty, 2 pdsector, 3 VTOC - modelled on the
stock boot disk's.
"""
import struct, sys

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    N = int(sys.argv[1]) * 2048                     # 512-byte sectors
    big = int(sys.argv[2]) * 2048 if len(sys.argv) > 2 else (N * 3 // 4) & ~1023
    s0 = bytearray(512)
    s0[0x1c2:0x1c6] = struct.pack('>I', N)
    s0[0x1c6:0x1ca] = b'\x41UNX'
    s0[0x1ca:0x1d2] = struct.pack('>II', 1, N - 1)
    s1 = bytes(512)
    pd = bytearray(512)
    struct.pack_into('>III', pd, 0, 0, 0xCA5E600D, 0)
    struct.pack_into('>4I', pd, 24, N // 1024, 16, 64, 512)   # cyls, tracks, sectors, bytes
    struct.pack_into('>II', pd, 84, 4, N)                    # allocatable start/end
    struct.pack_into('>I', pd, 136, N)                       # devsp[1]: whole-disk size
    v = bytearray(512)
    struct.pack_into('>II', v, 12, 0x600DDEEE, 1)
    v[20:28] = b'data\0\0\0\0'
    struct.pack_into('>HH', v, 28, 512, 16)
    parts = {0: (1, 1, 4, 1020), 1: (4, 0, 1024, big), 2: (4, 0, 1024 + big, N - 1024 - big),
             15: (7, 1, 0, N)}
    for i in range(16):
        tag, flag, st, sz = parts.get(i, (0, 1, 0, 0))
        struct.pack_into('>HHii', v, 72 + 12 * i, tag, flag, st, sz)
    sys.stdout.buffer.write(bytes(s0) + s1 + bytes(pd) + bytes(v))
    sys.stderr.write('s1: %d sectors (%d MB)  s2: %d sectors (%d MB)\n' %
                     (big, big // 2048, N - 1024 - big, (N - 1024 - big) // 2048))

if __name__ == '__main__':
    main()
