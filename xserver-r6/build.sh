#!/bin/sh
# build.sh - cross-build X11R6.3 for Atari System V: the Xatw server, the
# client libraries (X11, Xt, Xaw, Xmu, Xext, ICE, SM, ...) and clients.
#
#   sh build.sh [WORKDIR]        (default: ./work)
#
# Needs gcc-cross-amix in ~/opt/asv-cross (or ASV_CROSS=<prefix>) with the
# ASV sysroot, plus git, curl, python3 and a host gcc and cpp. Fetches the
# X11R6.3 AMIX overlay at a pinned commit, whose install.sh downloads and
# checks the X.Org sources, then adds the ASV platform and the ATW800/2 ddx.
# Everything is linked statically against the X libraries; ASV's own
# libc, libsocket and libnsl are shared.
set -e
HERE=`cd "\`dirname "$0"\`" && pwd`
WORK=${1:-$HERE/work}
CROSS=${ASV_CROSS:-$HOME/opt/asv-cross}
AMIX_REPO=https://github.com/isoriano1968/x11r6.3-amix.git
AMIX_REV=cb61a2115659cb3ae27c649439edb2ca133131b6
REALROOT=$CROSS/m68k-cbm-sysv4/sysroot
CLIENTS="xdpyinfo xclock xlogo xterm twm xsetroot"

mkdir -p "$WORK"
WORK=`cd "$WORK" && pwd`
SYSROOT=$WORK/sysroot
cd "$WORK"

# 1. a fixincluded shadow of the sysroot (see mksysroot.sh)
[ -d "$SYSROOT" ] || sh "$HERE/mksysroot.sh" "$REALROOT" "$SYSROOT"
CC="env AMIX_SYSROOT=$SYSROOT $CROSS/bin/m68k-cbm-sysv4-gcc"

# 2. the sources: pinned AMIX overlay + X.Org archives + our changes
if [ ! -d x11r6.3-amix ]; then
	git clone -q "$AMIX_REPO" x11r6.3-amix
	(cd x11r6.3-amix && git checkout -q "$AMIX_REV")
fi
if [ ! -d xc ]; then
	sh x11r6.3-amix/install.sh "$WORK"
	chmod -R u+w xc
	(cd xc && patch -p1 < "$HERE/patches/x11r6.3-asv.patch")
	sed "s|@ASV_CROSS@|$CROSS|g; s|@ASV_SYSROOT@|$SYSROOT|g" \
		"$HERE/config/asv.cf" > xc/config/cf/asv.cf
	mkdir -p xc/programs/Xserver/hw/atw
	cp "$HERE"/hw/atw/* xc/programs/Xserver/hw/atw/
	# the keymap is shared with the X11R4 server
	cp "$HERE/../xserver/atwKMap.c" xc/programs/Xserver/hw/atw/
fi
cd xc

# 3. objects every program links (asv.cf: AsvFpsetObjs): libc.a's fpset
#    helpers for libm, the BSD-name shims, and the stdio streams
mkdir -p config/asv
if [ ! -f config/asv/asviob.o ]; then
	(cd config/asv && "$CROSS/bin/m68k-cbm-sysv4-ar" x "$REALROOT/usr/lib/libc.a" \
		fpsetrnd.o fpsetmask.o fpsetsticky.o)
	cp "$HERE/../xserver/atwCompat.c" config/asv/asvcompat.c
	cp "$HERE/config/asviob.c" config/asv/
	(cd config/asv && $CC -O -c asvcompat.c && $CC -O -c asviob.c)
fi

# 4. host tools: imake, and the two generators the build runs on the PC
if [ ! -x config/imake/imake ]; then
	gcc -O -w -Ulinux -U__linux__ -DASV_CROSS -Iinclude \
		-o config/imake/imake config/imake/imake.c
fi
./config/imake/imake -DASV -I./config/cf -DTOPDIR=. -DCURDIR=. -s xmakefile
make -f xmakefile Makefiles IMAKE_DEFINES=-DASV > Makefiles.log 2>&1
# (object and program both, or make relinks makestrs with the cross compiler)
gcc -O -w -Iinclude -c -o config/util/makestrs.o config/util/makestrs.c
gcc -o config/util/makestrs config/util/makestrs.o
touch config/util/makestrs
make -f xmakefile includes > includes.log 2>&1 || true
gcc -O -w -Iexports/include -o config/util/makekeys.host lib/X11/util/makekeys.c
./config/util/makekeys.host < exports/include/X11/keysymdef.h > lib/X11/ks_tables.h
make -f xmakefile includes > includes.log 2>&1

# 5. libraries, server, clients
for d in lib programs/Xserver; do
	(cd $d && make) > "$d/build.log" 2>&1 || { echo "build failed in $d, see xc/$d/build.log"; exit 1; }
done
for c in $CLIENTS; do
	(cd programs/$c && make $c) > "programs/$c/build.log" 2>&1 || { echo "build failed: $c"; exit 1; }
done

# 6. the wrapper hands ld absolute library paths and ASV's .so files have no
#    SONAME: rewrite the NEEDED entries to bare names (after the last link)
for p in Xserver/Xatw `for c in $CLIENTS; do echo $c/$c; done`; do
	python3 "$HERE/../xserver/fixneeded.py" programs/$p > /dev/null
	ls -l programs/$p
done
