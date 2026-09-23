#!/bin/sh
# mksysroot.sh REAL_SYSROOT SHADOW - a "fixincluded" view of the ASV sysroot.
#
# ASV's headers were written for AT&T cc -Xa, where __STDC__ is 0; gcc
# defines it as 1, which hides sigset_t (and with it setjmp.h), the extra
# math and string declarations, and more. gcc's own fixincludes would
# rewrite them on a native install; for the cross toolchain we make a
# shadow sysroot whose usr/include is a fixed copy and whose other
# directories are symlinks, and point the wrapper at it (AMIX_SYSROOT).
#
# stdio: executables must never get a copy of __iob. libc.so.1 works on its
# own __iob directly, so when GNU ld copy-relocates __iob (or one of its
# 16-byte aliases __stdinb/__stdoutb/__stderrb, which stdio.h uses when
# m68k is defined) into a program, the program's stdout and libc's are
# two FILEs over one buffer: stderr writes fail with EINVAL, and mixing
# printf with putchar/fputs(stdout) garbles the output. The fixed stdio.h
# reaches the streams through __asv_iob(), a function compiled -fpic
# (asviob.c) whose GOT entry binds to libc's own __iob at run time.
set -e
REAL=$1; SHADOW=$2
[ -d "$REAL/usr/include" ] || { echo "no sysroot at $REAL" >&2; exit 1; }
rm -rf "$SHADOW"; mkdir -p "$SHADOW/usr"
for d in "$REAL"/usr/*; do
	n=`basename "$d"`
	[ "$n" = include ] || ln -s "$d" "$SHADOW/usr/$n"
done
cp -R "$REAL/usr/include" "$SHADOW/usr/include"
chmod -R u+w "$SHADOW/usr/include"
grep -rlE '__STDC__ *- *0 *== *0' "$SHADOW/usr/include" | while read f; do
	sed -i -E 's/__STDC__ *- *0 *== *0/1/g' "$f"
done
S=$SHADOW/usr/include/stdio.h
sed -i -E 's/^#if[ \t]+m68k \|\| m88k[ \t]*$/#if 0 \/* ASV: no __std*b aliases *\//' "$S"
sed -i -E 's/^(#define[ \t]+std(in|out|err)[ \t]+)\(&__iob\[([012])\]\)/\1(\&__asv_iob()[\3])/' "$S"
sed -i -E 's/^(extern FILE[ \t]+__iob\[_NFILE\];)/\1\nextern FILE *__asv_iob(void);/' "$S"
grep -q '__asv_iob()\[2\]' "$S" || { echo "stdio.h fix did not apply" >&2; exit 1; }
echo "shadow sysroot: $SHADOW"
