#!/bin/sh
# build.sh - the module loader for Atari System V, on the PC:
#
#   sh dlm/asv/build.sh
#
# Out, under dlm/asv/build/:
#   DLM.o        the loader for /boot (mkboot with master.d/dlm)
#   spare.o      the empty object the placeholder drivers dlma..dlmh share
#   modadmin     the admin command, static, for ASV
#   mkksym       fills dlm_ksym in a linked kernel, static, for ASV
#   mkksym.host  the same on the PC (checks, and filling images there)
#   src/         upstream with asv.patch applied
#
# upstream/ stays as it came; asv.patch puts the ASV changes under
# DLM_ASV.  Kernel flags are upstream's AMIX ones: ASV's headers want
# __STDC__ == 0 (-traditional).
set -e
here=$(cd "$(dirname "$0")" && pwd)
up=$(cd "$here/../upstream" && pwd)
sp1=$(cd "$here/../.." && pwd)
: "${ASV_CROSS:=$HOME/opt/asv-cross}"
: "${ASV_SYSROOT:=$ASV_CROSS/m68k-cbm-sysv4/sysroot}"
X=$ASV_CROSS/bin/m68k-cbm-sysv4
B=$here/build
KSYM=114688		# 112 KB: build 18's kernel needs 97816 bytes
rm -rf "$B"; mkdir -p "$B/src" "$B/obj"
cp -r "$up/." "$B/src/"
(cd "$B/src" && patch -s -p1 < "$here/asv.patch")
S=$B/src
KF="-O -traditional -fno-builtin -Dinline=__inline__ -Dvolatile=__volatile__ -D_KERNEL -DDLM_ASV -DKSYM_SPACE=$KSYM -nostdinc -I$S/include -I$S -I$ASV_SYSROOT/usr/include -Wall -Wno-implicit -Wno-return-type"
: > "$B/kobj.warn"
for f in dlm_core dlm_ld dlm_sym dlm_slot dlm_str dlm_boot dlmconf; do
	"$X-gcc" $KF -c "$S/$f.c" -o "$B/obj/$f.o" 2>> "$B/kobj.warn"
done
"$X-gcc" $KF -c "$here/dlm_asv.c" -o "$B/obj/dlm_asv.o" 2>> "$B/kobj.warn"
"$X-gcc" -c "$here/dlm_cache_asv.s" -o "$B/obj/dlm_cache.o"	# ASV: cputype is 68030, not 30
"$X-gcc" -c "$S/dlm_hooksw.s" -o "$B/obj/dlm_hooksw.o"
sed "s/163840/$KSYM/g" "$S/dlmksym.s" > "$B/obj/dlmksym.s"
"$X-gcc" -c "$B/obj/dlmksym.s" -o "$B/obj/dlmksym.o"
"$X-ld" -r -o "$B/DLM.o" "$B/obj/dlm_asv.o" "$B/obj/dlm_core.o" "$B/obj/dlm_ld.o" \
	"$B/obj/dlm_sym.o" "$B/obj/dlm_slot.o" "$B/obj/dlm_str.o" "$B/obj/dlm_boot.o" \
	"$B/obj/dlmconf.o" "$B/obj/dlm_cache.o" "$B/obj/dlm_hooksw.o" "$B/obj/dlmksym.o"
"$X-size" "$B/DLM.o" | tail -1
# the placeholders' object: nothing in it
printf '\t.text\n' > "$B/obj/spare.s"
"$X-gcc" -c "$B/obj/spare.s" -o "$B/spare.o"
# ASV programs, static
CC="$sp1/rsync/asv-static-cc"
export ASV_SYSROOT
"$X-gcc" -c "$here/libmod.s" -o "$B/obj/libmod.o"
$CC -O -I"$S/include" -o "$B/modadmin" "$S/modadmin.c" "$B/obj/libmod.o"
$CC -O -traditional -DDLM_TOOL -DKSYM_SPACE=$KSYM -I"$S" -I"$S/include" -o "$B/mkksym" "$S/tools/mkksym.c" "$S/dlm_sym.c"
cc -std=gnu89 -O -DDLM_TOOL -DKSYM_SPACE=$KSYM -I"$S" -I"$S/include" -o "$B/mkksym.host" "$S/tools/mkksym.c" "$S/dlm_sym.c" 2>/dev/null
cc -std=gnu89 -O -o "$B/modfix" "$S/tools/modfix.c" 2>/dev/null
# the configuration: the loader, and the spare rows
mkdir -p "$B/master.d"
cat > "$B/master.d/dlm" <<EOT
*ident	"dlm - loadable kernel modules (sp1 dlm/, Ash Nazag's DLM)"
*
* DLM - not a driver: dlminit() starts the module loader at boot.
*
*FLAG	PREFIX	MAJOR	#DEV	DEPENDENCIES/VARIABLES
ox	dlm
EOT
e=110
for x in a b c d e f g h; do
	cat > "$B/master.d/dlm$x" <<EOT
*ident	"dlm$x - a spare character-driver row for loadable modules (sp1 dlm/)"
*
*FLAG	PREFIX	MAJOR	#DEV	DEPENDENCIES/VARIABLES
cs	dlm$x	$e
EOT
	e=$((e + 1))
done
# upstream's two test modules, for ASV; the kernel's exports from a
# reference kernel (ASV_KERNEL, a /stand/unix) plus the loader's own
if [ -n "$ASV_KERNEL" ]; then
	{ "$B/mkksym.host" -x "$ASV_KERNEL" 2>/dev/null || "$X-nm" "$ASV_KERNEL" | awk '$2 ~ /[TDBCAWV]/ {print $3}'
	  "$X-nm" "$B/DLM.o" | awk '$2 ~ /[TDBCA]/ {print $3}'; } | sort -u > "$B/exports"
	MF="-O -traditional -fno-builtin -D_KERNEL -nostdinc -I$S/include -I$ASV_SYSROOT/usr/include"
	for t in dlmdep dlmtest; do
		"$X-gcc" $MF -c "$S/test/mod/$t/$t.c" -o "$B/obj/$t.o" 2>> "$B/kobj.warn"
	done
	sh "$here/mkmod.sh" -e "$B/exports" -o "$B/mod.d" dlmdep dlmdep "" "$B/obj/dlmdep.o"
	sh "$here/mkmod.sh" -e "$B/exports" -d "$B/mod.d/dlmdep" -o "$B/mod.d" dlmtest dlmtest "dlmdep" "$B/obj/dlmtest.o"
fi
cp "$here/install.sh" "$B/install.sh"
echo "built: $(cd "$B" && ls DLM.o spare.o modadmin mkksym mkksym.host modfix | tr '\n' ' ')"
