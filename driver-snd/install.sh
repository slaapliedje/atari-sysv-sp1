# install.sh - build the snd driver (the TT's DMA sound as /dev/audio)
# into the kernel.  Run as root:
#   sh install.sh
# Then reboot (init 6). Re-running is a no-op if snd.c, snd.h and this
# script are unchanged.
#
# No -O: ASV's cc ignores volatile when optimising and reads a polled
# register ONCE per loop (see ../driver-tlk/install.sh).
cd "`dirname $0`" || exit 1
[ -c /dev/audio ] || /sbin/mknod /dev/audio c 43 0
chmod 666 /dev/audio
cat snd.c snd.h install.sh > .src.c
if cmp -s .src.c .built.c 2>/dev/null; then echo "kernel already carries this snd.c"; exit 0; fi
echo "== compiling snd.c"
cc -D_KERNEL -c snd.c || { echo "COMPILE FAILED"; exit 1; }
cp master.d-snd /etc/master.d/snd
echo "== mkboot"
/usr/sbin/mkboot -m /etc/master.d -d /boot snd.o || { echo "MKBOOT FAILED"; exit 1; }
grep "INCLUDE:SND" /stand/system >/dev/null || echo "INCLUDE:SND" >> /stand/system
echo "== buildsys"
/sbin/buildsys || { echo "BUILDSYS FAILED"; exit 1; }
mv .src.c .built.c
echo "== kernel rebuilt; reboot when ready"
