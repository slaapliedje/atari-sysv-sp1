# install.sh - build the midi driver (the TT's MIDI port as /dev/midi and
# /dev/midit) into the kernel.  Run as root:
#   sh install.sh
# Then reboot (init 6). Re-running is a no-op if the sources are unchanged.
#
# The object normally comes built on the PC (../kbuild.sh, GCC 2.7.2 -O2),
# copied here as midi.pc.o; the TT's own cc (GCC 1.40) is the fallback,
# without -O: it ignores volatile when optimising.
cd "`dirname $0`" || exit 1
[ -c /dev/midi ] || /sbin/mknod /dev/midi c 44 0
[ -c /dev/midit ] || /sbin/mknod /dev/midit c 44 1
chmod 666 /dev/midi /dev/midit
cat midi.c midi.h install.sh > .src.c
[ -f midi.pc.o ] && cat midi.pc.o >> .src.c
if cmp -s .src.c .built.c 2>/dev/null; then echo "kernel already carries this midi.c"; exit 0; fi
if [ -f midi.pc.o ]; then
	echo "== using midi.pc.o, built on the PC"
	cp midi.pc.o midi.o
else
	echo "== compiling midi.c"
	cc -D_KERNEL -c midi.c || { echo "COMPILE FAILED"; exit 1; }
fi
cp master.d-midi /etc/master.d/midi
echo "== mkboot"
/usr/sbin/mkboot -m /etc/master.d -d /boot midi.o || { echo "MKBOOT FAILED"; exit 1; }
grep "INCLUDE:MIDI" /stand/system >/dev/null || echo "INCLUDE:MIDI" >> /stand/system
echo "== buildsys"
/sbin/buildsys || { echo "BUILDSYS FAILED"; exit 1; }
mv .src.c .built.c
echo "== kernel rebuilt; reboot when ready"
