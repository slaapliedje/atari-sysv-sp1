#!/usr/bin/env python3
"""amixify.py FILE... - make AMIX (Amiga UNIX) ELF files use AMIX's own C
library on Atari System V, installed under /usr/amx instead of /usr/lib.
Everything is patched in place (same-length strings).

Programs: the interpreter and any NEEDED /usr/lib/libc.so.1 become
/usr/amx/libc.so.1.

AMIX's libc.so.1 (and ld.so.1), which is also its dynamic linker: its own
name, which it compares against the NEEDED libc.so.1 to recognise itself
(otherwise a second copy of libc is loaded and malloc sees no heap), and its
default library directory, /usr/lib, so that libsocket, libnsl and friends
come from /usr/amx without LD_LIBRARY_PATH.

Needs a kernel with the amx module (sp1/driver-amix): AMIX enters the
kernel with trap #0."""
import os, sys
OLD, NEW = b'/usr/lib/libc.so.1\0', b'/usr/amx/libc.so.1\0'
LIBDIR_OLD, LIBDIR_NEW = b'\0/usr/lib\0', b'\0/usr/amx\0'
for p in sys.argv[1:]:
    d = bytearray(open(p, 'rb').read())
    assert d[:4] == b'\x7fELF', p
    n = d.count(OLD)
    d = d.replace(OLD, NEW)
    m = 0
    if os.path.basename(p) in ('libc.so.1', 'ld.so.1'):
        m = d.count(LIBDIR_OLD)
        d = d.replace(LIBDIR_OLD, LIBDIR_NEW)
    open(p, 'wb').write(d)
    print('%s: %d libc reference(s), %d library dir(s) rewritten' % (p, n, m))
