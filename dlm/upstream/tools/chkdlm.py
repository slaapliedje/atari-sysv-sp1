#!/usr/bin/env python3
# chkdlm.py -- static checks of the loadable-module core in a fully
# linked kernel image (after mkksym).
#
#   chkdlm.py image.elf
#
# - sysent has 142 entries and slots 64..69 still hold nosys, so the
#   claim check in dlm_init passes at boot;
# - dlm_init's table maps 64..69 to the six handlers with the right
#   argument counts (dlm_init copies it into sysent);
# - startup() calls the dmainit that calls dlm_init;
# - dlm_ksym is 160 KiB of .data holding a valid table (mkksym -c does
#   the per-symbol check).
# One OK/FAIL line per check; exit 1 on any FAIL.
import struct, subprocess, sys

elf = sys.argv[1]
d = open(elf, 'rb').read()
u16 = lambda o: struct.unpack('>H', d[o:o+2])[0]
u32 = lambda o: struct.unpack('>I', d[o:o+4])[0]
shoff, shnum, shstr = u32(32), u16(48), u16(50)
secs = []
for i in range(shnum):
    b = shoff + 40 * i
    secs.append(dict(name=u32(b), type=u32(b+4), addr=u32(b+12), off=u32(b+16),
                     size=u32(b+20), link=u32(b+24)))
so = secs[shstr]['off']
for s in secs:
    s['nm'] = d[so + s['name']:d.index(b'\0', so + s['name'])].decode()
sym = {}
for s in secs:
    if s['type'] == 2:
        st = secs[s['link']]['off']
        for k in range(s['size'] // 16):
            b = s['off'] + 16 * k
            n = u32(b)
            nm = d[st+n:d.index(b'\0', st+n)].decode()
            if u16(b + 14):
                sym.setdefault(nm, (u32(b + 4), u32(b + 8), u16(b + 14)))


def rd(a, n):
    for s in secs:
        if s['type'] == 1 and s['addr'] <= a and a + n <= s['addr'] + s['size']:
            return d[s['off'] + a - s['addr']:s['off'] + a - s['addr'] + n]
    raise SystemExit('FAIL address 0x%x not in the image' % a)


L = lambda a: struct.unpack('>I', rd(a, 4))[0]
bad = 0


def check(ok, msg):
    global bad
    print(('OK   ' if ok else 'FAIL ') + msg)
    bad |= not ok


se, ss = sym['sysent'][0], L(sym['sysentsize'][0])
check(ss == 142, 'sysentsize = %d' % ss)
free = [n for n in range(64, 70) if L(se + 8 * n + 4) == sym['nosys'][0]]
check(len(free) == 6, 'sysent[64..69] hold nosys at link time (dlm_init claims them)')
want = [(64, 1, 'dlm_modload'), (65, 1, 'dlm_moduload'), (66, 1, 'dlm_modpath'),
        (67, 3, 'dlm_modstat'), (68, 3, 'dlm_modadm'), (69, 3, 'dlm_getksym')]
t = sym['dlm_calls'][0]
got = [(L(t + 12 * i), L(t + 12 * i + 4), L(t + 12 * i + 8)) for i in range(6)]
ok = all(g == (n, a, sym[h][0]) for g, (n, a, h) in zip(got, want))
check(ok, 'dlm_calls: ' + ', '.join('%d/%d -> %s 0x%x' % (n, a, h, sym[h][0]) for n, a, h in want))
check(sym['dlm_ksym'][1] == 160 * 1024 and
      secs[sym['dlm_ksym'][2]]['nm'] == '.data', 'dlm_ksym: 160 KiB in .data')
check(not any(s['nm'] == '.ksym' for s in secs), 'no separate .ksym section')

dis = subprocess.run(['m68k-elf-objdump', '-d', '--start-address=0x%x' % sym['startup'][0],
                      '--stop-address=0x%x' % (sym['startup'][0] + (sym['startup'][1] or 0x100)), elf],
                     capture_output=True, text=True).stdout
check('<dmainit>' in dis and ('%x' % sym['dmainit'][0]) in dis,
      'startup calls dmainit at 0x%x' % sym['dmainit'][0])
dis = subprocess.run(['m68k-elf-objdump', '-d', '--start-address=0x%x' % sym['dmainit'][0],
                      '--stop-address=0x%x' % (sym['dmainit'][0] + 0x20), elf],
                     capture_output=True, text=True).stdout
check('<dlm_init>' in dis and '<plat_dmainit>' in dis, 'dmainit calls dlm_init, then plat_dmainit')
sys.exit(bad)
