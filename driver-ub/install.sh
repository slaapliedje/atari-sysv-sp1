# install.sh - build the ub driver (USB: the ISP1160 on the Lightning VME,
# the NetUSBee next) into the kernel and make /dev/ub0.  Run as root:
#   sh install.sh
# Then reboot.  The object comes built on the PC (../kbuild.sh
# driver-ub/ub.c, GCC 2.7.2 -O2) as ub.pc.o.  Re-running is a no-op if
# nothing changed.
cd "`dirname $0`" || exit 1
[ -f ub.pc.o ] || { echo "no ub.pc.o: build it on the PC"; exit 1; }
cat ub.c ub.h ub_io.c usb.c hub.c ucd.c udd.c isp116x.c isp116x.h ubstor.c ubstor.h ubioc.h master.d-ub install.sh ub.pc.o > .src.c
if cmp -s .src.c .built.c 2>/dev/null; then echo "kernel already carries this ub"; exit 0; fi
cp ub.pc.o ub.o
cp master.d-ub /etc/master.d/ub
echo "== mkboot"
/usr/sbin/mkboot -m /etc/master.d -d /boot ub.o || { echo "MKBOOT FAILED"; exit 1; }
grep "INCLUDE:UB" /stand/system >/dev/null || echo "INCLUDE:UB" >> /stand/system
[ -c /dev/ub0 ] || /sbin/mknod /dev/ub0 c 46 0
chmod 600 /dev/ub0
# the storage units: minor 16 + unit, block and raw
for n in 0 1 2 3; do
	[ -b /dev/dsk/usb$n ] || /sbin/mknod /dev/dsk/usb$n b 46 `expr 16 + $n`
	[ -c /dev/rdsk/usb$n ] || /sbin/mknod /dev/rdsk/usb$n c 46 `expr 16 + $n`
	chmod 600 /dev/dsk/usb$n /dev/rdsk/usb$n
done
[ -f ubtool ] && cp ubtool /usr/bin/ubtool && chmod 755 /usr/bin/ubtool
echo "== buildsys"
/sbin/buildsys || { echo "BUILDSYS FAILED"; exit 1; }
mv .src.c .built.c
echo "== kernel rebuilt; reboot when ready"
