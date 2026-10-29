#!/bin/sh
# mkmod.sh - a loadable module for Atari System V from its objects, on the PC:
#
#   sh mkmod.sh -e exports [-d depmodule]... -o outdir NAME PREFIX "DEPENDS" obj...
#
# Links the objects with a .moddata section (the address of PREFIX_wrapper,
# then the names of the modules it depends on) and runs upstream's modfix,
# which checks the result and refuses a module that needs a symbol neither
# the kernel (-e, one name per line) nor a dependency (-d) defines.  The
# module lands in outdir/NAME.  Upstream's tools/mkmod does the same from
# a module directory (Driver.o, Master) on the AMIX toolchain.
set -e
here=$(cd "$(dirname "$0")" && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
X=$ASV_CROSS/bin/m68k-cbm-sysv4
EXP= DEPS= OUT=.
while [ $# -gt 0 ]; do
	case "$1" in
	-e) EXP=$2; shift 2 ;;
	-d) DEPS="$DEPS -d $2"; shift 2 ;;
	-o) OUT=$2; shift 2 ;;
	*) break ;;
	esac
done
[ $# -ge 4 ] || { echo "usage: mkmod.sh -e exports [-d dep]... -o outdir NAME PREFIX DEPENDS obj..." >&2; exit 2; }
NAME=$1 PREFIX=$2 DEPEND=$3; shift 3
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
{
	echo '	.section .moddata,"aw",@progbits'
	echo '	.balign	4'
	echo "	.long	${PREFIX}_wrapper"
	[ -z "$DEPEND" ] || echo "	.ascii	\"$DEPEND\""
	echo '	.byte	0'
} > "$W/moddep.s"
"$X-as" -o "$W/moddep.o" "$W/moddep.s"
"$X-ld" -r -o "$W/$NAME" "$@" "$W/moddep.o"
"$here/build/modfix" ${EXP:+-e "$EXP"} $DEPS -p "$PREFIX" -m "$NAME" "$W/$NAME"
mkdir -p "$OUT"
cp "$W/$NAME" "$OUT/$NAME"
echo "mkmod: $OUT/$NAME"
