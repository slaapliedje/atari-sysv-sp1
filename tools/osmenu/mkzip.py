#!/usr/bin/env python3
"""mkzip.py OUT.ZIP: the package to hand out - OSMENU.PRG and MINTBOOT.PRG
(build.sh first), README.TXT and a sample OSMENU.INF with CR LF line
ends, the source in SRC, the GPL as COPYING."""
import os, sys, zipfile
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
def text(p):
    return open(p, 'rb').read().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
    for name in ('OSMENU.PRG', 'MINTBOOT.PRG'):
        z.write(os.path.join(here, name), 'OSMENU/' + name)
    z.writestr('OSMENU/README.TXT', text(os.path.join(here, 'README.TXT')))
    z.writestr('OSMENU/OSMENU.INF', text(os.path.join(here, 'OSMENU.INF')))
    z.writestr('OSMENU/COPYING', text(os.path.join(root, 'LICENSE')))
    for src, name in (('osmenu.s', 'OSMENU.S'), ('mintboot.s', 'MINTBOOT.S'), ('build.sh', 'BUILD.SH')):
        z.writestr('OSMENU/SRC/' + name, open(os.path.join(here, src), 'rb').read())
for i in zipfile.ZipFile(sys.argv[1]).infolist():
    print('%7d  %s' % (i.file_size, i.filename))
