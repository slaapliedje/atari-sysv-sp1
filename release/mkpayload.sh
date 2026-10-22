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
for d in driver-dp driver-amix driver-snd driver-midi driver-nu driver-ub dhcpc; do
	mkdir -p "$p/sp1/$d"
	(cd "$sp1/$d" && find . -maxdepth 1 -type f ! -name '.built.c' ! -name '.src.c' ! -name '*.o' ! -name 'ubuser*' ! -name 'nuuser*' ! -name 'core' -print | cpio -pdm "$p/sp1/$d" 2>/dev/null)
done
for d in nu ub snd midi; do need "$sp1/driver-$d/$d.o"; cp "$sp1/driver-$d/$d.o" "$p/sp1/driver-$d/$d.pc.o"; done
need "$sp1/driver-ub/ubtool"; cp "$sp1/driver-ub/ubtool" "$p/sp1/driver-ub/ubtool"
need "$sp1/dhcpc/dhcpc"; cp "$sp1/dhcpc/dhcpc" "$p/sp1/dhcpc/dhcpc"
# apkg's client side
mkdir -p "$p/sp1/amix"; cp "$sp1/amix/apkg-setup.sh" "$sp1/amix/amixify.c" "$sp1/amix/apkg-pkgadd" "$sp1/amix/apkg-pkgrm" "$p/sp1/amix/"
# the loader probes and the stage scripts
cp "$REL/edt/EDTP.dp" "$REL/edt/EDTP.nu" "$REL/edt/edt_data.rel" "$p/edt/"
cp "$here/stage1.sh" "$here/stage2.sh" "$here/cleanup.sh" "$here/vtoc-2g.dat" "$here/manx-profile" "$p/"
# what the stock image had, to put back
python3 -I "$sp1/tools/ufs.py" "$STOCK" 102400 get /etc/rc2.d/S75rpc "$p/S75rpc.stock"
python3 -I "$sp1/tools/ufs.py" "$STOCK" 102400 get /etc/inet/hosts.net "$p/stock/hosts.net"
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
