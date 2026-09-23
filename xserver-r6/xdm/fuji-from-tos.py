#!/usr/bin/env python3
"""fuji-from-tos.py TOS.img OUT.xbm - the Atari Fuji and "ATARI" lettering
from the boot screen, taken from your own TOS ROM image (the art is Atari's
and is not distributed with sp1).

TOS 3.06 keeps it as an uncompressed 96x86 1-bit bitmap, 12 bytes a row
(at 0x35FE8 in the US ROM). It is found here by content - the row where the
wings are widest - so other ROMs carrying the same art work as well."""
import sys

W, H, STRIDE = 96, 86, 12
# row 39 of the logo: the three columns, wings at their widest
SIG = bytes.fromhex('000003ff81ff03ff8000')
SIG_ROW = 39

rom = open(sys.argv[1], 'rb').read()
pos = rom.find(SIG)
if pos < 0:
    sys.exit('no Fuji bitmap found in %s' % sys.argv[1])
base = pos - SIG_ROW * STRIDE
bm = rom[base:base + H * STRIDE]
# sanity: the top 16 rows (the straight tops of the three columns) are
# identical and not empty; a blind signature match would fail this
top = bm[:STRIDE]
if not any(top) or any(bm[y * STRIDE:(y + 1) * STRIDE] != top for y in range(16)):
    sys.exit('bitmap at %#x does not look like the Fuji' % base)
out = ['#define fuji_width %d' % W, '#define fuji_height %d' % H,
       'static unsigned char fuji_bits[] = {']
vals = []
for y in range(H):
    for x in range(STRIDE):
        b = bm[y * STRIDE + x]
        vals.append('0x%02x' % int('{:08b}'.format(b)[::-1], 2))  # XBM is LSB-first
for i in range(0, len(vals), 12):
    out.append('   ' + ', '.join(vals[i:i + 12]) + (',' if i + 12 < len(vals) else '};'))
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
print('Fuji %dx%d from %s at %#x' % (W, H, sys.argv[1], base))
