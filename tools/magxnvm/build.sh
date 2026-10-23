#!/bin/sh
# MAGXNVM.PRG from magxnvm.s: GNU as (any m68k target), the text wrapped in
# a TOS program header with an empty relocation table.
set -e
cd "$(dirname "$0")"
AS=${AS:-$HOME/opt/asv-cross/bin/m68k-cbm-sysv4-as}
OBJCOPY=${OBJCOPY:-$HOME/opt/asv-cross/bin/m68k-cbm-sysv4-objcopy}
$AS -m68000 -o magxnvm.o magxnvm.s
$OBJCOPY -O binary magxnvm.o magxnvm.bin
python3 - <<'PY'
import struct
t = open('magxnvm.bin', 'rb').read()
hdr = struct.pack('>HLLLLLLH', 0x601a, len(t), 0, 0, 0, 0, 0, 0)
open('MAGXNVM.PRG', 'wb').write(hdr + t + b'\0\0\0\0')
print('MAGXNVM.PRG:', 28 + len(t) + 4, 'bytes')
PY
rm -f magxnvm.o magxnvm.bin
