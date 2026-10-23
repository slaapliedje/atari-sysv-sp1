#!/bin/sh
# mktests.sh OUTDIR - the test and diagnostic programs for
# /usr/local/lib/sp1/tests, built static on the PC (rsync/asv-static-cc,
# which needs ASV_SYSROOT), plus setboot for /usr/local/bin and the README.
set -e
out=${1:?usage: mktests.sh OUTDIR}
here=$(cd "$(dirname "$0")" && pwd); sp1=$(cd "$here/.." && pwd)
: "${ASV_SYSROOT:=$HOME/opt/asv-cross/m68k-cbm-sysv4/sysroot}"; export ASV_SYSROOT
[ -d "$ASV_SYSROOT" ] || { echo "mktests.sh: no ASV sysroot at $ASV_SYSROOT" >&2; exit 1; }
rm -rf "$out"; mkdir -p "$out"
cc="$sp1/rsync/asv-static-cc"
build() {	# build DIR NAME...
	d=$1; shift
	for n; do
		(cd "$sp1/$d" && bash "$cc" -O -o "$out/$n" "$n.c") || { echo "mktests.sh: $d/$n failed" >&2; exit 1; }
	done
}
build tools/snd khz cachet mixbench sndprobe sndstat sndrate sndtest
build driver-midi miditool
build tools/atw atwprobe atw4mb atwstart atwbars atwtest atwblit atwblstat atwblspeed atwfill atwtorture
build atw800 atwfb
build tools/tram ttest tdiag tmem tfpu tspy
build tools/setboot setboot
# atwtest.sh names the program's place
sed 's|/usr/local/bin/atwtest|/usr/local/lib/sp1/tests/atwtest|' "$sp1/tools/atw/atwtest.sh" > "$out/atwtest.sh"
cp "$here/tests.README" "$out/README"
ls "$out" | wc -l
