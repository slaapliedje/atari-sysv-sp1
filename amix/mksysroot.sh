#!/bin/sh
# mksysroot.sh [OUT] - from AMIX 2.1's own packages, build
#   OUT/sysroot  an AMIX sysroot for gcc-cross-amix: usr/include (from Cdev),
#                usr/lib (core) and usr/ccs/lib (Cdev: crt*.o, values-X*.o,
#                libc.a...), with the headers' `__STDC__ == 0' tests fixed for
#                gcc as mksysroot.sh does for ASV
#   OUT/amx      AMIX's shared C library, patched by amixify.py to live in
#                /usr/amx on Atari System V: libc.so.1 (also the dynamic
#                linker), ld.so.1, libsocket.so, libnsl.so ...
#   OUT/amx.tar  the same, in the V7 tar format ASV's tar reads
#
# The packages are core-2.1.pkg and Cdev-2.1.pkg, SVR4 datastreams. Put them
# in PKGDIR (default OUT); with FETCH=1 they are downloaded from the AMIX
# package server and checked against its catalog's MD5 sums. They are
# Commodore's, so nothing made from them belongs in a repository.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/work}
pkgdir=${PKGDIR:-$out}
server=${AMIX_PKG_SERVER:-https://pkg.amigaux.org}
mkdir -p "$out" "$pkgdir"

fetch() {	# name (catalog field 1)
	[ -f "$pkgdir/catalog" ] || curl -sfL "$server/catalog" -o "$pkgdir/catalog"
	line=$(grep "^$1|" "$pkgdir/catalog") || { echo "no $1 in the catalog" >&2; exit 1; }
	path=$(echo "$line" | cut -d'|' -f4); md5=$(echo "$line" | cut -d'|' -f5)
	f="$pkgdir/$(basename "$path")"
	if [ ! -f "$f" ]; then
		[ "$FETCH" = 1 ] || { echo "missing $f (FETCH=1 downloads it)" >&2; exit 1; }
		curl -sfL "$server/pkgs/$path" -o "$f"
	fi
	echo "$md5  $f" | md5sum -c --quiet
	echo "$f"
}

core=$(fetch core); cdev=$(fetch cdev)
rm -rf "$out/x" "$out/sysroot" "$out/amx"
python3 "$here/unpkg.py" "$core" "$out/x/core"
python3 "$here/unpkg.py" "$cdev" "$out/x/cdev"

mkdir -p "$out/sysroot"
for p in core cdev; do
	for part in "$out/x/$p"/part*; do
		[ -d "$part/root" ] && cp -a "$part/root/." "$out/sysroot/"
	done
done
chmod -R u+rw "$out/sysroot"
# the headers were written for AT&T cc -Xa, where __STDC__ is 0; gcc sets 1
grep -rlE '__STDC__ *(- *0 *)?== *0' "$out/sysroot/usr/include" | while read f; do
	sed -i -E 's/__STDC__ *(- *0 *)?== *0/1/g' "$f"
done
for f in usr/include/stdio.h usr/lib/libc.so.1 usr/ccs/lib/crt1.o usr/ccs/lib/libc.a; do
	[ -f "$out/sysroot/$f" ] || { echo "sysroot lacks $f" >&2; exit 1; }
done

mkdir -p "$out/amx"
for l in libc.so.1 ld.so.1 libsocket.so libnsl.so libsockhost.so libsockdns.so libdl.so; do
	[ -f "$out/sysroot/usr/lib/$l" ] && cp "$out/sysroot/usr/lib/$l" "$out/amx/"
done
python3 "$here/amixify.py" "$out/amx/libc.so.1" "$out/amx/ld.so.1"
chmod 755 "$out"/amx/*
(cd "$out" && tar --format=v7 -cf amx.tar amx)
echo "sysroot: $out/sysroot"
echo "for /usr/amx: $out/amx.tar  (on the TT: cd /usr && tar xf amx.tar)"
