#!/usr/bin/env python3
"""Patch a stock Atari System V disk image so it boots on a TT030 with a large
TT-RAM board and/or an ATW800/2 graphics card.

    patch-asv-image.py asv_boot.img [--ttram-cap MB] [--no-la-move]

Applies, to EVERY kernel and kernel object on the image:

 1. TT-RAM cap (default 96 MB). The stock kernels size TT-RAM by probing up
    to 2 GB and keep the per-page bookkeeping in ST-RAM; with 256 MB fitted
    that bookkeeping outgrows 4 MB of ST-RAM and kvm_init() panics before the
    console exists, so the machine reset-loops. The ceiling is set by ST-RAM
    (about 21 KB per MB of RAM): 4 MB ST-RAM -> 96 MB is safe, 128 MB works
    in emulation but leaves ST-RAM nearly full.

 2. Lance probe move. edt_data tells the LA (Riebl ethernet) driver to probe
    0xFEC0FFF0 / 0xFEC10000, which on a TT is inside the ATW800/2's video
    memory. Video RAM reads back, so the kernel "finds" an ethernet card that
    is really the framebuffer and hangs in lainit. The probe is moved to
    0x0080FFF0 (unpopulated ST-RAM: bus-errors on real hardware).

The image is patched IN PLACE; keep a copy. Tested against Richard's image
(md5 54a30c77cf845284908258bea14e5673); other images are patched only where
the same byte patterns are found, and the script reports what it changed.
"""
import mmap, re, struct, sys

def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    cap_mb, move_la = 96, True
    path = args[0]
    if '--ttram-cap' in args:
        cap_mb = int(args[args.index('--ttram-cap') + 1])
    if '--no-la-move' in args:
        move_la = False
    f = open(path, 'r+b')
    m = mmap.mmap(f.fileno(), 0)

    # 1. pdesc[]: {0, 0xA00000, 0xC0000000, 0,0,0,0} then {0x1000000, LIMIT, 0xA0000000, ...}
    st = struct.pack('>7I', 0, 0xA00000, 0xC0000000, 0, 0, 0, 0)
    n = 0
    for x in re.finditer(re.escape(st) + re.escape(struct.pack('>I', 0x1000000)) +
                         b'....' + re.escape(struct.pack('>I', 0xA0000000)), m, re.S):
        o = x.start() + 28 + 4
        old = struct.unpack('>I', m[o:o + 4])[0]
        m[o:o + 4] = struct.pack('>I', cap_mb << 20)
        n += 1
        print('TT-RAM table at 0x%x: limit 0x%x -> %d MB' % (x.start(), old, cap_mb))
    print('%d TT-RAM table(s) patched' % n)

    # 2. edt_data text: "LA ... 0xfec0fff0 ... 0xfec10000"
    if move_la:
        k = 0
        for x in re.finditer(rb'LA\s+-\s+0\s+0xfec0fff0(\s+-\s+)0xfec10000', m):
            s, e = x.start(), x.end()
            new = m[s:e].replace(b'0xfec0fff0', b'0x0080fff0').replace(b'0xfec10000', b'0x00810000')
            m[s:e] = new
            k += 1
            print('edt_data LA line at 0x%x moved to 0x0080fff0' % s)
        print('%d edt_data line(s) patched' % k)
    m.flush()

if __name__ == '__main__':
    main()
