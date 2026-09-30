#!/bin/sh
# apkg-setup.sh - install apkg, the AMIX package client, on Atari System V.
# Run as root on the TT, in a directory holding amixify.c, apkg-pkgadd,
# apkg-pkgrm and the two packages from pkg.amigaux.org (see README.md):
#
#   sh apkg-setup.sh APKGENG.pkg APKG.pkg [NAMESERVER]
#
# Needs the amx module and /usr/amx (README.md, steps 1 and 2). It builds
# amixify, installs the apkg-pkgadd and apkg-pkgrm hooks, installs both
# packages through the hook (ASV's pkgadd installs the engine, the engine
# installs apkg), and points /etc/apkg.conf at the hooks and at NAMESERVER
# (default: the first one in /etc/resolv.conf). Unpacked packages go in
# $TMPDIR (default /var/tmp). Running it again is harmless: installed
# packages are left alone.
eng=$1 apkg=$2 ns=$3
if [ -z "$eng" ] || [ -z "$apkg" ]
then
	echo "usage: sh apkg-setup.sh APKGENG.pkg APKG.pkg [NAMESERVER]"
	exit 2
fi
here=`pwd`
case $eng in /*) ;; *) eng=$here/$eng ;; esac
case $apkg in /*) ;; *) apkg=$here/$apkg ;; esac
[ -f /usr/amx/libc.so.1 ] || { echo "no /usr/amx/libc.so.1: install /usr/amx first"; exit 1; }

echo "== amixify, apkg-pkgadd, apkg-pkgrm -> /usr/amx/bin"
[ -d /usr/amx/bin ] || mkdir /usr/amx/bin || exit 1	# ASV's mkdir -p fails on an existing directory
cc -O -o /usr/amx/bin/amixify amixify.c || exit 1
cp apkg-pkgadd apkg-pkgrm /usr/amx/bin/ || exit 1
chmod 755 /usr/amx/bin/amixify /usr/amx/bin/apkg-pkgadd /usr/amx/bin/apkg-pkgrm

# pkgadd's admin file: no questions, as apkg's own
admin=/var/tmp/apkg-setup.admin
cat > $admin <<EOF
mail=
instance=quit
partial=nocheck
runlevel=nocheck
idepend=nocheck
rdepend=nocheck
space=quit
setuid=nocheck
conflict=nocheck
action=nocheck
basedir=default
EOF

# ASV's pkgadd installs the engine; the engine's own pkgadd then does the rest
for p in APKGENG:$eng APKG:$apkg
do
	if [ -x /usr/apkg/bin/pkgadd ]
	then
		APKG_ENGINE=/usr/apkg/bin/pkgadd
	else
		APKG_ENGINE=/usr/sbin/pkgadd
	fi
	export APKG_ENGINE
	name=`echo $p | sed 's/:.*//'`
	file=`echo $p | sed 's/^[^:]*://'`
	if [ -d /var/sadm/pkg/$name ]
	then
		echo "== $name is already installed"
		continue
	fi
	echo "== $name"
	/usr/amx/bin/apkg-pkgadd -n -a $admin -d $file $name || { rm -f $admin; exit 1; }
done
rm -f $admin

echo "== /etc/apkg.conf"
[ -n "$ns" ] || ns=`sed -n 's/^nameserver[ 	]*//p' /etc/resolv.conf 2>/dev/null | sed -n 1p`
conf=/etc/apkg.conf
for k in pkgadd pkgrm
do
	grep "^$k=/usr/amx/bin/apkg-$k\$" $conf >/dev/null ||
		echo "$k=/usr/amx/bin/apkg-$k" >> $conf
done
if [ -n "$ns" ]
then
	grep "^nameserver=$ns\$" $conf >/dev/null || echo "nameserver=$ns" >> $conf
else
	echo "no nameserver: add pkg.amigaux.org to /etc/inet/hosts, or nameserver= to $conf"
fi
grep '^pkgadd=' $conf
grep '^pkgrm=' $conf
grep '^nameserver=' $conf
echo "== done: apkg update; apkg list; apkg install NAME (programs land in /opt/amix/bin)"
