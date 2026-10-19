# install.sh - build the ub driver (USB: the ISP1160 on the Lightning VME,
# the NetUSBee next) into the kernel, make /dev/ub0 and the disk devices,
# and install ubtool with its rc script (ub.init: `ubtool daemon` at
# boot, /etc/rc2.d/S75ub).  Run as root:
#   sh install.sh
# Then reboot, or `/etc/init.d/ub start` if only the daemon changed.  The
# object comes built on the PC (../kbuild.sh driver-ub/ub.c, GCC 2.7.2
# -O2) as ub.pc.o.  The kernel is rebuilt only if what goes into it
# changed; the rest is redone every time (cheap, and idempotent).
cd "`dirname $0`" || exit 1
[ -f ub.pc.o ] || { echo "no ub.pc.o: build it on the PC"; exit 1; }
[ -c /dev/ub0 ] || /sbin/mknod /dev/ub0 c 46 0
chmod 600 /dev/ub0
# the storage units: minor 16 + unit, block and raw
for n in 0 1 2 3; do
	[ -b /dev/dsk/usb$n ] || /sbin/mknod /dev/dsk/usb$n b 46 `expr 16 + $n`
	[ -c /dev/rdsk/usb$n ] || /sbin/mknod /dev/rdsk/usb$n c 46 `expr 16 + $n`
	chmod 600 /dev/dsk/usb$n /dev/rdsk/usb$n
done
if [ -f ubtool ]; then
	cp ubtool /usr/bin/ubtool && chmod 755 /usr/bin/ubtool
fi
cp ub.init /etc/init.d/ub && chmod 744 /etc/init.d/ub
rm -f /etc/rc2.d/S75ub /etc/rc0.d/K75ub
ln /etc/init.d/ub /etc/rc2.d/S75ub
ln /etc/init.d/ub /etc/rc0.d/K75ub
cat ub.c ub.h ub_io.c usb.c hub.c ucd.c udd.c isp116x.c isp116x.h ubstor.c ubstor.h ubhid.c ubhid.h ubioc.h master.d-ub ub.pc.o > .src.c
if cmp -s .src.c .built.c 2>/dev/null; then echo "kernel already carries this ub; the daemon's rc script is in"; exit 0; fi
cp ub.pc.o ub.o
cp master.d-ub /etc/master.d/ub
echo "== mkboot"
/usr/sbin/mkboot -m /etc/master.d -d /boot ub.o || { echo "MKBOOT FAILED"; exit 1; }
grep "INCLUDE:UB" /stand/system >/dev/null || echo "INCLUDE:UB" >> /stand/system
echo "== buildsys"
/sbin/buildsys || { echo "BUILDSYS FAILED"; exit 1; }
mv .src.c .built.c
echo "== kernel rebuilt; reboot when ready"
