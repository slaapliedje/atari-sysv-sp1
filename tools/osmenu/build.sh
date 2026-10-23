#!/bin/sh
# OSMENU.PRG and MINTBOOT.PRG: GNU as for m68k (the SVR4 one in the ASV
# cross toolchain: & immediates, # comments), each program's text in a
# TOS header with an empty relocation table (everything is pc-relative).
set -e
cd "$(dirname "$0")"
AS=${AS:-$HOME/opt/asv-cross/bin/m68k-cbm-sysv4-as}
OBJCOPY=${OBJCOPY:-$HOME/opt/asv-cross/bin/m68k-cbm-sysv4-objcopy}
for p in osmenu mintboot; do
	$AS -m68000 -o $p.o $p.s
	$OBJCOPY -O binary $p.o $p.bin
	python3 - $p <<'PY'
import struct, sys
p = sys.argv[1]
t = open(p + '.bin', 'rb').read()
hdr = struct.pack('>HLLLLLLH', 0x601a, len(t), 0, 0, 0, 0, 0, 0)
open(p.upper() + '.PRG', 'wb').write(hdr + t + b'\0\0\0\0')
print(p.upper() + '.PRG:', 28 + len(t) + 4, 'bytes')
PY
	rm -f $p.o $p.bin
done
