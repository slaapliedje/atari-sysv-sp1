#!/bin/sh
# run.sh -- static tests of the loadable-module core.
#
#   sh test/run.sh image.elf builddir
#
# image.elf: the kernel the modules resolve against.  If its dlm_ksym is
# filled (mkksym -c passes) the tests use that block, else mkksym -o
# builds one from the image's symbol table.
#
#  1. modules: mkmod builds dlmdep, dlmtest (-O), dlmtest2 (-O2 -m68040),
#     rall (assembler, every relocation type) and two A/UX objects
#     converted by coff2elf (at_sig, ddp) with a C wrapper
#  2. relocation oracle: each module loaded by the host build of
#     dlm_ld.c at a chosen base must be byte-identical to m68k-elf-ld
#     linking it at that base against the image
#  3. error paths: damaged files and assembler cases -> the errno
#  4. state harness: dlm_core.c against simulated kernel services
#
# One PASS/FAIL line per check; exit 1 if any failed.
set -e

T=$(cd "$(dirname "$0")" && pwd)
DLM=$(cd "$T/.." && pwd)
AUX=$(cd "$DLM/../.." && pwd)
IMG=$1
B=$2
. "$AUX/toolchain/src/gcc-cross-amix/build/env.sh"
PATH="$AUX/toolchain/bin:$PATH"
export PATH
TB=$B/tools
W=$B/test
rm -rf "$W"
mkdir -p "$W"
NICE="nice -n 19"
sh "$T/cslots-check.sh" "$W"
fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# ---- kernel table ----
if "$TB/mkksym" -c "$IMG" > "$W/ksym.chk" 2>&1; then
	python3 - "$IMG" "$W/ksym.bin" <<'EOF'
import struct, sys
d = open(sys.argv[1], 'rb').read()
u16 = lambda o: struct.unpack('>H', d[o:o+2])[0]
u32 = lambda o: struct.unpack('>I', d[o:o+4])[0]
sh = [u32(32) + 40 * i for i in range(u16(48))]
st = [s for s in sh if u32(s + 4) == 2][0]
strs = u32(sh[u32(st + 24)] + 16)
for k in range(u32(st + 20) // 16):
    o = u32(st + 16) + 16 * k
    n = u32(o)
    if d[strs + n:d.index(b'\0', strs + n)] == b'dlm_ksym' and u16(o + 14):
        s = sh[u16(o + 14)]
        off = u32(s + 16) + u32(o + 4) - u32(s + 12)
        open(sys.argv[2], 'wb').write(d[off:off + u32(o + 8)])
        break
EOF
	echo "kernel table: dlm_ksym of $IMG ($(cat "$W/ksym.chk"))"
	# independent of mkksym: the block against m68k-elf-nm
	m68k-elf-nm "$IMG" | awk 'NF==3 && $2 ~ /^[A-TV-Z]$/ {print $3, $1}' > "$W/nm.globals"
	if python3 - "$W/ksym.bin" "$W/nm.globals" > "$W/nmcheck" <<'EOF2'
import struct, sys
b = open(sys.argv[1], 'rb').read()
u32 = lambda o: struct.unpack('>I', b[o:o+4])[0]
nsym, so, stro = u32(8), u32(12), u32(16)
tab = {}
for i in range(1, nsym):
    e = so + 16 * i
    n = stro + u32(e)
    tab[b[n:b.index(b'\0', n)].decode()] = u32(e + 4)
nm = dict(l.split() for l in open(sys.argv[2]))
bad = [k for k in nm if tab.get(k) != int(nm[k], 16)]
print('%d nm globals, %d table entries, %d mismatches' % (len(nm), len(tab), len(bad)))
sys.exit(1 if bad or len(nm) != len(tab) else 0)
EOF2
	then pass "dlm_ksym = m68k-elf-nm globals: $(cat "$W/nmcheck")"
	else fail "dlm_ksym vs m68k-elf-nm: $(cat "$W/nmcheck")"; fi
else
	"$TB/mkksym" -o "$W/ksym.bin" "$IMG" 2> "$W/ksym.chk"
	echo "kernel table: built from $IMG ($(cat "$W/ksym.chk"))"
fi
"$TB/mkksym" -x "$IMG" > "$W/exports"
symdefs() { awk '{printf " --defsym=%s=%s", $1, $2}' "$1"; }
# an image without the DLM: its module API gets fake addresses
KX= KLD=
if ! grep -qx mod_miscops "$W/exports"; then
	m68k-elf-nm "$B/obj/dlm.o" | awk '$2 ~ /^[TDB]$/ && $3 ~ /^mod_/ {print $3}' |
		awk '{printf "%s 0x%x\n", $1, 0x00d00000 + 16 * NR}' > "$W/dlmk.sym"
	awk '{print $1}' "$W/dlmk.sym" >> "$W/exports"
	KX="-S $W/dlmk.sym"
	KLD=$(symdefs "$W/dlmk.sym")
	echo "image has no DLM: $(awk '{printf "%s ", $1}' "$W/dlmk.sym")at fake addresses"
fi

# ---- 1. modules ----
CC="m68k-cbm-sysv4-gcc $AMIX_KERNEL_CFLAGS -I$DLM/include"
M=$W/mods
S=$W/src
mkdir -p "$M" "$S"
mkmod() {	# mkmod dir [mkmod args]
	d=$1; shift
	if DLM_TOOLS="$TB" $NICE sh "$DLM/tools/mkmod" "$@" -o "$M" "$d" > "$W/mkmod.log" 2>&1; then
		pass "mkmod $(basename "$d")"
	else
		fail "mkmod $(basename "$d")"; cat "$W/mkmod.log"
	fi
}
for m in dlmdep dlmtest dlmtest2 rall atsig ddp; do
	mkdir -p "$S/$m"
	cp "$T/mod/$m/"* "$S/$m/" 2>/dev/null || true
done
$NICE $CC -c "$T/mod/dlmdep/dlmdep.c" -o "$S/dlmdep/Driver.o"
$NICE $CC -c "$T/mod/dlmtest/dlmtest.c" -o "$S/dlmtest/Driver.o"
$NICE $CC -O2 -m68040 -c "$T/mod/dlmtest/dlmtest.c" -o "$S/dlmtest2/Driver.o"
$NICE m68k-cbm-sysv4-as -o "$S/rall/Driver.o" "$T/mod/rall/rall.s"
mkmod "$S/dlmdep" -e "$W/exports"
mkmod "$S/dlmtest" -e "$W/exports" -d "$M/mod.d/dlmdep"
mkmod "$S/dlmtest2" -e "$W/exports"	# dlmdep found in mod.d
check_undef() {	# the -O2 build must also need only kernel and dlmdep names
	m68k-elf-nm -u "$M/mod.d/$1" | awk '$1=="U"{print $2}' | sort > "$W/$1.und"
	if [ -z "$(grep -vxFf "$W/exports" "$W/$1.und" | grep -vx 'dlmdep_add\|dlmdep_value')" ]; then
		pass "$1: every undefined symbol is a kernel or dlmdep export"
	else fail "$1: undefined symbols outside the kernel list"; fi
}
check_undef dlmtest
check_undef dlmtest2
grep -qx 'misc	dlmtest' "$M/mod_register.d/dlmtest" && [ -f "$M/loadmods.d/dlmtest" ] &&
	pass "mkmod: registration line and loadmods entry" || fail "mkmod registration outputs"

# rall: extra absolute symbols
cat > "$W/rall.sym" <<EOF
abs16 0x1234
absneg 0xfffffff0
abs8 0x7f
abs8u 0xff
absm8 0xffffff80
EOF
awk '{print $1}' "$W/rall.sym" | cat "$W/exports" - > "$W/rall.exports"
mkmod "$S/rall" -e "$W/rall.exports"

# A/UX objects: coff2elf, then a wrapper; A/UX-only names get fake addresses
C2E="$AUX/tools/coff2elf/build/coff2elf"
[ -n "$AUXROOT" ] || [ ! -f "$AUX/tests/aux/auxroot" ] || AUXROOT=$(cat "$AUX/tests/aux/auxroot")
AUXBOOTD=${AUXBOOTD:-${AUXROOT:-/aux}/etc/boot.d}
for m in at_sig ddp; do
	n=$(echo $m | tr -d _)
	[ -d "$S/$n" ] || mkdir -p "$S/$n"
	"$C2E" "$AUXBOOTD/$m" "$S/$n/Driver.o"
	printf '#include "sys/types.h"\n#include "sys/moddefs.h"\nMOD_MISC_WRAPPER(%s, 0, 0, "A/UX %s");\n' \
		"$n" "$m" > "$S/$n/Space.c"
	printf '$modtype misc\n%s\t%s\tm\t0\t0\t0\n' "$n" "$n" > "$S/$n/Master"
	m68k-elf-nm -u "$S/$n/Driver.o" | awk '{print $2}' | grep -vxFf "$W/exports" |
		awk '{printf "%s 0x%x\n", $1, 0x00e00000 + 16 * NR}' > "$W/$n.sym" || true
	awk '{print $1}' "$W/$n.sym" | cat "$W/exports" - > "$W/$n.exports"
	mkmod "$S/$n" -e "$W/$n.exports"
done

# ---- 2. relocation oracle ----
O=$W/oracle
mkdir -p "$O"
oracle() {	# oracle module base [reloc args] -- ld args
	mod=$1 base=$2; shift 2
	ra="$KX" la=
	while [ $# -gt 0 ] && [ "$1" != -- ]; do ra="$ra $1"; shift; done
	[ $# -gt 0 ] && shift
	la="$* $KLD"
	f="$M/mod.d/$mod"
	tag="$mod@$base"
	if ! "$B/test-bin/reloc" -k "$W/ksym.bin" $ra -L "$O/$tag.lay" -b "$base" "$f" > "$O/$tag.log" 2>&1; then
		fail "oracle $tag: loader: $(cat "$O/$tag.log")"; return
	fi
	python3 "$T/oracle.py" script "$f" "$O/$tag.lay" > "$O/$tag.ld"
	# kernel symbols as assignments, minus the names the module defines
	# (its own definition wins in the loader; --just-symbols would not)
	m68k-elf-nm "$f" | awk '$2 ~ /^[TDBRAVW]$/ {print $3}' | sort -u > "$O/$tag.own"
	m68k-elf-nm "$IMG" | awk '$2 ~ /^[TDBRAVW]$/ {print $3, $1}' |
		awk 'NR==FNR {own[$1]=1; next} !($1 in own) {printf "\"%s\" = 0x%s;\n", $1, $2}' \
		"$O/$tag.own" - > "$O/$tag.ksyms"
	if ! m68k-elf-ld -e 0 -o "$O/$tag.out" -T "$O/$tag.ld" $la "$f" "$O/$tag.ksyms" > "$O/$tag.ldlog" 2>&1; then
		fail "oracle $tag: m68k-elf-ld: $(head -3 "$O/$tag.ldlog")"; return
	fi
	python3 "$T/oracle.py" commons "$f" "$O/$tag.out" > "$O/$tag.com"
	"$B/test-bin/reloc" -k "$W/ksym.bin" $ra -C "$O/$tag.com" -L "$O/$tag.lay" \
		-o "$O/$tag.img" -b "$base" "$f" > "$O/$tag.log" 2>&1
	nrel=$(m68k-elf-readelf -rW "$f" | grep -c 'R_68K' || true)
	types=$(m68k-elf-readelf -rW "$f" | awk '/R_68K/{print $3}' | sort | uniq -c | awk '{printf "%s%s:%s", s, $2, $1; s=" "}')
	if python3 "$T/oracle.py" compare "$f" "$O/$tag.lay" "$O/$tag.out" "$O/$tag.img" > "$O/$tag.cmp"; then
		pass "oracle $tag: $(wc -c < "$O/$tag.img") bytes identical, $nrel relocations ($types), $(wc -l < "$O/$tag.com") commons"
	else
		fail "oracle $tag: $(cat "$O/$tag.cmp")"
	fi
}
oracle dlmdep 0x00380000
oracle dlmtest 0x00300000 -D "$M/mod.d/dlmdep@0x00380000" -- --just-symbols="$O/dlmdep@0x00380000.out"
oracle dlmtest 0x0a123450 -D "$M/mod.d/dlmdep@0x00380000" -- --just-symbols="$O/dlmdep@0x00380000.out"
oracle dlmtest2 0x00300000 -D "$M/mod.d/dlmdep@0x00380000" -- --just-symbols="$O/dlmdep@0x00380000.out"
oracle rall 0x00300000 -S "$W/rall.sym" -- $(symdefs "$W/rall.sym")
oracle rall 0x7ff00000 -S "$W/rall.sym" -- $(symdefs "$W/rall.sym")
oracle atsig 0x00300000 -S "$W/atsig.sym" -- $(symdefs "$W/atsig.sym")
oracle ddp 0x00300000 -S "$W/ddp.sym" -- $(symdefs "$W/ddp.sym")

# ---- 3. error paths ----
N=$W/neg
mkdir -p "$N"
python3 "$T/mkneg.py" "$M/mod.d/rall" "$N" > "$N/list"
asmneg() {	# asmneg name errno what source
	printf '%s\n\t.data\n\t.globl\t%s_wrapper\n%s_wrapper:\n\t.long\t1,0,0,0,0,0\n' "$4" "$1" "$1" > "$N/$1.s"
	printf '\t.section .moddata,"aw",@progbits\n\t.balign\t4\n\t.long\t%s_wrapper\n' "$1" >> "$N/$1.s"
	m68k-cbm-sysv4-as -o "$N/$1.o" "$N/$1.s"
	m68k-cbm-sysv4-ld -r -o "$N/$1" "$N/$1.o"
	"$TB/modfix" -p "$1" -m "$1" "$N/$1"
	echo "$N/$1 $2 $3" >> "$N/list"
}
asmneg pc16ovf 165 'R_68K_PC16 out of range' '	.text
	bsr.w	printf'
asmneg pc8ovf 165 'R_68K_PC8 out of range' '	.text
	bsr.b	printf
	nop'
asmneg r16ovf 165 'R_68K_16 out of range' '	.data
	.word	printf'
asmneg r8ovf 165 'R_68K_8 out of range' '	.data
	.byte	abs16'
asmneg undef 165 'undefined symbol' '	.text
	jsr	no_such_symbol'
while read f want what; do
	got=$("$B/test-bin/reloc" -k "$W/ksym.bin" $KX -S "$W/rall.sym" -b 0x300000 "$f" 2>/dev/null | awk '/^ERR/{print $2} /^OK/{print 0}')
	if [ "$got" = "$want" ]; then pass "error path: $what -> $want"
	else fail "error path: $what: got '$got' want $want"; fi
done < "$N/list"
cp "$N/relnone" "$M/mod.d/relnone"
oracle relnone 0x00300000 -S "$W/rall.sym" -- $(symdefs "$W/rall.sym")

# ---- 4. state harness ----
R=$W/root/etc/conf/mod.d
mkdir -p "$R"
python3 "$T/mkmods.py" "$W/hmods" > "$W/hmods.list"
while read n s; do
	m68k-cbm-sysv4-as -o "$W/hmods/$n.o" "$s"
	m68k-cbm-sysv4-ld -r -o "$R/$n" "$W/hmods/$n.o"
	"$TB/modfix" -p "$n" -m "$n" "$R/$n"
done < "$W/hmods.list"
if "$B/test-bin/state" "$W/ksym.bin" "$W/root" > "$W/state.log" 2>&1; then
	sed 's/^/state: /' "$W/state.log" | grep -v '^state: PASS'
	pass "state harness: $(tail -1 "$W/state.log")"
else
	sed 's/^/state: /' "$W/state.log" | grep -v '^state: PASS'
	fail "state harness"
fi

echo "tests: $fails failed"
[ $fails -eq 0 ]
