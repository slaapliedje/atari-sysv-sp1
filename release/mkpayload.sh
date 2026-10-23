#!/bin/sh
# mkpayload.sh OUTDIR - gather everything the release build installs into
# OUTDIR/payload/ and tar it as OUTDIR/payload.tar (ustar, root-owned;
# ASV's tar reads it straight off a raw SCSI disk).  Run from a checkout
# of sp1 with the drivers' objects built (kbuild.sh), Manx built for sysv4
# (MANX), and the AMIX pieces built (see README.md): their locations are
# given by the variables below.
set -e
here=$(cd "$(dirname "$0")" && pwd); sp1=$(cd "$here/.." && pwd)
out=${1:?usage: mkpayload.sh OUTDIR}; mkdir -p "$out"
: "${MANX:=$HOME/dev/unix-browser}"
: "${REL:=$HOME/dev/asv-release}"            # amix/amx.tar, edt/, dl/
: "${STOCK:=$HOME/dev/OpenUA/data/work/asv/asv-richard/asv_boot.img}"
p=$out/rel; rm -rf "$p"; mkdir -p "$p/sp1" "$p/files" "$p/edt" "$p/manx" "$p/stock"
need() { [ -e "$1" ] || { echo "missing: $1" >&2; exit 1; }; }
# the kernel drivers and dhcpc, sources + PC-built objects, without the
# per-machine tt/ scratch
for d in driver-dp driver-amix driver-snd driver-midi driver-nu driver-ub driver-tlk dhcpc; do
	mkdir -p "$p/sp1/$d"
	(cd "$sp1/$d" && find . -maxdepth 1 -type f ! -name '.built.c' ! -name '.src.c' ! -name '*.o' ! -name 'ubuser*' ! -name 'nuuser*' ! -name 'core' -print | cpio -pdm "$p/sp1/$d" 2>/dev/null)
done
for d in nu ub snd midi tlk; do need "$sp1/driver-$d/$d.o"; cp "$sp1/driver-$d/$d.o" "$p/sp1/driver-$d/$d.pc.o"; done
need "$sp1/driver-ub/ubtool"; cp "$sp1/driver-ub/ubtool" "$p/sp1/driver-ub/ubtool"
need "$sp1/dhcpc/dhcpc"; cp "$sp1/dhcpc/dhcpc" "$p/sp1/dhcpc/dhcpc"
# apkg's client side
mkdir -p "$p/sp1/amix"; cp "$sp1/amix/apkg-setup.sh" "$sp1/amix/amixify.c" "$sp1/amix/apkg-pkgadd" "$sp1/amix/apkg-pkgrm" "$p/sp1/amix/"
# the loader probes and the stage scripts.  Two device tables: .atw has the
# Riebl/Lance probe moved off the ATW800/2's video memory (the image's
# default, safe everywhere), .la the stock probe address (sp1hw installs
# it on a TT without the card, so a Riebl card is found)
cp "$REL/edt/EDTP.dp" "$REL/edt/EDTP.nu" "$REL/edt/EDTP.tlk" "$p/edt/"
sed 's/0xfec0fff0/0x0080fff0/; s/0xfec10000/0x00810000/' "$REL/edt/edt_data.new" > "$p/edt/edt_data.atw"
sed 's/0x0080fff0/0xfec0fff0/; s/0x00810000/0xfec10000/' "$REL/edt/edt_data.new" > "$p/edt/edt_data.la"
grep -q 0xfec0fff0 "$p/edt/edt_data.la" || { echo "edt_data.la: no LA row" >&2; exit 1; }
grep -q 0x0080fff0 "$p/edt/edt_data.atw" || { echo "edt_data.atw: LA row not moved" >&2; exit 1; }
grep -q '^TLK' "$p/edt/edt_data.atw" || { echo "edt_data: no TLK row" >&2; exit 1; }
cp "$here/stage1.sh" "$here/stage2.sh" "$here/cleanup.sh" "$here/vtoc-2g.dat" "$here/manx-profile" "$p/"
# what the stock image had, to put back
python3 -I "$sp1/tools/ufs.py" "$STOCK" 102400 get /etc/rc2.d/S75rpc "$p/S75rpc.stock"
python3 -I "$sp1/tools/ufs.py" "$STOCK" 102400 get /etc/inet/hosts.net "$p/stock/hosts.net"
# the clock driver with the chip's year counted from 1968, as TOS counts
# it (tools/ttntp/README.md): the stock one reads a TT's clock two years
# ahead.  Small, so it rides with the probes in rel/edt (phase 1).
python3 -I "$sp1/tools/ufs.py" "$STOCK" 102400 get /boot/CLOCK "$p/stock/CLOCK"
python3 -I "$sp1/tools/ttntp/patch-clock.py" "$p/stock/CLOCK" "$p/edt/CLOCK.1968" >/dev/null
# the same for the mini kernel the loader boots to relink unix (it is
# never relinked itself, so its clock driver is the stock one: a relink
# on the real TT dated /stand/unix two years ahead)
python3 -I "$here/bfs.py" "$STOCK" 122880000 get mUNIX "$p/stock/mUNIX" >/dev/null
python3 -I "$sp1/tools/ttntp/patch-clock.py" "$p/stock/mUNIX" "$p/edt/mUNIX.1968" >/dev/null
# the kernel objects with the page bookkeeping in TT-RAM and the probe
# up to 256 MB (kernel/patch-ttram.py): the image's kernel, since Atari
# System V does not boot without TT-RAM anyway; the stock ones stay in
# /stand as a fallback
python3 -I "$sp1/tools/ufs.py" "$STOCK" 102400 get /boot/KERNEL "$p/stock/KERNEL"
python3 -I "$sp1/tools/ufs.py" "$STOCK" 102400 get /boot/PLATFORM "$p/stock/PLATFORM"
python3 -I "$sp1/kernel/patch-ttram.py" "$p/stock/KERNEL" "$p/edt/KERNEL.256" "$p/stock/PLATFORM" "$p/edt/PLATFORM.256" 256 >/dev/null
# the stock kernel itself, for the new disk's /stand (the stock image's
# /stand has no room for a second kernel)
python3 -I "$here/bfs.py" "$STOCK" 122880000 get unix "$p/edt/unix.stock" >/dev/null
# what the first boot finds out, and the pieces it switches on
cp "$here/sp1hw" "$here/swapfile.init" "$here/xdm.sh" "$p/files/"
need "$REL/tprobe"; cp "$REL/tprobe" "$p/files/tprobe"
need "$REL/xtrees.cpio"; cp "$REL/xtrees.cpio" "$p/files/xtrees.cpio"
cp "$sp1/tools/ttntp/S99ttntp" "$p/files/S99ttntp"
# the userland
f=$p/files
need "$REL/amix/amx.tar"; cp "$REL/amix/amx.tar" "$f/amx.tar"
cp "$REL/dl/apkg/APKGENG-1.12.3.pkg" "$f/APKGENG.pkg"; cp "$REL/dl/apkg/APKG-0.2.3.pkg" "$f/APKG.pkg"
need "$sp1/amix/bash/work/bash"; cp "$sp1/amix/bash/work/bash" "$f/bash"
need "$sp1/amix/vim/work/vim-9.2.1189-amix.tar.gz"; cp "$sp1/amix/vim/work/vim-9.2.1189-amix.tar.gz" "$f/vim.tar.gz"
need "$sp1/amix/nano/work/nano-9.2-amix.tar"; cp "$sp1/amix/nano/work/nano-9.2-amix.tar" "$f/nano.tar"
need "$sp1/amix/mtools/work/mtools-4.0.49-asv.tar"; cp "$sp1/amix/mtools/work/mtools-4.0.49-asv.tar" "$f/mtools.tar"
need "$sp1/rsync/work/rsync"; cp "$sp1/rsync/work/rsync" "$f/rsync"
need "$sp1/tools/ttntp/ttntp"; cp "$sp1/tools/ttntp/ttntp" "$f/ttntp"
need "$REL/dl/cacert/cacert.pem"; cp "$REL/dl/cacert/cacert.pem" "$f/cacert.pem"
for b in manx xmanx manxmail manxtrust ufetch t425wipe; do need "$MANX/build/sysv4/$b"; cp "$MANX/build/sysv4/$b" "$p/manx/"; done
cp "$MANX/tools/install.sh" "$p/manx/install.sh"
[ -f "$MANX/tools/S04t425wipe" ] && cp "$MANX/tools/S04t425wipe" "$p/manx/" || true
# names under 100 characters, as ustar wants; ASV extracts it into /, so
# the top directory is rel
(cd "$out" && find rel | awk 'length > 99 { print "long name: " $0; bad=1 } END { exit bad }')
(cd "$out" && rm -f payload.tar && tar --format=ustar --owner=root --group=root -cf payload.tar rel && rm -rf rel)
ls -l "$out/payload.tar"
