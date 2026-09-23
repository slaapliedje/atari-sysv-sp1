#!/usr/bin/env python3
"""libcextra.py LIBC_A LIBC_SO SYMBOL... - the libc.a members a program needs
for SYMBOLs that Atari System V's shared libc does not export (setitimer,
sys_errlist, syscall, vfork, the utmpx calls ...), closed over their own
undefined references, stopping at what libc.so.1 does export. Prints the
member names, one per line, for `ar x`."""
import subprocess, sys

nm = 'm68k-cbm-sysv4-nm'
def run(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout

libc_a, libc_so, wanted = sys.argv[1], sys.argv[2], sys.argv[3:]
exported = set()
for l in run('readelf', '-W', '-s', libc_so).splitlines():
    p = l.split()
    if len(p) >= 8 and p[6] != 'UND' and p[4] in ('GLOBAL', 'WEAK'):
        exported.add(p[7])
defs, undefs, member = {}, {}, None
for l in run(nm, libc_a).splitlines():
    if l.endswith('.o:'):
        member = l[:-1]; undefs[member] = set(); continue
    p = l.split()
    if len(p) == 3 and p[1] in 'TDWBR':
        defs.setdefault(p[2], member)
    elif len(p) == 2 and p[0] == 'U':
        undefs[member].add(p[1])
need, todo = [], list(wanted)
while todo:
    s = todo.pop()
    if s in exported or s not in defs or defs[s] in need:
        continue
    m = defs[s]; need.append(m)
    todo.extend(undefs[m])
missing = [s for s in wanted if s not in defs and s not in exported]
if missing:
    sys.exit('not in libc.a: ' + ' '.join(missing))
print('\n'.join(need))
