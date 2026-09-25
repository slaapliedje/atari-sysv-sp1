#!/bin/sh
# kbuild.sh - compile a kernel driver on the PC for Atari System V.
#
#   sh kbuild.sh driver-snd/snd.c [...]
#
# Uses the cross GCC 2.7.2 (m68k-cbm-sysv4-gcc) against the ASV headers
# (its own sysroot holds them; ASV_SYSROOT picks another copy). The object
# lands next to the source; copy it to the TT's driver directory as
# NAME.pc.o and the driver's install.sh links it into the kernel instead
# of compiling there.
#
# Why not on the TT: its cc is GCC 1.40, which ignores volatile when
# optimising (a polling loop read its register once: the C011 link ran at
# 2 KB/s) and does not know "signed". GCC 2.7.2 has neither problem, so
# drivers are built with -O2. Avoid long long: 2.7.2 -O miscompiles
# long long && and ||.
set -e
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${ASV_SYSROOT:=$ASV_CROSS/m68k-cbm-sysv4/sysroot}"
CC="$ASV_CROSS/bin/m68k-cbm-sysv4-gcc"
for src in "$@"; do
	dir=$(dirname "$src")
	obj="${src%.c}.o"
	"$CC" -O2 -D_KERNEL -nostdinc -I"$dir" -I"$ASV_SYSROOT/usr/include" \
		-Wall -Wno-implicit -Wno-return-type -c "$src" -o "$obj"
	echo "built $obj"
done
