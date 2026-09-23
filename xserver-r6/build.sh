#!/bin/sh
# build.sh - cross-build the X11R6.3 Xatw server for Atari System V.
#
#   sh build.sh [WORKDIR]        (default: ./work)
#
# Needs gcc-cross-amix in ~/opt/asv-cross (or ASV_CROSS=<prefix>), with the
# ASV sysroot, plus curl, python3 and a host gcc and cpp. Fetches the
# X11R6.3 AMIX overlay at a pinned commit, whose install.sh downloads and
# checks the X.Org sources, then adds the ASV platform and the ATW800/2 ddx.
set -e
HERE=`cd "\`dirname "$0"\`" && pwd`
WORK=${1:-$HERE/work}
CROSS=${ASV_CROSS:-$HOME/opt/asv-cross}
AMIX_REPO=https://github.com/isoriano1968/x11r6.3-amix.git
AMIX_REV=cb61a2115659cb3ae27c649439edb2ca133131b6
SYSROOT=$CROSS/m68k-cbm-sysv4/sysroot

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d x11r6.3-amix ]; then
	git clone -q "$AMIX_REPO" x11r6.3-amix
	(cd x11r6.3-amix && git checkout -q "$AMIX_REV")
fi
if [ ! -d xc ]; then
	sh x11r6.3-amix/install.sh "$WORK"
	chmod -R u+w xc
	(cd xc && patch -p1 < "$HERE/patches/x11r6.3-asv.patch")
	# the imake config uses the cross compiler's absolute path
	sed "s|@ASV_CROSS@|$CROSS|g" \
		"$HERE/config/asv.cf" > xc/config/cf/asv.cf
	mkdir -p xc/programs/Xserver/hw/atw
	cp "$HERE"/hw/atw/* xc/programs/Xserver/hw/atw/
	# keymap and libc shims are shared with the X11R4 server
	cp "$HERE/../xserver/atwKMap.c" "$HERE/../xserver/atwCompat.c" \
		xc/programs/Xserver/hw/atw/
	# libm's sqrt needs these from libc.a; the wrapper orders archives
	# before -l libraries, so they are linked as plain objects
	mkdir -p xc/config/asv
	(cd xc/config/asv && "$CROSS/bin/m68k-cbm-sysv4-ar" x "$SYSROOT/usr/lib/libc.a" \
		fpsetrnd.o fpsetmask.o fpsetsticky.o)
fi
cd xc
if [ ! -x config/imake/imake ]; then
	gcc -O -w -Ulinux -U__linux__ -DASV_CROSS -Iinclude \
		-o config/imake/imake config/imake/imake.c
fi
./config/imake/imake -DASV -I./config/cf -DTOPDIR=. -DCURDIR=. -s xmakefile
make -f xmakefile Makefiles IMAKE_DEFINES=-DASV > Makefiles.log 2>&1
# includes: the Xlib/Xt helper tools fail (built for the target, run on
# the host); the server only needs the header links
make -f xmakefile includes > includes.log 2>&1 || true
for d in lib/font lib/Xau lib/Xdmcp programs/Xserver; do
	(cd $d && make) > "$d/build.log" 2>&1 || { echo "build failed in $d, see $d/build.log"; exit 1; }
done
# the wrapper hands ld absolute library paths and ASV's .so files have no
# SONAME: rewrite the NEEDED entries to bare names
python3 "$HERE/../xserver/fixneeded.py" programs/Xserver/Xatw
ls -l programs/Xserver/Xatw
