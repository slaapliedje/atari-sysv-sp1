#!/bin/sh
# build.sh - GNU nano 9.2 as a static AMIX program, cross-built on the PC.
# It runs on AMIX, and on Atari System V with the amx module (../../driver-amix).
#
#   AMIX_SYSROOT=../work/sysroot sh build.sh
#
# -> work/nano (stripped), and work/nano-$V-amix.tar: /usr/local/bin/nano,
# its syntax files and a system nanorc, to unpack in /.
#
# ncurses 6.5 is built first, static, narrow characters, no C++, reading
# the system's own terminfo database (/usr/share/lib/terminfo on both
# systems: SVR4's compiled format is what ncurses reads).  Needs what
# ../vim/build.sh needs: gcc-cross-amix at ~/opt/asv-cross (ASV_CROSS=)
# for the SVR4 assembler, linker, ar and strip; the mint GCC
# (m68k-atari-mint-gcc or MINT_GCC=); Manx (MANX=, default
# ~/dev/unix-browser) for toolchain/sysv4-cc and sysv4-ld; the AMIX sysroot
# from ../mksysroot.sh; curl, make, gcc.  OPT= sets the optimisation.
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${MANX:=$HOME/dev/unix-browser}"
: "${AMIX_SYSROOT:?set AMIX_SYSROOT (see ../mksysroot.sh)}"
AMIX_SYSROOT=$(cd "$AMIX_SYSROOT" && pwd)
MANX=$(cd "$MANX" && pwd)
V=9.2
SHA256=05ecb99247b782e8a5b3a25ed4101dd034b0236902f7449bc9795b717642f7e9
NCV=6.5
NCSHA256=136d91bc269a9a5785e5f9e980bc76ab57428f604ce3e5a5a90cebc767971cc6
work=${WORK:-$here/work}
dl=$work/dl
AMIX_RT=$work/rt.a
nc=$work/ncurses
TERMINFO=/usr/share/lib/terminfo
export ASV_CROSS AMIX_SYSROOT MANX AMIX_RT
mkdir -p "$work" "$dl"
cd "$dl"
[ -f nano-$V.tar.xz ] ||
	curl -sfL -o nano-$V.tar.xz https://www.nano-editor.org/dist/v${V%%.*}/nano-$V.tar.xz
echo "$SHA256  nano-$V.tar.xz" | sha256sum -c --quiet
[ -f ncurses-$NCV.tar.gz ] ||
	curl -sfL -o ncurses-$NCV.tar.gz https://ftp.gnu.org/gnu/ncurses/ncurses-$NCV.tar.gz
echo "$NCSHA256  ncurses-$NCV.tar.gz" | sha256sum -c --quiet

cd "$work"
# (GCC 15 makes a pointer type mismatch and an implicit int errors: ncurses's
# twalk call, gnulib's stdint.h 64-bit check)
CFLAGS="-m68030 -m68881 ${OPT:--O2} -fomit-frame-pointer -std=gnu99 -Wno-incompatible-pointer-types -Wno-implicit-int"
"$MANX/toolchain/sysv4-cc" $CFLAGS -c "$here/rt.c" -o rt.o
rm -f rt.a
"$ASV_CROSS/bin/m68k-cbm-sysv4-ar" rc rt.a rt.o

# --- ncurses: the library and headers only, into $nc
if [ ! -f "$nc/lib/libncurses.a" ]; then
	rm -rf ncurses-$NCV "$nc"
	tar xzf "$dl/ncurses-$NCV.tar.gz"
	cd ncurses-$NCV
	CC="$here/amix-cc" CFLAGS="$CFLAGS" BUILD_CC=gcc \
		./configure --host=m68k-unknown-sysv4 --build="$(gcc -dumpmachine)" \
		--prefix="$nc" --without-cxx --without-cxx-binding --without-ada \
		--without-manpages --without-progs --without-tests --without-debug \
		--without-shared --disable-db-install --without-tack \
		--with-terminfo-dirs=$TERMINFO --with-default-terminfo-dir=$TERMINFO \
		--disable-widec --without-gpm --without-dlsym --disable-rpath \
		--enable-termcap=no --without-termlib \
		> "$work/ncurses-configure.log" 2>&1 ||
		{ tail -20 "$work/ncurses-configure.log"; exit 1; }
	# make_hash and make_keys are built for the PC with this same header;
	# its ssize_t stands in for AMIX's only
	sed -i 's/^#define ssize_t int$/#ifndef USE_BUILD_CC\n#define ssize_t int\n#endif/' include/ncurses_cfg.h
	# amixfix.h ahead of each source, but not of configure's tests
	for m in ncurses/Makefile form/Makefile menu/Makefile panel/Makefile; do
		sed -i "s|^CC\([[:space:]]*\)= .*|CC\1= $here/amix-cc -include $here/amixfix.h|" $m
	done
	make -j"$(nproc)" libs > "$work/ncurses-make.log" 2>&1 ||
		{ grep -E ':[0-9]+: |Error|undefined' "$work/ncurses-make.log" | grep -v warning | tail -30; exit 1; }
	make install.libs install.includes >> "$work/ncurses-make.log" 2>&1
	cd "$work"
fi
echo "ncurses: $nc/lib/libncurses.a"

# --- nano
rm -rf nano-$V
tar xJf "$dl/nano-$V.tar.xz"
cd nano-$V
# What configure would learn by running test programs on the machine itself;
# nothing of the PC's may get in (no pkg-config).
CC="$here/amix-cc" CFLAGS="$CFLAGS" CPPFLAGS="-I$nc/include -I$nc/include/ncurses" \
LDFLAGS="-L$nc/lib" LIBS=-lgen PKG_CONFIG=false \
	./configure --host=m68k-unknown-sysv4 --build="$(gcc -dumpmachine)" \
	--prefix=/usr/local --sysconfdir=/usr/local/etc \
	--disable-nls --disable-utf8 --disable-libmagic --without-slang \
	> "$work/configure.log" 2>&1 ||
	{ tail -20 "$work/configure.log"; exit 1; }
# amixfix.h at the end of config.h: gnulib's headers want config.h first
printf '\n#include "%s/amixfix.h"\n' "$here" >> config.h
# AMIX's struct stat keeps its times as timestruc_t, the same two fields
sed -i 's/^\(\t*filetimes\[\([01]\)\] = fileinfo\.st_\([am]\)tim\);$/\tfiletimes[\2].tv_sec = fileinfo.st_\3tim.tv_sec; filetimes[\2].tv_nsec = fileinfo.st_\3tim.tv_nsec;/' src/files.c
grep -c "st_atim.tv_sec" src/files.c > /dev/null
make -j"$(nproc)" > "$work/make.log" 2>&1 ||
	{ grep -E ':[0-9]+: |Error|undefined' "$work/make.log" | grep -v warning | tail -30; exit 1; }
cp src/nano "$work/nano"
"$ASV_CROSS/bin/m68k-cbm-sysv4-strip" "$work/nano"

# The tree to unpack in /: the program, the syntax files, a system nanorc
# that includes them.  ASV's tar is from 1991: ustar, every name under 100
# bytes, and owners named (AMIX's tar crashes on an empty owner name).
root=$work/root
rm -rf "$root"
mkdir -p "$root/usr/local/bin" "$root/usr/local/share/nano" "$root/usr/local/etc"
cp "$work/nano" "$root/usr/local/bin/nano"
cp syntax/*.nanorc "$root/usr/local/share/nano/"
cp "$here/nanorc" "$root/usr/local/etc/nanorc"
cd "$root"
find . -type f | sed 's|^\./||' | awk 'length > 99 { print "name too long: " $0; bad = 1 } END { exit bad }'
tar --format=ustar --owner=root --group=root \
	-cf "$work/nano-$V-amix.tar" usr
ls -l "$work/nano" "$work/nano-$V-amix.tar"
