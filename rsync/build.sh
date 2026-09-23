#!/bin/sh
# build.sh - rsync 3.4.1 for Atari System V, cross-built on the PC as a
# static program (no shared libraries needed on the TT).
#
#   sh build.sh          # -> work/rsync
#
# Needs gcc-cross-amix at ~/opt/asv-cross (or ASV_CROSS=) with the ASV
# sysroot. The fixincluded shadow of it (../xserver-r6/mksysroot.sh) is made
# in work/sysroot unless ASV_SYSROOT names one already made.
#
# Built WITHOUT optimisation, deliberately: gcc 2.7.2's optimiser
# miscompiles `long long' comparisons joined by && or || on the 68k (both
# halves false, the combination false as well), and rsync's file sizes
# and offsets are 64-bit. At -O, popt rejected every numeric option. The
# 030 is not the bottleneck anyway: the network is.
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
V=3.4.1
SHA256=2924bcb3a1ed8b551fc101f740b9f0fe0a202b115027647cf69850d65fd88c52
work=${WORK:-$here/work}
mkdir -p "$work"
if [ -z "$ASV_SYSROOT" ]; then
	ASV_SYSROOT=$work/sysroot
	[ -d "$ASV_SYSROOT" ] || sh "$here/../xserver-r6/mksysroot.sh" \
		"$ASV_CROSS/m68k-cbm-sysv4/sysroot" "$ASV_SYSROOT"
fi
ASV_SYSROOT=$(cd "$ASV_SYSROOT" && pwd)
export ASV_CROSS ASV_SYSROOT PATH="$ASV_CROSS/bin:$PATH"
cd "$work"

[ -f rsync-$V.tar.gz ] || curl -sfLO https://download.samba.org/pub/rsync/src/rsync-$V.tar.gz
echo "$SHA256  rsync-$V.tar.gz" | sha256sum -c --quiet
rm -rf rsync-$V build
tar xzf rsync-$V.tar.gz
(cd rsync-$V && patch -p1 -s < "$here/rsync-$V-asv.patch")

mkdir build && cd build
CC="$here/asv-static-cc" CFLAGS=-O0 CPPFLAGS="-I$here/include" LIBS=-lsocket \
	../rsync-$V/configure --host=m68k-unknown-sysv4 --build="$(gcc -dumpmachine)" \
	--prefix=/usr/local --disable-openssl --disable-xxhash --disable-zstd \
	--disable-lz4 --disable-md2man --disable-roll-simd --disable-md5-asm \
	--disable-acl-support --disable-xattr-support --disable-iconv \
	--disable-iconv-open --with-included-popt --with-included-zlib \
	--disable-debug > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
make -j"$(nproc)" rsync > make.log 2>&1 ||
	{ grep -E ':[0-9]+: |undefined|Error' make.log | grep -v warning | tail -20; exit 1; }
cp rsync "$work/rsync"
m68k-cbm-sysv4-strip "$work/rsync"
ls -l "$work/rsync"
