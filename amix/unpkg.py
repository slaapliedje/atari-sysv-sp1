#!/usr/bin/env python3
"""unpkg.py PKG OUTDIR - unpack an SVR4 package datastream (pkgtrans format):
a text header padded to 512 bytes, then cpio archives (package info first,
then the payload) one after another, each ending in TRAILER!!!.

Archive boundaries come from walking the cpio headers, not from searching
for the trailer's name: cpio(1) itself contains the string TRAILER!!!."""
import sys, os, subprocess

def archive_end(d, pos):
    """end of the cpio archive at pos (after its TRAILER!!! entry)"""
    while True:
        magic = d[pos:pos+6]
        if magic in (b'070701', b'070702'):		# newc / crc: 4-byte aligned
            h = d[pos:pos+110]
            size, namesize = int(h[54:62], 16), int(h[94:102], 16)
            name = d[pos+110:pos+110+namesize-1]
            p = pos + 110 + namesize
            p = (p + 3) & ~3
            p += size
            p = (p + 3) & ~3
        elif magic == b'070707':			# odc: no padding
            h = d[pos:pos+76]
            namesize, size = int(h[59:65], 8), int(h[65:76], 8)
            name = d[pos+76:pos+76+namesize-1]
            p = pos + 76 + namesize + size
        else:
            raise SystemExit('bad cpio header at %d' % pos)
        pos = p
        if name == b'TRAILER!!!':
            return pos

d = open(sys.argv[1], 'rb').read()
out = sys.argv[2]; os.makedirs(out, exist_ok=True)
pos = (d.index(b'# end of header') // 512 + 1) * 512
n = 0
while pos < len(d):
    if d[pos:pos+6] not in (b'070701', b'070702', b'070707'):
        nxt = min([p for p in (d.find(b'070701', pos), d.find(b'070707', pos)) if p >= 0] or [len(d)])
        pos = nxt; continue
    end = archive_end(d, pos)
    n += 1
    sub = os.path.join(out, 'part%d' % n); os.makedirs(sub, exist_ok=True)
    subprocess.run(['cpio', '-idm', '--quiet', '--no-absolute-filenames'], input=d[pos:end], cwd=sub)
    pos = (end + 511) // 512 * 512
print('%s: %d archives' % (sys.argv[1], n))
