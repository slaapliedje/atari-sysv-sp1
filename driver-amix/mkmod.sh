#!/bin/sh
# mkmod.sh - amx as a loadable module (sp1 dlm/), on the PC:
#   sh driver-amix/mkmod.sh EXPORTS OUTDIR
# EXPORTS: the kernel's symbol names (dlm/asv/build/exports).  The module
# lands in OUTDIR/amx; it goes to /etc/conf/mod.d on ASV, and
# /etc/init.d/dlm loads it at boot (/etc/conf/mod_load).  -traditional:
# ASV's headers declare struct user's signal types only when __STDC__
# is 0, as the TT's own cc (install.sh) has it.
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${ASV_SYSROOT:=$ASV_CROSS/m68k-cbm-sysv4/sysroot}"
dlm=$here/../dlm
o=$(mktemp -d); trap 'rm -rf "$o"' EXIT
"$ASV_CROSS/bin/m68k-cbm-sysv4-gcc" -O2 -traditional -fno-builtin -D_KERNEL -DAMX_MODULE -nostdinc -I"$here" \
	-I"$dlm/asv/build/src/include" -I"$ASV_SYSROOT/usr/include" \
	-Wall -Wno-implicit -Wno-return-type -c "$here/amx.c" -o "$o/amx.o"
sh "$dlm/asv/mkmod.sh" -e "$1" -o "$2" amx amx "" "$o/amx.o"
