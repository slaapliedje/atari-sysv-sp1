# install.sh - build the dp driver into the kernel.  Run as root:  sh install.sh
# Then reboot (init 6). Re-running is a no-op if dp.c is unchanged.
cd "$(dirname "$0")" || exit 1
if cmp -s dp.c .built.c 2>/dev/null; then echo "kernel already carries this dp.c"; exit 0; fi
echo "== compiling dp.c"
cc -O -D_KERNEL -c dp.c || { echo "COMPILE FAILED"; exit 1; }
cp master.d-dp /etc/master.d/dp
echo "== mkboot"
/usr/sbin/mkboot -m /etc/master.d -d /boot dp.o || { echo "MKBOOT FAILED"; exit 1; }
grep "INCLUDE:DP" /stand/system >/dev/null || echo "INCLUDE:DP" >> /stand/system
echo "== buildsys"
/sbin/buildsys || { echo "BUILDSYS FAILED"; exit 1; }
cp dp.c .built.c
echo "== kernel rebuilt; reboot when ready"
