#!/bin/sh
# build.sh - GNU bash 5.2.37 as a static AMIX program, cross-built on the PC.
# It runs on AMIX, and on Atari System V with the amx module (../../driver-amix).
#
#   AMIX_SYSROOT=../work/sysroot sh build.sh     # -> work/bash (stripped)
#
# Needs gcc-cross-amix at ~/opt/asv-cross (or ASV_CROSS=), the AMIX sysroot
# from ../mksysroot.sh, and on the PC curl, gpg (optional), bison, gcc.
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${AMIX_SYSROOT:?set AMIX_SYSROOT (see ../mksysroot.sh)}"
AMIX_SYSROOT=$(cd "$AMIX_SYSROOT" && pwd)
export ASV_CROSS AMIX_SYSROOT PATH="$ASV_CROSS/bin:$PATH"
V=5.2.37
SHA256=9599b22ecd1d5787ad7d3b7bf0c59f312b3396d1e281175dd1f8a4014da621ff
work=${WORK:-$here/work}
mkdir -p "$work"
cd "$work"

[ -f bash-$V.tar.gz ] || curl -sfLO https://ftp.gnu.org/gnu/bash/bash-$V.tar.gz
echo "$SHA256  bash-$V.tar.gz" | sha256sum -c --quiet
rm -rf bash-$V build
tar xzf bash-$V.tar.gz
(cd bash-$V && patch -p1 -s < "$here/bash-$V-amix.patch")

mkdir build && cd build
# CC links statically against AMIX's libc.a (the shared libc.so.1 lacks
# select, getpwent, siglongjmp...), so configure sees what the link will.
# sys/file.h is AMIX's kernel header, of no use to bash.
CC="$here/amix-static-cc" AR=m68k-cbm-sysv4-ar RANLIB=m68k-cbm-sysv4-ranlib \
CC_FOR_BUILD=gcc CFLAGS="-O -D__amix" ac_cv_header_sys_file_h=no \
	../bash-$V/configure --host=m68k-unknown-sysv4 --build="$(gcc -dumpmachine)" \
	--prefix=/usr/local --without-bash-malloc --disable-nls > configure.log 2>&1 ||
	{ tail -20 configure.log; exit 1; }
# the build-host tools (mksignames, mksyntax, ...) see the target's config.h
make -j"$(nproc)" CC_FOR_BUILD="gcc -std=gnu89 -include $here/hostfix.h" > make.log 2>&1 ||
	{ grep -E ':[0-9]+: |Error' make.log | grep -v warning | tail -20; exit 1; }
cp bash "$work/bash"
m68k-cbm-sysv4-strip "$work/bash"
ls -l "$work/bash"
