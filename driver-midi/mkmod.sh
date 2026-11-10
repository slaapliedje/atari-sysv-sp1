#!/bin/sh
# mkmod.sh - midi as a loadable module (sp1 dlm/), on the PC:
#   sh driver-midi/mkmod.sh EXPORTS OUTDIR
# EXPORTS: the kernel's symbol names (dlm/asv/build/exports).  The module
# lands in OUTDIR/midi; it goes to /etc/conf/mod.d on ASV, and
# `modadmin -r cdev midi 44` at boot registers its major.
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${ASV_SYSROOT:=$ASV_CROSS/m68k-cbm-sysv4/sysroot}"
dlm=$here/../dlm
o=$(mktemp -d); trap 'rm -rf "$o"' EXIT
"$ASV_CROSS/bin/m68k-cbm-sysv4-gcc" -O2 -D_KERNEL -DMIDI_MODULE -nostdinc -I"$here" \
	-I"$dlm/asv/build/src/include" -I"$ASV_SYSROOT/usr/include" \
	-Wall -Wno-implicit -Wno-return-type -c "$here/midi.c" -o "$o/midi.o"
sh "$dlm/asv/mkmod.sh" -e "$1" -o "$2" midi midi "" "$o/midi.o"
