# install.sh - build the amx module (runs AMIX binaries; see ../amix/README.md) into the kernel.  Run as root:  sh install.sh
# Then reboot (init 6). Re-running is a no-op if amx.c is unchanged.
cd "`dirname $0`" || exit 1
if cmp -s amx.c .built.c 2>/dev/null; then echo "kernel already carries this amx.c"; exit 0; fi
echo "== compiling amx.c"
cc -O -D_KERNEL -c amx.c || { echo "COMPILE FAILED"; exit 1; }
cp master.d-amx /etc/master.d/amx
echo "== mkboot"
/usr/sbin/mkboot -m /etc/master.d -d /boot amx.o || { echo "MKBOOT FAILED"; exit 1; }
grep "INCLUDE:AMX" /stand/system >/dev/null || echo "INCLUDE:AMX" >> /stand/system
echo "== buildsys"
/sbin/buildsys || { echo "BUILDSYS FAILED"; exit 1; }
cp amx.c .built.c
echo "== kernel rebuilt; reboot when ready"
