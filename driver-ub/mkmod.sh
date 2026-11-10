#!/bin/sh
# mkmod.sh - ub as a loadable module (sp1 dlm/), on the PC:
#   sh driver-ub/mkmod.sh EXPORTS OUTDIR
# EXPORTS: the kernel's symbol names (dlm/asv/build/exports).  The module
# lands in OUTDIR/ub; it goes to /etc/conf/mod.d on ASV, and
# /etc/init.d/ub loads it before it starts the daemon; the kernel keeps
# an empty ub driver (major 46) whose rows it fills.
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${ASV_SYSROOT:=$ASV_CROSS/m68k-cbm-sysv4/sysroot}"
dlm=$here/../dlm
o=$(mktemp -d); trap 'rm -rf "$o"' EXIT
"$ASV_CROSS/bin/m68k-cbm-sysv4-gcc" -O2 -D_KERNEL -DUB_MODULE -nostdinc -I"$here" \
	-I"$dlm/asv/build/src/include" -I"$ASV_SYSROOT/usr/include" \
	-Wall -Wno-implicit -Wno-return-type -c "$here/ub.c" -o "$o/ub.o"
sh "$dlm/asv/mkmod.sh" -e "$1" -o "$2" ub ub "" "$o/ub.o"
