#!/bin/sh
# build.sh - GNU mtools as a static AMIX program for Atari System V (it
# runs there with the amx module, ../../driver-amix), with a drive type
# of its own: "ub:N", the kernel USB driver's storage unit N read by
# block number through /dev/ub0 (ub_io.c), so a 16 GB stick's FAT32 is
# all reachable, which a 32-bit lseek on /dev/rdsk/usb0 would cap at 2 GB.
#
#   AMIX_SYSROOT=../work/sysroot sh build.sh
#
# -> work/mtools (stripped) and work/mtools-$V-asv.tar: /usr/local/bin/mtools
# with its command names hard-linked to it, /usr/local/etc/mtools.conf
# (drive u: = ub:0, partition 1).  Unpack in / on the TT.
#
# Needs what ../vim/build.sh needs: gcc-cross-amix at ~/opt/asv-cross
# (ASV_CROSS=) for the SVR4 assembler, linker, ar and strip; the mint GCC
# (m68k-atari-mint-gcc or MINT_GCC=); Manx (MANX=, default
# ~/dev/unix-browser) for toolchain/sysv4-cc, sysv4-ld and os/sysv4/snprintf.c;
# the AMIX sysroot from ../mksysroot.sh; curl, make.
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${MANX:=$HOME/dev/unix-browser}"
: "${AMIX_SYSROOT:?set AMIX_SYSROOT (see ../mksysroot.sh)}"
AMIX_SYSROOT=$(cd "$AMIX_SYSROOT" && pwd)
MANX=$(cd "$MANX" && pwd)
V=4.0.49
SHA256=10cd1111da87bf2400a380c1639a6cba8bfb937a24f9c51f5f88d393ae5f6f76
work=${WORK:-$here/work}
AMIX_RT=$work/rt.a
export ASV_CROSS AMIX_SYSROOT MANX AMIX_RT
mkdir -p "$work"
cd "$work"

[ -f mtools-$V.tar.gz ] ||
	curl -sfL -o mtools-$V.tar.gz https://ftp.gnu.org/gnu/mtools/mtools-$V.tar.gz
echo "$SHA256  mtools-$V.tar.gz" | sha256sum -c --quiet
rm -rf mtools-$V
tar xzf mtools-$V.tar.gz

CFLAGS="-m68030 -m68881 ${OPT:--O2} -fomit-frame-pointer -std=gnu99"
"$MANX/toolchain/sysv4-cc" $CFLAGS -include "$here/amixfix.h" -c "$here/rt.c" -o rt.o
"$MANX/toolchain/sysv4-cc" $CFLAGS -c "$MANX/os/sysv4/snprintf.c" -o snprintf.o
rm -f rt.a
"$ASV_CROSS/bin/m68k-cbm-sysv4-ar" rc rt.a rt.o snprintf.o

cd mtools-$V
# the ub: drive: its stream, the driver's ioctl interface, and the hook
# in OpenImage ahead of the plain file open
cp "$here/ub_io.c" "$here/ub_io.h" .
cp "$here/../../driver-ub/ubioc.h" .
python3 - <<'PY'
import re
p='open_image.c'; s=open(p).read()
old='''		if (!Stream) {
			Stream = SimpleFileOpenWithLm(out_dev, dev, name,'''
new='''		if (!Stream && name && !strncmp(name, "ub:", 3)) {
			/* Atari System V: the USB driver's storage unit */
			Stream = UbOpen(out_dev, name + 3, mode, errmsg, maxSize);
			if (!Stream)
				return NULL;
		}
		if (!Stream) {
			Stream = SimpleFileOpenWithLm(out_dev, dev, name,'''
assert s.count(old) == 1, "OpenImage changed"
s = s.replace(old, new)
i = s.rindex('#include ')
j = s.index('\n', i) + 1
s = s[:j] + '#include "ub_io.h"\n' + s[j:]
open(p,'w').write(s)
p='Makefile.in'; s=open(p).read()
assert ' plain_io.o' in s
s = s.replace(' plain_io.o', ' plain_io.o ub_io.o', 1)
open(p,'w').write(s)
PY

# configure cross: amix-cc compiles with sysv4-cc and links with sysv4-ld,
# so the link tests tell the truth about AMIX's libc (no strcasecmp: mtools'
# own; snprintf: rt.a's).  No wide characters, no iconv: the TT's console
# has neither.
CC="$here/../vim/amix-cc" CFLAGS="$CFLAGS" \
ac_cv_header_wchar_h=no ac_cv_header_wctype_h=no ac_cv_header_iconv_h=no \
ac_cv_header_langinfo_h=no \
	./configure --host=m68k-unknown-sysv4 --build="$(gcc -dumpmachine)" \
	--prefix=/usr/local --sysconfdir=/usr/local/etc \
	--disable-floppyd --disable-xdf --disable-vold --disable-new-vold \
	--without-x > "$work/configure.log" 2>&1 ||
	{ tail -20 "$work/configure.log"; exit 1; }
# amixfix.h goes ahead of each source, but not of configure's tests: they
# declare the functions they probe as "char f();", which its prototypes
# would contradict (mkdir came out missing that way)
sed -i "s|^CFLAGS\([[:space:]]*\)= |CFLAGS\1= -include $here/amixfix.h |" Makefile
grep -q amixfix.h Makefile || { echo "amixfix.h not in the Makefile"; exit 1; }
# configure adds SVR4's BSD library for a static link that has none
sed -i 's|-L/usr/ucblib -lucb||' Makefile
make -k -j"$(nproc)" mtools > "$work/make.log" 2>&1 ||
	{ grep -E ':[0-9]+: |Error|undefined' "$work/make.log" | grep -v warning | tail -30; exit 1; }
cp mtools "$work/mtools"
"$ASV_CROSS/bin/m68k-cbm-sysv4-strip" "$work/mtools"
links=$(make -pn 2>/dev/null | sed -n 's/^LINKS = //p' | head -1 | sed 's/$(EXEEXT)//g')

# the tar: ustar, short names, owners named (ASV's tar is from 1991)
root=$work/root
rm -rf "$root"
mkdir -p "$root/usr/local/bin" "$root/usr/local/etc"
cp "$work/mtools" "$root/usr/local/bin/mtools"
for l in $links; do ln "$root/usr/local/bin/mtools" "$root/usr/local/bin/$l"; done
cp "$here/mtools.conf" "$root/usr/local/etc/mtools.conf"
(cd "$root" && tar --format=ustar --owner=root:0 --group=root:0 \
	-cf "$work/mtools-$V-asv.tar" usr)
ls -l "$work/mtools" "$work/mtools-$V-asv.tar"
echo "links: $links"
