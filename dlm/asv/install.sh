#!/sbin/sh
# install.sh - the module loader into this system's kernel (sp1 dlm/asv).
# Run as root from this directory (the files build.sh made, as the
# payload carries them); then reboot.
#
#   /boot/DLM          the loader: dlminit() starts it from io_init
#   /boot/DLMA..DLMH   empty character drivers, externals 110-117: the
#                      spare cdevsw rows module drivers are given
#   /sbin/mkksym       fills the kernel's symbol table after a relink;
#                      buildsys runs it after cunix from now on
#   /sbin/modadmin     load, unload and list modules
#   /etc/conf/mod.d    where modules are looked for
cd "`dirname $0`" || exit 1
ulimit -f 4194303
PATH=/sbin:/usr/sbin:/usr/bin:/etc; export PATH
die() { echo "dlm install: $*"; exit 1; }
for d in dlm dlma dlmb dlmc dlmd dlme dlmf dlmg dlmh; do
	cp master.d/$d /etc/master.d/$d || die "master.d/$d"
	if [ $d = dlm ]; then cp DLM.o dlm.o; else cp spare.o $d.o; fi
	mkboot -m /etc/master.d -d /boot $d.o || die "mkboot $d"
	rm -f $d.o
	D=`echo $d | tr '[a-z]' '[A-Z]'`
	grep "^INCLUDE:$D\$" /stand/system > /dev/null || echo "INCLUDE:$D" >> /stand/system
done
cp mkksym modadmin /sbin || die "programs"
chmod 755 /sbin/mkksym /sbin/modadmin
# buildsys: fill the symbol table of every kernel cunix makes (a copy
# with the line, checked, then moved over the original)
for b in /sbin/buildsys /etc/buildsys; do
	[ -f $b ] || continue
	grep mkksym $b > /dev/null && continue
	sed '/^rval=\$?$/a\
[ $rval = 0 -a -x /sbin/mkksym ] \&\& { /sbin/mkksym $ofile || rval=1; }' $b > $b.dlm
	grep '/sbin/mkksym \$ofile' $b.dlm > /dev/null || die "$b: no rval line to add mkksym after"
	cp $b $b.pre-dlm && mv $b.dlm $b && chmod 755 $b
done
[ -d /etc/conf/mod.d ] || mkdir -p /etc/conf/mod.d
[ -d mod.d ] && cp mod.d/* /etc/conf/mod.d/
echo "== buildsys"
/sbin/buildsys || die "buildsys"
/sbin/mkksym -c /stand/unix || die "the symbol table does not check"
echo "== the loader is in /stand/unix; reboot to start it"
