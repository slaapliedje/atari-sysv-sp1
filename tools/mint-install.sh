#!/bin/bash
# mint-install.sh IMAGE CARD.IMG [OFFSET]
#
# FreeMiNT from the FreeMiNT OS Distro's card image
# (github.com/rootadm79/freemint_os_distro, 26.2: a byte-swapped
# CF/IDE image with C: at sector 2) into the C: partition of a dual-boot
# HD0 image, beside TOS and MagiC, started by the boot menu
# (tools/osmenu) and not from the AUTO folder.  Needs mtools.
#
# Taken: MINT (kernel 1.19, XaAES, drivers; the distro's AUTO\05MTLOAD.PRG
# as MINT\MTLOAD.PRG, the kernel chooser OSMENU.INF points at), APPS,
# ACC, CPX, BTFONTS, CLIPBRD, GUIDES, COPS.INF.
# Left out, on purpose: NVDI and WDIALOG (03NVDI.PRG, 04WDIAL.PRG,
# NVDITOOLS, NVDI.INF, GEMSYS: NVDI's drivers, printer drivers and
# fonts; the ATW800/2 has XVDI), the demo HDDRIVER.SYS, HDDRCONF.CPX and
# APPS\HDDRIVER (the owner's HDDriver is on C:), 01MAPROM.PRG,
# 02FVDI.PR, VIDELITY and the Videl/CT60 CPXs and accessories (Falcon
# and CT60 only), the boot and XaAES logs.
# MINT.CNF: hostname tt030; the PicoWiFi lines replaced by commented
# NetUSBee ones (the ENEC3.XIF EtherNEC driver loads by itself; it hangs
# a Hatari without a NetUSBee, so take it out for emulator tests).
# The ext2 userland (drive E: in MINT.CNF) is a separate step.
# With mint/ufsxfs/UFS.XFS built, it goes into MINT\1-19-CUR too: the
# Unix slices of the same disk as MiNT drives V: W: X: (read-only).
set -e
IMG=$1; CARD=$2; D="$IMG@@${3:-1024}"
[ -f "$IMG" ] && [ -f "$CARD" ] || { echo "usage: $0 IMAGE CARD.IMG [OFFSET]" >&2; exit 1; }
export MTOOLS_SKIP_CHECK=1
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
# the distro's C:, unswapped
dd if="$CARD" of="$T/C.fs" bs=512 skip=2 count=977586 conv=swab status=none
S="$T/C.fs"
mkdir "$T/tree"
for d in ACC APPS BTFONTS CLIPBRD CPX GUIDES MINT; do mcopy -s -n -m -i "$S" "::/$d" "$T/tree/"; done
mcopy -n -m -i "$S" ::/COPS.INF ::/AUTO/05MTLOAD.PRG "$T/tree/"
cd "$T/tree"
mv 05MTLOAD.PRG MINT/MTLOAD.PRG
rm -rf APPS/HDDRIVER CPX/NVDICONF.CPX CPX/NPRNCONF.CPX CPX/NCACHE.CPX CPX/HDDRCONF.CPX \
	CPX/CT60CONF.CPX CPX/VIDEOCNF.CPX CPX/FSOUND.CPX ACC/CT60TEMP.AP ACC/CT60TEMP.APP \
	MINT/1-19-CUR/XAAES/XA_BOOT.LOG MINT/1-19-CUR/MINT.BAK MINT/1-19-CUR/XAAES/XAAES.BAK
python3 - MINT/1-19-CUR/MINT.CNF <<'EOF'
import sys, re
p = sys.argv[1]; s = open(p, 'rb').read().decode('latin1')
s = s.replace('setenv HOSTNAME myFalcon', 'setenv HOSTNAME tt030')
s = re.sub(r'exec u:/sbin/ifconfig pwi .*\r?\n',
	'# NetUSBee: the ENEC3.XIF driver loads by itself; give en0 an address here, e.g.\r\n'
	'# exec u:/sbin/ifconfig en0 addr 192.168.0.2 netmask 255.255.255.0 up\r\n', s)
s = re.sub(r'exec u:/sbin/route add default pwi .*\r?\n', '# exec u:/sbin/route add default en0 gw 192.168.0.1\r\n', s)
open(p, 'wb').write(s.encode('latin1'))
EOF
find . -type d ! -path . | sort | while read -r d; do mmd -i "$D" "::/${d#./}" 2>/dev/null || true; done
find . -type f | sort | while read -r f; do mcopy -m -D o -i "$D" "$f" "::/${f#./}"; done
UFS="$(dirname "$0")/../mint/ufsxfs/UFS.XFS"
[ -f "$UFS" ] && mcopy -m -D o -i "$D" "$UFS" ::/MINT/1-19-CUR/UFS.XFS && echo "ufs.xfs installed"
echo "FreeMiNT installed: $(find . -type f | wc -l) files; kernel chooser ::/MINT/MTLOAD.PRG"
