#!/bin/sh
# build.sh - Vim 9.2 as a static AMIX program, cross-built on the PC.
# It runs on AMIX, and on Atari System V with the amx module (../../driver-amix).
#
#   AMIX_SYSROOT=../work/sysroot sh build.sh
#
# -> work/vim (stripped), and work/vim-$V-amix.tar: vim with its runtime
# files and vimrc, to unpack in / (it all goes in /usr/local).
#
# Needs gcc-cross-amix at ~/opt/asv-cross (or ASV_CROSS=) for its SVR4
# assembler, linker and libgcc; the mint GCC (m68k-atari-mint-gcc on PATH,
# or MINT_GCC=), which compiles, since vim 9 is C99 and gcc-cross-amix's
# gcc 2.7.2 is not; a checkout of Manx (MANX=, default ~/dev/unix-browser)
# for toolchain/sysv4-cc and sysv4-ld; the AMIX sysroot from ../mksysroot.sh;
# on the PC curl, make, gcc. OPT= sets the optimisation (default -O2).
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${MANX:=$HOME/dev/unix-browser}"
: "${AMIX_SYSROOT:?set AMIX_SYSROOT (see ../mksysroot.sh)}"
AMIX_SYSROOT=$(cd "$AMIX_SYSROOT" && pwd)
MANX=$(cd "$MANX" && pwd)
V=9.2.1189
SHA256=f83bfbb04a0f960fc7fcb4b5c886073e696e5035517f0a7a4cfc4499e25897c6
work=${WORK:-$here/work}
AMIX_RT=$work/rt.a
export ASV_CROSS AMIX_SYSROOT MANX AMIX_RT
mkdir -p "$work"
cd "$work"

[ -f vim-$V.tar.gz ] ||
	curl -sfL -o vim-$V.tar.gz https://github.com/vim/vim/archive/refs/tags/v$V.tar.gz
echo "$SHA256  vim-$V.tar.gz" | sha256sum -c --quiet
rm -rf vim-$V
tar xzf vim-$V.tar.gz

CFLAGS="-m68030 -m68881 ${OPT:--O2} -fomit-frame-pointer -std=gnu99"
"$MANX/toolchain/sysv4-cc" $CFLAGS -c "$here/rt.c" -o rt.o
rm -f rt.a
"$ASV_CROSS/bin/m68k-cbm-sysv4-ar" rc rt.a rt.o

cd vim-$V/src
# What configure would learn by running test programs on the machine itself.
# Nothing of the PC's may get in: no pkg-config (it finds the PC's Wayland
# and GTK), no /usr/local.
CC="$here/amix-cc" CFLAGS="$CFLAGS" PKG_CONFIG=false \
vim_cv_toupper_broken=no vim_cv_terminfo=yes vim_cv_tgetent=zero \
vim_cv_getcwd_broken=no vim_cv_stat_ignores_slash=no \
vim_cv_memmove_handles_overlap=yes vim_cv_timer_create=no \
vim_cv_uname_output=UNIX_System_V vim_cv_uname_r_output=4.0 vim_cv_uname_m_output=m68k \
	./configure --host=m68k-unknown-sysv4 --build="$(gcc -dumpmachine)" \
	--prefix=/usr/local --with-features=normal --with-tlib=termlib \
	--with-compiledby=atari-sysv-sp1 \
	--enable-gui=no --without-x --without-wayland --without-local-dir \
	--disable-netbeans --disable-channel --disable-gpm --disable-sysmouse \
	--disable-acl --disable-selinux --disable-smack --disable-xattr \
	--disable-xsmp --disable-canberra --disable-libsodium --disable-nls \
	--disable-largefile > "$work/configure.log" 2>&1 ||
	{ tail -20 "$work/configure.log"; exit 1; }
# configure looks for /proc/self/exe on the PC; AMIX's /proc has none
sed -i '/PROC_EXE_LINK/d' auto/config.h
# amixfix.h goes ahead of each source of vim's, but not of configure's tests:
# they declare a function as "char f();" to see if it links
sed -i "s|^CFLAGS\([[:space:]]*\)= |CFLAGS\1= -include $here/amixfix.h |" auto/config.mk
make -j"$(nproc)" > "$work/make.log" 2>&1 ||
	{ grep -E ':[0-9]+: |Error|undefined' "$work/make.log" | grep -v warning | tail -30; exit 1; }
cp vim "$work/vim"
"$ASV_CROSS/bin/m68k-cbm-sysv4-strip" "$work/vim"

# The runtime files, but for the GUI's pictures, the message translations
# (no NLS here), the spelling dictionaries, the tutor and the tests. ASV's
# tar is from 1991: ustar, every name under 100 bytes, and owners named
# (AMIX's tar crashes on an empty owner name).
root=$work/root
rm -rf "$root"
mkdir -p "$root/usr/local/bin" "$root/usr/local/share/vim/vim92"
cp "$work/vim" "$root/usr/local/bin/vim"
cp "$here/vimrc" "$root/usr/local/share/vim/vimrc"
cd ../runtime
tar cf - --exclude=testdir ./*.vim autoload colors compiler doc ftplugin \
	import indent keymap macros pack plugin print syntax |
	(cd "$root/usr/local/share/vim/vim92" && tar xf -)
chmod -R go-w "$root"
(cd "$root" && tar cf "$work/vim-$V-amix.tar" --format=ustar --owner=root:0 \
	--group=root:0 usr)
ls -l "$work/vim" "$work/vim-$V-amix.tar"
