# install.sh - build the nu driver (the NetUSBee's Ethernet as nu0) into
# the kernel.  Run as root:
#   sh install.sh
# Then reboot (init 6). Re-running is a no-op if the sources are unchanged.
#
# The object comes built on the PC (../kbuild.sh driver-nu/nu.c, GCC 2.7.2
# -O2), copied here as nu.pc.o: the TT's own cc (GCC 1.40) drops volatile
# loads when optimising, and the card is driven by loads.
#
# /dev/nu0 is the clone device (major 63) for major 45. Nothing is linked
# at boot yet: nuup.sh links nu0 under IP by hand. (A failed line in
# strcf would make inetinit unlink every interface, en0 too.)
cd "`dirname $0`" || exit 1
[ -f nu.pc.o ] || { echo "no nu.pc.o: build it on the PC"; exit 1; }
cat nu.c nuchip.c nu.h nureg.h nustat.h master.d-nu install.sh nu.pc.o > .src.c
if cmp -s .src.c .built.c 2>/dev/null; then echo "kernel already carries this nu.c"; exit 0; fi
cp nu.pc.o nu.o
cp master.d-nu /etc/master.d/nu
echo "== mkboot"
/usr/sbin/mkboot -m /etc/master.d -d /boot nu.o || { echo "MKBOOT FAILED"; exit 1; }
grep "INCLUDE:NU" /stand/system >/dev/null || echo "INCLUDE:NU" >> /stand/system
[ -c /dev/nu0 ] || /sbin/mknod /dev/nu0 c 63 45
chmod 600 /dev/nu0
echo "== buildsys"
/sbin/buildsys || { echo "BUILDSYS FAILED"; exit 1; }
mv .src.c .built.c
echo "== kernel rebuilt; reboot when ready"
