#!/bin/sh
# mkmod.sh - dp as a loadable module (sp1 dlm/), on the PC:
#   sh driver-dp/mkmod.sh EXPORTS OUTDIR
# EXPORTS: the kernel's symbol names (dlm/asv/build/exports).  The module
# lands in OUTDIR/dp; it goes to /etc/conf/mod.d on ASV, and
# `modadmin -r sdev dp 41` at boot registers its major.
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${ASV_SYSROOT:=$ASV_CROSS/m68k-cbm-sysv4/sysroot}"
dlm=$here/../dlm
o=$(mktemp -d); trap 'rm -rf "$o"' EXIT
"$ASV_CROSS/bin/m68k-cbm-sysv4-gcc" -O2 -D_KERNEL -DDP_MODULE -nostdinc -I"$here" \
	-I"$dlm/asv/build/src/include" -I"$ASV_SYSROOT/usr/include" \
	-Wall -Wno-implicit -Wno-return-type -c "$here/dp.c" -o "$o/dp.o"
sh "$dlm/asv/mkmod.sh" -e "$1" -o "$2" dp dp "" "$o/dp.o"
