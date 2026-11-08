#!/bin/sh
# build.sh - JWM (Joe's Window Manager, MIT licence) for Atari System V on
# X11R6.3: a window manager with a taskbar, a menu and a pager, offered on
# the login screen beside OpenLook and twm.
#
#   sh build.sh X11WORK [OUT]
#
# X11WORK is the X11R6.3 tree ../build.sh made (its xc/exports and the
# fixincluded ASV sysroot); OUT (default ./work) gets the jwm binary.
# JWM is C99, which gcc-cross-amix's gcc 2.7.2 can't compile, so the mint
# GCC compiles it through the Manx repository's toolchain/sysv4-cc (MANX,
# default ~/dev/unix-browser), against ASV's headers and R6.3's, and
# gcc-cross-amix links it the way ../build.sh links the other clients:
# R6.3's shared Xlib and Xext from /usr/x11r6/lib, ASV's libc. snprintf comes from Manx's
# os/sysv4; regex.c puts POSIX regcomp on libgen's regcmp; asvjwm.h
# declares what ASV's headers keep from GCC (see each file).
set -e
HERE=`cd "\`dirname "$0"\`" && pwd`
X=${1:?usage: build.sh X11WORK [OUT]}
OUT=${2:-$HERE/work}
CROSS=${ASV_CROSS:-$HOME/opt/asv-cross}
MANX=${MANX:-$HOME/dev/unix-browser}
VER=2.4.6
URL=https://github.com/joewing/jwm/releases/download/v$VER/jwm-$VER.tar.xz
SHA256=b5871ec28317594b3fa22b83ed5524cc911d498c455eaab3ae68def195dd802d
mkdir -p "$OUT"; OUT=`cd "$OUT" && pwd`; X=`cd "$X" && pwd`
cd "$OUT"
[ -f jwm-$VER.tar.xz ] || curl -sSL -o jwm-$VER.tar.xz $URL
echo "$SHA256  jwm-$VER.tar.xz" | sha256sum -c - > /dev/null || { echo "jwm-$VER.tar.xz: wrong SHA-256" >&2; exit 1; }
rm -rf jwm-$VER obj; tar -xJf jwm-$VER.tar.xz; mkdir obj
cp "$HERE/config.h" jwm-$VER/config.h
export AMIX_SYSROOT=$X/sysroot
PATH=$HOME/opt/cross-mint/bin:$PATH
# -m68030: the mint GCC's default is the 68000, without 32-bit multiply and
# divide. -m68881: JWM's few float sums (gradients, opacity) go to the TT's
# 68882, since gcc 2.7's libgcc has no soft-float routines; JWM calls
# nothing in libm, whose doubles come back soft-float style. -fcommon: an
# ASV header defines lconv in every file that includes it
CC="$MANX/toolchain/sysv4-cc -m68030 -m68881 -fcommon -O2 -fomit-frame-pointer -std=gnu99 -Wno-implicit-int -include $HERE/asvjwm.h -I$HERE -isystem $X/xc/exports/include"
for f in jwm-$VER/src/*.c "$HERE/regex.c" "$HERE/asvrt.c" "$MANX/os/sysv4/snprintf.c"; do
	$CC -c "$f" -o obj/`basename "$f" .c`.o
done
LINK="env AMIX_SYSROOT=$X/sysroot AMIX_RETURN_D0_TO_A0=1 $CROSS/bin/m68k-cbm-sysv4-gcc"
$LINK -o jwm obj/*.o -L$X/xc/exports/lib -lXext -lX11 -L$CROSS/m68k-cbm-sysv4/sysroot/usr/ccs/lib -lgen \
	-lsocket -lsockhost -lnsl $X/xc/config/asv/libcextra.o $X/xc/config/asv/asvcompat.o \
	$X/xc/config/asv/asviob.o -rpath=/usr/x11r6/lib -rpath-link=$X/xc/exports/lib
python3 "$HERE/../../xserver/fixneeded.py" jwm > /dev/null
ls -l "$OUT/jwm"
