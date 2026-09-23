#!/bin/sh
# build.sh - cross-build X11R6.3 for Atari System V: the Xatw server, the
# client libraries (X11, Xt, Xaw, Xmu, Xext, ICE, SM, ...) and clients.
#
#   sh build.sh [WORKDIR]        (default: ./work)
#
# Needs gcc-cross-amix in ~/opt/asv-cross (or ASV_CROSS=<prefix>) with the
# ASV sysroot, plus git, curl, python3, a host gcc and cpp, and bdftopcf/mkfontdir. Fetches the
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
CLIENTS="xdpyinfo xclock xlogo xterm twm xsetroot xset xlsfonts xfd xrdb xauth xdm"

# the gcc-cross-amix wrapper must have the PIC fixes for GNU as (LC%N
# labels, long PLT calls) and AMIX_RETURN_D0_TO_A0
grep -q 'AMIX_RETURN_D0_TO_A0' "$CROSS/bin/m68k-cbm-sysv4-gcc" || {
	echo "the cross gcc wrapper lacks the PIC and return-register fixes; update gcc-cross-amix" >&2
	exit 1
}

mkdir -p "$WORK"
WORK=`cd "$WORK" && pwd`
SYSROOT=$WORK/sysroot
cd "$WORK"

# 1. a fixincluded shadow of the sysroot (see mksysroot.sh)
[ -d "$SYSROOT" ] || sh "$HERE/mksysroot.sh" "$REALROOT" "$SYSROOT"
CC="env AMIX_SYSROOT=$SYSROOT AMIX_RETURN_D0_TO_A0=1 $CROSS/bin/m68k-cbm-sysv4-gcc"

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

# 3. objects every program links (asv.cf: AsvFpsetObjs):
#    - libcextra.o: what ASV's libc.so.1 leaves to the static libc.a (setitimer,
#      sys_errlist, cfree, crypt, the shadow and utmp calls, libm's fpset
#      helpers), with everything those need,
#      combined with ld -r
#    - asvcompat.o: BSD names over libc's _abi_* exports, syscall (ERESTART ->
#      EINTR), vfork, killpg
#    - asviob.o: stdio's stdin/stdout/stderr (see mksysroot.sh)
mkdir -p config/asv
if [ ! -f config/asv/asviob.o ]; then
	M=`PATH=$CROSS/bin:$PATH python3 "$HERE/libcextra.py" "$REALROOT/usr/lib/libc.a" \
		"$REALROOT/usr/lib/libc.so.1" _fpsetround _fpsetmask _fpsetsticky _fpgetround \
		setitimer getitimer sys_errlist sys_nerr cfree \
		crypt getspnam setspent endspent setutent getutid pututline endutent utmpname | grep -v -x -E 'syscall.o|vfork.o'`
	rm -rf config/asv/libc; mkdir -p config/asv/libc
	(cd config/asv/libc && "$CROSS/bin/m68k-cbm-sysv4-ar" x "$REALROOT/usr/lib/libc.a" $M &&
		"$CROSS/bin/m68k-cbm-sysv4-ld" -r -o ../libcextra.o *.o)
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
# resize: xterm's second program (on SVR4 it prints nothing; it sets the
# tty's window size with TIOCSWINSZ from xterm's cursor-position report)
(cd programs/xterm && make resize) > programs/xterm/resize.log 2>&1 || { echo "build failed: resize"; exit 1; }

# 6. the wrapper hands ld absolute library paths and ASV's .so files have no
#    SONAME: rewrite the NEEDED entries to bare names (after the last link)
for p in Xserver/Xatw xterm/resize `for c in $CLIENTS; do echo $c/$c; done`; do
	python3 "$HERE/../xserver/fixneeded.py" programs/$p > /dev/null
done

# xfuji: the Fuji on xdm's login screen
$CC -O -Iexports/include -c "$HERE/xdm/xfuji.c" -o programs/xfuji.o
$CC -o programs/xfuji programs/xfuji.o -Lexports/lib -lXext -lX11 \
	-lsocket -lsockhost -lnsl config/asv/libcextra.o config/asv/asvcompat.o \
	config/asv/asviob.o -rpath=/usr/x11r6/lib -rpath-link=exports/lib
python3 "$HERE/../xserver/fixneeded.py" programs/xfuji > /dev/null

# 7. an install tree: copy dist/usr/x11r6 to /usr/x11r6 on the machine
D=$WORK/dist/usr/x11r6
rm -rf "$WORK/dist"; mkdir -p $D/bin $D/lib
cp programs/Xserver/Xatw $D/bin/
for c in $CLIENTS; do cp programs/$c/$c $D/bin/; done
cp programs/xterm/resize $D/bin/
for l in lib/*/lib*.so.[0-9]*; do cp $l $D/lib/; done

# xdm: the ASV configuration, xfuji, and the Fuji itself - taken from your
# own TOS ROM (TOS=path; a Hatari install's ROM if you have one), since the
# art is Atari's and does not ship with sp1. Without one there is no logo.
X=$D/lib/X11/xdm
mkdir -p $X
for f in xdm-config Xservers Xaccess Xresources Xsetup_0 Xstartup Xsession xrdb-nocpp; do
	cp "$HERE/xdm/$f" $X/
done
cp programs/xfuji $X/
if [ -z "$TOS" ]; then
	for t in /usr/share/hatari/tos306*.img /usr/share/hatari/tos206*.img \
		 /usr/share/hatari/tos404*.img; do
		[ -f "$t" ] && { TOS=$t; break; }
	done
fi
if [ -n "$TOS" ]; then
	python3 "$HERE/xdm/fuji-from-tos.py" "$TOS" $X/fuji.xbm
else
	echo "no TOS ROM given (TOS=...): the login screen will have no Fuji"
fi

# 8. fonts: the R6.3 BDF sources as PCF, compiled with the host's bdftopcf
#    and mkfontdir (PCF records its own byte and bit order). Uncompressed:
#    the server's gzip support would need zlib on the target.
for d in misc 75dpi 100dpi; do
	F=$D/lib/X11/fonts/$d
	mkdir -p $F
	for f in fonts/bdf/$d/*.bdf; do
		bdftopcf -t -o $F/`basename $f .bdf`.pcf $f
	done
	cp fonts/bdf/$d/fonts.alias $F/ 2>/dev/null || true
	mkfontdir $F
done
# ASV's tar cannot read GNU-format archives: pack V7, numeric owners. Two
# archives, each under the 16 MB a process may write by default (ULIMIT):
# the fonts, and everything else.
tar --format=v7 --owner=0 --group=0 -C $WORK/dist -cf $WORK/x11r6-asv.tar \
	--exclude=usr/x11r6/lib/X11/fonts usr
tar --format=v7 --owner=0 --group=0 -C $WORK/dist -cf $WORK/x11r6-fonts-asv.tar \
	usr/x11r6/lib/X11/fonts
ls -l $D/bin $D/lib $WORK/x11r6-asv.tar $WORK/x11r6-fonts-asv.tar
