#!/usr/bin/env python3
# oracle.py -- reference side of the relocation oracle.
#
#   oracle.py script  module layout            > link.ld
#   oracle.py commons module ldout             > commons
#   oracle.py compare module layout ldout image
#
# layout is reloc -L output ("index address size" per placed section,
# then "COMMON address size").  The script places every section of the
# module at the address the loader chose, and COMMON at the loader's
# common area.  compare checks the loader's image byte for byte against
# the sections of the m68k-elf-ld output: each placed section, the
# padding between them (zero) and the common area (zero).
import struct, sys


def elf(path):
    d = open(path, 'rb').read()
    u16 = lambda o: struct.unpack('>H', d[o:o+2])[0]
    u32 = lambda o: struct.unpack('>I', d[o:o+4])[0]
    shoff, shnum, shstr = u32(32), u16(48), u16(50)
    secs = []
    for i in range(shnum):
        b = shoff + 40 * i
        secs.append(dict(name=u32(b), type=u32(b+4), flags=u32(b+8), addr=u32(b+12),
                         off=u32(b+16), size=u32(b+20), link=u32(b+24)))
    so = secs[shstr]['off']
    for s in secs:
        s['nm'] = d[so + s['name']:d.index(b'\0', so + s['name'])].decode()
    syms = []
    for s in secs:
        if s['type'] == 2:
            st = secs[s['link']]['off']
            for k in range(s['size'] // 16):
                b = s['off'] + 16 * k
                n = u32(b)
                syms.append(dict(name=d[st+n:d.index(b'\0', st+n)].decode(),
                                 value=u32(b+4), size=u32(b+8), info=d[b+12], shndx=u16(b+14)))
    return d, secs, syms


def layout(path):
    secs, com = [], None
    for l in open(path):
        p = l.split()
        if p[0] == 'COMMON':
            com = (int(p[1], 16), int(p[2], 16))
        else:
            secs.append((int(p[0]), int(p[1], 16), int(p[2], 16)))
    return secs, com


def script(module, lay):
    _, secs, _ = elf(module)
    placed, com = layout(lay)
    print('SECTIONS\n{')
    for idx, addr, size in placed:
        n = secs[idx]['nm']
        if size == 0:
            continue
        print('\t%s 0x%x : { *(%s) }' % (n, addr, n))
    print('\t.dlmcommon 0x%x : { *(COMMON) }' % com[0])
    print('\t/DISCARD/ : { *(.comment) }')
    print('}')


def commons(module, ldout):
    _, _, msyms = elf(module)
    _, _, lsyms = elf(ldout)
    want = {s['name'] for s in msyms if s['shndx'] == 0xfff2}
    for s in lsyms:
        if s['name'] in want and s['shndx'] not in (0, 0xfff1):
            print('%s 0x%x' % (s['name'], s['value']))


def compare(module, lay, ldout, image):
    _, msecs, _ = elf(module)
    ld, lsecs, _ = elf(ldout)
    img = open(image, 'rb').read()
    placed, com = layout(lay)
    base = min([a for _, a, _ in placed] + [com[0]])
    ref = bytearray(len(img))
    bad = 0
    for idx, addr, size in placed:
        n = msecs[idx]['nm']
        if size == 0:
            continue
        ls = [s for s in lsecs if s['nm'] == n]
        if len(ls) != 1 or ls[0]['addr'] != addr or ls[0]['size'] != size:
            print('FAIL %s: ld placed it differently' % n)
            bad += 1
            continue
        if ls[0]['type'] != 8:
            ref[addr - base:addr - base + size] = ld[ls[0]['off']:ls[0]['off'] + size]
    if img != bytes(ref):
        diffs = [i for i in range(len(img)) if img[i] != ref[i]]
        print('FAIL %d bytes differ, first at +0x%x (0x%x): loader %02x ld %02x' %
              (len(diffs), diffs[0], base + diffs[0], img[diffs[0]], ref[diffs[0]]))
        bad += 1
    return bad


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'script':
        script(*sys.argv[2:4])
    elif cmd == 'commons':
        commons(*sys.argv[2:4])
    elif cmd == 'compare':
        sys.exit(1 if compare(*sys.argv[2:6]) else 0)
