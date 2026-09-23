#!/usr/bin/env python3
"""fixneeded.py ELF: rewrite DT_NEEDED entries that carry an absolute host
path (the cross wrapper hands ld resolved paths, and ASV's shared libraries
have no SONAME) to the bare library name, in place in .dynstr."""
import struct, sys

p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
assert d[:4] == b'\x7fELF' and d[5] == 2, 'big-endian ELF32 expected'
shoff, = struct.unpack('>I', d[32:36])
shentsize, shnum, shstrndx = struct.unpack('>HHH', d[46:52])
secs = []
for i in range(shnum):
    o = shoff + i * shentsize
    name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack('>10I', d[o:o + 40])
    secs.append((name, typ, off, size, link))
shstr = secs[shstrndx]
def secname(n):
    s = d[shstr[2] + n:]
    return s[:s.index(b'\0')].decode()
dyn = next(s for s in secs if s[1] == 6)          # SHT_DYNAMIC
dynstr = secs[dyn[4]]                             # sh_link -> .dynstr
for o in range(dyn[2], dyn[2] + dyn[3], 8):
    tag, val = struct.unpack('>II', d[o:o + 8])
    if tag == 1:                                  # DT_NEEDED
        so = dynstr[2] + val
        s = d[so:]
        name = s[:s.index(b'\0')].decode()
        # a host path (any absolute path that is not ASV's own /usr/lib)
        if name.startswith('/') and not name.startswith('/usr/lib/'):
            base = name.rsplit('/', 1)[1].encode() + b'\0'
            d[so:so + len(base)] = base
            print('NEEDED %s -> %s' % (name, base[:-1].decode()))
open(p, 'wb').write(d)
