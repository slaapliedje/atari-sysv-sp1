#!/bin/sh
# build.sh -- loadable kernel modules (stages 0 and 1): kernel objects,
# host tools, target programs, and the static tests.
#
#   sh kernel/dlm/build.sh [image.elf]
#   sh kernel/dlm/build.sh -k outdir
#
# -k: only the kernel objects and the post-link tool, for relink-mac.sh:
#     outdir/dlm.o and outdir/mkksym.
#
# image.elf: the kernel the tests resolve against (default
# kernel/build/unix-mac.elf).  With a filled dlm_ksym the tests use that
# block; otherwise mkksym builds one from the image's symbols.
#
# Out, under kernel/dlm/build/:
#   obj/dlm.o          the kernel objects, ld -r'd, for relink-mac.sh
#   tools/mkksym, tools/modfix   host tools (tools/mkmod is a script)
#   target/libmod.a, target/modadmin   AMIX programs
#   test/              modules, oracle and harness work files
# One PASS/FAIL line per stage; exit 1 on the first failed stage.
set -e

DLM=$(cd "$(dirname "$0")" && pwd)
K=$(cd "$DLM/.." && pwd)
AUX=$(cd "$K/.." && pwd)
. "$AUX/toolchain/src/gcc-cross-amix/build/env.sh"
PATH="$AUX/toolchain/bin:$PATH"
export PATH
NICE="nice -n 19"
HCC="$NICE ${HOSTCC:-cc} -std=gnu89 -O -g -Wall -Wno-parentheses -Wno-unused-but-set-variable"
KCC="$NICE m68k-cbm-sysv4-gcc $AMIX_KERNEL_CFLAGS -Wall -I$DLM/include -I$DLM"

# kernel objects into $1 (dlm.o), warnings into $2
kobj() {
	rm -f "$2"
	for f in dlm_core dlm_ld dlm_sym dlm_slot dlm_str dlm_fs dlm_boot dlmconf; do
		$KCC -c "$DLM/$f.c" -o "$1/$f.o" 2>> "$2"
	done
	for f in dlm_cache dlmksym dlm_hooksw; do
		$NICE m68k-cbm-sysv4-gcc -c "$DLM/$f.s" -o "$1/$f.o"
	done
	# the root mount hook; the SCSI host adapter list on the base's sd.h layer
	$KCC -c "$DLM/dlm_root.c" -o "$1/dlm_root.o" 2>> "$2"
	$KCC -I"$AMIX_ROOT/usr/sys/amiga/alien" -c "$DLM/dlm_sd.c" -o "$1/dlm_sd.o" 2>> "$2"
	m68k-cbm-sysv4-ld -r -o "$1/dlm.o" "$1/dlm_core.o" "$1/dlm_ld.o" \
		"$1/dlm_sym.o" "$1/dlm_slot.o" "$1/dlm_str.o" "$1/dlm_fs.o" "$1/dlm_boot.o" "$1/dlmconf.o" "$1/dlm_cache.o" \
		"$1/dlmksym.o" "$1/dlm_hooksw.o"
	# no warnings beyond the AMIX header's own
	! grep -v 'types.h:182\|In file included\|^ *from ' "$2" | grep .
}

if [ "$1" = -k ]; then
	mkdir -p "$2"
	O=$(cd "$2" && pwd)
	kobj "$O" "$O/kobj.warn"
	$HCC -DDLM_TOOL -I"$DLM" -I"$DLM/include" -o "$O/mkksym" \
		"$DLM/tools/mkksym.c" "$DLM/dlm_sym.c"
	exit 0
fi

IMG=${1:-$K/build/unix-mac.elf}
B=$DLM/build
LOG=$B/logs
mkdir -p "$B/obj" "$B/tools" "$B/target" "$B/test-bin" "$LOG"

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1: $2 (log: $LOG/$1.log)"; exit 1; }

# 1. host tools
{
	$HCC -DDLM_TOOL -I"$DLM" -I"$DLM/include" -o "$B/tools/mkksym" \
		"$DLM/tools/mkksym.c" "$DLM/dlm_sym.c" &&
	$HCC -o "$B/tools/modfix" "$DLM/tools/modfix.c" &&
	$HCC -DDLM_HOST -I"$DLM" -I"$DLM/include" -o "$B/test-bin/reloc" \
		"$DLM/test/reloc.c" "$DLM/dlm_ld.c" "$DLM/dlm_sym.c" &&
	$HCC -DDLM_HOST -I"$DLM" -I"$DLM/include" -o "$B/test-bin/state" \
		"$DLM/test/state.c" "$DLM/test/hostk.c" "$DLM/dlm_core.c" \
		"$DLM/dlm_ld.c" "$DLM/dlm_sym.c" "$DLM/dlm_slot.c" "$DLM/test/hconf.c"
} > "$LOG/tools.log" 2>&1 || fail tools "host compile"
pass tools

# 2. kernel objects
{
	kobj "$B/obj" "$LOG/kobj.warn"
	# everything dlm.o needs must be in the kernel (cputype is weak)
	m68k-elf-nm "$IMG" | awk '{print $NF}' | sort -u > "$B/obj/kernel.names"
	m68k-elf-nm -u "$B/obj/dlm.o" | awk '$1=="U"{print $2}' > "$B/obj/dlm.undef"
	# __amix_* are the relink's aliases of replaced stock tables
	miss=$(grep -vxFf "$B/obj/kernel.names" "$B/obj/dlm.undef" | grep -v '^__amix_' || true)
	echo "undefined in dlm.o: $(wc -l < "$B/obj/dlm.undef"), not in the kernel: ${miss:-none}"
	[ -z "$miss" ]
} > "$LOG/kobj.log" 2>&1 || fail kobj "kernel objects"
pass "kobj ($(m68k-elf-size "$B/obj/dlm.o" | awk 'NR==2{print $1 "+" $2 "+" $3}') text+data+bss)"

# 3. target programs
{
	for f in modload moduload modpath modstat modadm getksym; do
		m68k-cbm-sysv4-as -o "$B/target/$f.o" "$DLM/libmod/$f.s"
	done
	rm -f "$B/target/libmod.a"
	m68k-elf-ar rcs "$B/target/libmod.a" "$B/target"/mod*.o "$B/target/getksym.o"
	$NICE m68k-cbm-sysv4-gcc -O -I"$DLM/include" -o "$B/target/modadmin" \
		"$DLM/modadmin.c" "$B/target/libmod.a"
	m68k-elf-readelf -h "$B/target/modadmin" | grep -q 'EXEC'
} > "$LOG/target.log" 2>&1 || fail target "libmod.a / modadmin"
pass target

# 4. static tests
sh "$DLM/test/run.sh" "$IMG" "$B" > "$LOG/test.log" 2>&1 || {
	grep '^FAIL' "$LOG/test.log"; fail test "see log"; }
pass "test ($(grep -c '^PASS' "$LOG/test.log") checks)"
