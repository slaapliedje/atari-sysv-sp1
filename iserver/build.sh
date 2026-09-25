#!/bin/sh
# build.sh - iserver, the INMOS host file server, for Atari System V / AMIX,
# talking to the ATW800/2's transputers through /dev/link0 and /dev/link1.
#
#   sh build.sh ISERVER_SRC [OUT]
#
# ISERVER_SRC is the iserver of the ATW800/2 file share
# (Transputer/atari.dev/iserver: iserver.c hostc.c filec.c serverc.c
# iserver.h inmos.h pack.h). Those sources are INMOS's (c) 1988, All Rights
# Reserved, and are NOT in this repository: this script copies them into
# a build directory, makes the few edits below and links them with
# lnktlk.c. Needs OpenUA's toolchain/sysv4-cc and sysv4-ld (AMIX_SYSROOT).
set -e
SRC=${1:?usage: build.sh ISERVER_SRC [OUT]}
OUT=${2:-iserver}
HERE=$(cd "$(dirname "$0")" && pwd)
: "${SYSV4:=$HERE/../../../../toolchain}"
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT
for f in iserver.c hostc.c filec.c serverc.c iserver.h inmos.h pack.h; do
	cp "$SRC/$f" "$B/"
done
# the Atari edit's "unsigned  unsigned char"
sed -i 's/^unsigned  unsigned char BootBuffer/unsigned char BootBuffer/' "$B/iserver.c"
# SunOS's tm_gmtoff: SVR4 has timezone (seconds west, set by localtime)
sed -i 's/Time = UTCTime + (localtime(&UTCTime))->tm_gmtoff;/Time = UTCTime - timezone + (localtime(\&UTCTime)->tm_isdst > 0 ? 3600 : 0);/' "$B/hostc.c"
grep -q 'UTCTime - timezone' "$B/hostc.c"
# SUN = the Unix host code (termios, fopen modes); sun3 = big-endian;
# BOARD_ID 1 = B004, a C011 board (anything but UDP)
CFLAGS="-std=gnu89 -O1 -m68020-60 -msoft-float -w -DSUN -Dsun3 -DBOARD_ID=1 -include $HERE/asvhost.h"
cp "$HERE/lnktlk.c" "$B/"
for f in iserver hostc filec serverc; do
	"$SYSV4/sysv4-cc" $CFLAGS -c "$B/$f.c" -o "$B/$f.o"
done
"$SYSV4/sysv4-cc" -std=gnu99 -O1 -m68020-60 -msoft-float -Wall -I"$HERE/../driver-tlk" -c "$HERE/lnktlk.c" -o "$B/lnktlk.o"
"$SYSV4/sysv4-ld" -o "$OUT" "$B/iserver.o" "$B/hostc.o" "$B/filec.o" "$B/serverc.o" "$B/lnktlk.o"
echo "built $OUT"
