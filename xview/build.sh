#!/bin/sh
# build.sh - cross-build XView 3.2p1.4 (the OPEN LOOK toolkit, olwm, cmdtool,
# clock) for Atari System V, on top of the X11R6.3 build.
#
#   sh build.sh [XWORK] [WORKDIR]
#
# XWORK is the work directory of ../xserver-r6/build.sh (default
# ../xserver-r6/work): its xc tree supplies imake, the ASV imake config, the
# X headers and shared libraries, and the per-program objects; its sysroot
# the fixed headers. Needs curl and python3 besides that build's tools.
set -e
HERE=`cd "\`dirname "$0"\`" && pwd`
XWORK=`cd "${1:-$HERE/../xserver-r6/work}" && pwd`
WORK=${2:-$HERE/work}
XC=$XWORK/xc
A=$XC/config/asv
SRC_URL=https://www.ibiblio.org/pub/Linux/libs/X/xview/xview-3.2p1.4.src.tar.gz
SRC_SHA256=fcc88f884a6cb05789ed800edea24d9c4cf1f60cb7d61f3ce7f10de677ef9e8d
CLIENTS="clock olwm olwmslave cmdtool"

[ -x $XC/config/imake/imake ] || { echo "build ../xserver-r6 first ($XC)" >&2; exit 1; }
mkdir -p "$WORK"
WORK=`cd "$WORK" && pwd`
cd "$WORK"
[ -f xview-3.2p1.4.src.tar.gz ] || curl -fsSLO $SRC_URL
echo "$SRC_SHA256  xview-3.2p1.4.src.tar.gz" | sha256sum -c - >/dev/null
if [ ! -d xview-3.2p1.4 ]; then
	tar xzf xview-3.2p1.4.src.tar.gz
	(cd xview-3.2p1.4 && patch -p1 < "$HERE/patches/xview-3.2p1.4-asv.patch")
	mkdir -p xview-3.2p1.4/asv/include
	cp -R "$HERE/asv/include/sys" xview-3.2p1.4/asv/include/
fi
XV=$WORK/xview-3.2p1.4
cd $XV

# imake: the X tree's, in "installed" mode with the ASV config, and every
# generated Makefile gets the settings below appended (as XView's own Linux
# build script does)
mkdir -p asvbin
cat > asvbin/imake <<EOF
#!/bin/sh
$XC/config/imake/imake -DASV -DUseInstalled -I$XV/config -I$XC/config/cf "\$@" || exit 1
[ -f Makefile ] && cat $XV/imake.append >> Makefile
exit 0
EOF
chmod +x asvbin/imake
cat > imake.append <<EOF
  XVDESTDIR      = /usr/openwin
  OPENWINHOME    = /usr/openwin
  EXTRA_DEFINES  = -DOPENWINHOME_DEFAULT=\"/usr/openwin\" -Denviron=_environ
  IMAKE          = $XV/asvbin/imake
  MKDIRHIER      = mkdir -p
  INCLUDES      ?=
  INCLUDES      := -I$XV/build/include -I$XV/asv/include -I$XC/exports/include \$(INCLUDES)
  LOCAL_LDFLAGS ?=
  LOCAL_LDFLAGS := -L$XV/lib/libxview -L$XV/lib/libolgx -L$XC/exports/lib -rpath-link=$XC/exports/lib \$(LOCAL_LDFLAGS)
  USRLIBDIR      = $XC/exports/lib
  SYSV_CLIENT_LIB =
  EXTRA_LIBRARIES = -lsocket -lsockhost -lnsl $A/libcextra.o $A/asvcompat.o $A/asviob.o
  EXTRA_LOAD_FLAGS = -rpath=/usr/openwin/lib -rpath=/usr/x11r6/lib -rpath-link=$XC/exports/lib -rpath-link=$XV/lib/libxview -rpath-link=$XV/lib/libolgx
EOF
PATH=$XV/asvbin:$PATH; export PATH

(cd config && imake) > imake.log 2>&1
imake >> imake.log 2>&1
make Makefiles > Makefiles.log 2>&1
make includes > includes.log 2>&1
for d in lib/libolgx lib/libxview; do
	(cd $d && make) > $d/build.log 2>&1 || { echo "build failed in $d, see $d/build.log"; exit 1; }
done
# the compiler wrapper resolves -lxview to lib<name>.so only
(cd lib/libxview && ln -sf libxview.so.3 libxview.so)
(cd lib/libolgx && ln -sf libolgx.so.3 libolgx.so)
(cd clients && imake -DTOPDIR=.. -DCURDIR=./clients && make Makefiles) > clients/Makefiles.log 2>&1
for c in $CLIENTS; do
	(cd clients/$c && make) > clients/$c/build.log 2>&1 || { echo "build failed: $c"; exit 1; }
	python3 "$HERE/../xserver/fixneeded.py" clients/$c/$c > /dev/null
done

D=$WORK/dist/usr/openwin
rm -rf $WORK/dist; mkdir -p $D/bin $D/lib
cp lib/libxview/libxview.so.3 lib/libolgx/libolgx.so.3 $D/lib/
for c in $CLIENTS; do cp clients/$c/$c $D/bin/; done
# olwm's Workspace menu (Shell Tool, Clock, xterm, Refresh, Exit)
cp "$HERE/openwin-menu" $D/lib/
tar --format=v7 --owner=0 --group=0 -C $WORK/dist -cf $WORK/xview-asv.tar usr
ls -l $D/bin $D/lib $WORK/xview-asv.tar
