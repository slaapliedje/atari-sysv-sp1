# install.sh - build the tlk driver (ATW800/2 transputer links) into the
# kernel and make /dev/link0 (C011) and /dev/link1 (FPGA).  Run as root:
#   sh install.sh
# Then reboot (init 6). Re-running is a no-op if tlk.c and tlk.h are unchanged.
cd "`dirname $0`" || exit 1
[ -c /dev/link0 ] || /sbin/mknod /dev/link0 c 42 0
[ -c /dev/link1 ] || /sbin/mknod /dev/link1 c 42 1
chmod 666 /dev/link0 /dev/link1
cat tlk.c tlk.h > .src.c
if cmp -s .src.c .built.c 2>/dev/null; then echo "kernel already carries this tlk.c"; exit 0; fi
echo "== compiling tlk.c"
cc -O -D_KERNEL -c tlk.c || { echo "COMPILE FAILED"; exit 1; }
cp master.d-tlk /etc/master.d/tlk
echo "== mkboot"
/usr/sbin/mkboot -m /etc/master.d -d /boot tlk.o || { echo "MKBOOT FAILED"; exit 1; }
grep "INCLUDE:TLK" /stand/system >/dev/null || echo "INCLUDE:TLK" >> /stand/system
echo "== buildsys"
/sbin/buildsys || { echo "BUILDSYS FAILED"; exit 1; }
mv .src.c .built.c
echo "== kernel rebuilt; reboot when ready"
