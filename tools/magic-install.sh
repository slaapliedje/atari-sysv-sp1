#!/bin/bash
# magic-install.sh IMAGE MAGIC.A
#
# MagiC for Atari (Andreas Kromke's GPLv3 release, 6.30 and later:
# gitlab.com/AndreasK/Atari-Mac-MagiC-Sources, MagiC/Releases) into the
# C: partition of a dual-boot HD0 image (doc/dual-boot-disk.md: C: is the
# BGM partition at sector 2, 8 KiB logical sectors, which mtools reads
# with the @@1024 offset).  MAGIC.A is the unpacked release folder,
# localised first the way LANG/EN.BAT does it (see the doc).
#
# - AUTO\MAGXBOOT.PRG goes first in the AUTO folder: MagiC runs the
#   programs after it; the XVDI set before it hangs MagiC's booter.
# - AUTO\AUTOEXEC.BAT is NOT copied: when that file exists MagiC's kernel
#   runs it instead of the AUTO folder (KERNEL/BIOS/COMMON/AUTO.S), and
#   the release ships an empty one, which would leave XVDI, HNETDEV and
#   the rest unrun.
# - LANG is left out (localised on the PC already).
set -e
IMG=$1; SRC=$2; D="$IMG@@1024"
[ -f "$IMG" ] && [ -f "$SRC/MAGIC.RAM" ] || { echo "usage: $0 IMAGE MAGIC.A (localised: MAGIC.RAM in it)" >&2; exit 1; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir "$T/auto"
mcopy -n -m -i "$D" "::/AUTO/*" "$T/auto/" 2>/dev/null || true
for f in "$T"/auto/*; do [ -f "$f" ] && mdel -i "$D" "::/AUTO/$(basename "$f")"; done
mcopy -m -i "$D" "$SRC/AUTO/MAGXBOOT.TOS" ::/AUTO/MAGXBOOT.PRG
for f in "$T"/auto/*; do
	[ -f "$f" ] || continue
	case $(basename "$f") in AUTOEXEC.BAT) continue;; esac
	mcopy -m -i "$D" "$f" "::/AUTO/$(basename "$f")"
done
cd "$SRC"
find . -type d ! -path './LANG*' ! -path . | sort | while read -r d; do mmd -i "$D" "::/${d#./}" 2>/dev/null || true; done
find . -type f ! -path './LANG/*' ! -path './AUTO/MAGXBOOT.TOS' ! -path './AUTO/AUTOEXEC.BAT' | sort | while read -r f; do
	mcopy -m -D o -i "$D" "$f" "::/${f#./}"
done
for f in EXTRAS/KAOSDESK/EN/*.RSC; do mcopy -m -D o -i "$D" "$f" "::/EXTRAS/KAOSDESK/$(basename "$f")"; done
echo "MagiC installed; AUTO folder:"; mdir -i "$D" ::/AUTO | grep -v '<DIR>'
