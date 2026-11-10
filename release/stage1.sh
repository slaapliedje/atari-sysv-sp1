# stage1.sh - the first half of the release build, run by the rc hook on
# the throw-away copy of the stock image (release/build.py puts it in
# /etc/rc2.d/S75rpc there).  Payload disk = SCSI 2, the new 2 GiB disk =
# SCSI 1, the log (/rel/log, the hook's redirect) is copied to SCSI 3 by
# flush, where the PC reads it.
#
# Two boots.  cunix builds the kernel from the device table the loader
# found at boot (the "incore EDT"), and the boot's own auto-configure
# (ckmunix) runs buildsys as soon as the loader finds a device the
# kernel was not built for - and fails, into a shell, when /boot has no
# driver for it.  So:
#   phase 1 (the stock boot): every driver's objects into /boot and its
#           INCLUDE line, the module loader and the modules, the device
#           nodes, dhcpc, the device tables into /stand, and a reboot;
#   phase 2 (the next boot: ckmunix's buildsys links the drivers and
#           the module loader in): the new disk, and the system copied
#           onto it.
# The cards' drivers - dp (DaynaPORT), nu (NetUSBee Ethernet), tlk (the
# ATW800/2's links) - are loadable modules, not in the kernel and not in
# the loader's device table: they load when their card answers, so the
# image's first boot on a TT never relinks the kernel for a card.  snd,
# midi, amx and ub are modules too: the kernel keeps an empty ub driver,
# whose rows the ub module fills (see driver-ub/ub.c, UB_MODULE).
PATH=/sbin:/usr/sbin:/usr/bin:/usr/ccs/bin:/etc; export PATH
# init starts the rc scripts with umask 0: without this every file made
# here (the driver objects in /boot among them) would be writable by any user
umask 022
flush() { sync; cat /rel/log > /dev/dsk/c3d0sf; sync; }
die() { echo "STAGE1 FAILED: $*"; flush; exit 1; }
if [ ! -f /rel/phase1.done ]; then
	echo "== stage 1, phase 1 `date`"
	cd / || die cd
	echo "== the payload (the stock root has ~15 MB free: only what stage 1 needs)"
	tar xf /dev/rdsk/c2d0sf rel/sp1 rel/edt rel/stock rel/S75rpc.stock rel/vtoc-2g.dat rel/stage2.sh rel/cleanup.sh rel/manx-profile || die "payload tar"
	ls /rel; df -t /
	flush
	echo "== the clock driver: the chip's year from 1968, as TOS counts it"
	cp /boot/CLOCK /stand/CLOCK.pre-1968 || die "CLOCK aside"
	cp /rel/edt/CLOCK.1968 /boot/CLOCK || die "CLOCK.1968"
	ls -l /boot/CLOCK /stand/CLOCK.pre-1968
	echo "== the kernel's page bookkeeping in TT-RAM, the probe up to 256 MB"
	# (the stock objects go to the new disk's /stand in phase 2: the
	# stock image's /stand is nearly full)
	cp /rel/edt/KERNEL.256 /boot/KERNEL || die "KERNEL.256"
	cp /rel/edt/PLATFORM.256 /boot/PLATFORM || die "PLATFORM.256"
	ls -l /boot/KERNEL /boot/PLATFORM
	echo "== driver objects into /boot (buildsys itself waits for the probes)"
	mv /sbin/buildsys /sbin/buildsys.real || die "buildsys aside"
	echo "exit 0" > /sbin/buildsys; chmod 755 /sbin/buildsys
	# ub's installer as it is (device nodes, ubtool, /etc/init.d/ub,
	# /etc/default/ub), with an empty object for the driver: the ub
	# module loads into its rows
	echo "-- driver-ub (the empty driver for the ub module)"; flush
	cp /rel/sp1/dlm/spare.o /rel/sp1/driver-ub/ub.pc.o || die "ub stub"
	sh /rel/sp1/driver-ub/install.sh || { mv /sbin/buildsys.real /sbin/buildsys; die "driver-ub install"; }
	mv /sbin/buildsys.real /sbin/buildsys || die "buildsys back"
	echo "-- the module loader (/boot/DLM, the spare rows DLMA..DLMH, mkksym in buildsys)"; flush
	DLM_NOBUILD=1 sh /rel/sp1/dlm/install.sh || die "dlm install"
	echo "-- the modules: dp, nu, tlk, snd, midi, amx, ub"
	for m in dp nu tlk snd midi amx ub; do
		cp /rel/sp1/mod.d/$m /etc/conf/mod.d/$m && chmod 644 /etc/conf/mod.d/$m || die "module $m"
	done
	cp /rel/sp1/mod.d/mod_register /etc/conf/mod_register && chmod 644 /etc/conf/mod_register || die "mod_register"
	cp /rel/sp1/mod.d/mod_load /etc/conf/mod_load && chmod 644 /etc/conf/mod_load || die "mod_load"
	cp /rel/sp1/dlm/dlm.init /etc/init.d/dlm && chmod 744 /etc/init.d/dlm || die "dlm.init"
	rm -f /etc/rc2.d/S02dlm; ln /etc/init.d/dlm /etc/rc2.d/S02dlm || die "S02dlm"
	ls -l /etc/conf/mod.d; cat /etc/conf/mod_register
	echo "-- the cards' device nodes"
	# /dev/en0 and /dev/nu0: the clone device (63) for dp's 41 and nu's 45
	rm -f /dev/en0 /dev/nu0
	mknod /dev/en0 c 63 41 && chmod 644 /dev/en0 || die "mknod en0"
	mknod /dev/nu0 c 63 45 && chmod 600 /dev/nu0 || die "mknod nu0"
	# the links: root and group tlk only (after a reset a transputer
	# answers peeks with no program running: whoever opens its link reads
	# what was left in its memory)
	grep '^tlk:' /etc/group > /dev/null || /usr/sbin/groupadd -g 110 tlk || die "group tlk"
	rm -f /dev/link0 /dev/link1
	mknod /dev/link0 c 42 0 && mknod /dev/link1 c 42 1 || die "mknod links"
	chgrp tlk /dev/link0 /dev/link1 && chmod 660 /dev/link0 /dev/link1 || die "link modes"
	# sound and MIDI: anyone may play
	rm -f /dev/audio /dev/midi /dev/midit
	mknod /dev/audio c 43 0 && mknod /dev/midi c 44 0 && mknod /dev/midit c 44 1 || die "mknod audio midi"
	chmod 666 /dev/audio /dev/midi /dev/midit || die "audio midi modes"
	ls -l /dev/en0 /dev/nu0 /dev/link0 /dev/link1 /dev/audio /dev/midi /dev/midit
	ls -l /boot/DLM /boot/DLMA /boot/UB
	grep INCLUDE /stand/system
	flush
	echo "== dhcpc (the DaynaPORT, /dev/en0)"
	sh /rel/sp1/dhcpc/install.sh /dev/en0 || die "dhcpc install"
	# the build's own address while dhcpc has not run (slirp's guest)
	sed 's/^192.58.149.33/192.168.30.20/' /etc/inet/hosts.net > /tmp/hosts.net && cp /tmp/hosts.net /etc/inet/hosts.net
	echo "== the device tables into /stand"
	cp /rel/edt/edt_data.atw /rel/edt/edt_data.la /stand/ || die "edt_data variants"
	cp /rel/edt/edt_data.atw /stand/edt_data || die edt_data
	ls -l /stand
	touch /rel/phase1.done
	echo "== phase 1 done: rebooting `date`"
	flush
	sync; sync; sync
	/sbin/uadmin 2 1
	exit 0
fi
echo "== stage 1, phase 2 `date`"
ls -l /stand/unix /stand/mUNIX
flush
# the kernel ckmunix just built (/stand/system is newer than it) must
# carry the loader and the drivers, and none of the modules' drivers;
# buildsys by hand if the boot did not relink
nm /stand/unix | grep dlminit > /dev/null || { echo "-- buildsys (the boot did not relink)"; /sbin/buildsys || die buildsys; }
nm /stand/unix | grep dlminit > /dev/null || die "the kernel has no dlminit"
grep '^INCLUDE:UB$' /stand/system > /dev/null || die "no INCLUDE:UB (the ub module's rows)"
for n in dpstart nustart tlkinit sndinit midiinit amxinit ubinit; do
	nm /stand/unix | grep "$n" > /dev/null && die "the kernel carries $n (a module's driver)"
done
/sbin/mkksym -c /stand/unix || die "the kernel's symbol table does not check"
echo "kernel carries dlm and ub's empty rows; dp nu tlk snd midi amx ub are modules"
echo "== the new disk"; flush
partinit -I -i -b -Z -P 0:1:FILL:UNX:UNIXBOOT -o /dev/rdsk/c1d0sf || die partinit
format -w /dev/rdsk/c1d0sf || die format
setvtoc -s /rel/vtoc-2g.dat /dev/rdsk/c1d0sf || die setvtoc
prtvtoc /dev/rdsk/c1d0sf
echo "-- file systems"; flush
mkfs -F bfs /dev/rdsk/c1d0s3 24000 128 || die "mkfs stand"
mkfs -F ufs -o nsect=32,ntrack=128,bsize=8192,fragsize=1024 /dev/rdsk/c1d0s1 1572864 || die "mkfs root"
mkfs -F ufs -o nsect=32,ntrack=128,bsize=8192,fragsize=1024 /dev/rdsk/c1d0s5 262144 || die "mkfs var"
mkfs -F ufs -o nsect=32,ntrack=128,bsize=8192,fragsize=1024 /dev/rdsk/c1d0s6 2052948 || die "mkfs home"
echo "-- copying / `date`"; flush
mount -F ufs /dev/dsk/c1d0s1 /mnt || die "mount root"
(cd / && find . -mount -print | cpio -pdmu /mnt) || die "cpio root"
# find -mount leaves the mount points themselves out: the directories
# the other file systems hang on, as the stock root has them
for d in home:755 mnt:775 proc:555 stand:755 var:755 dev/fd:555; do
	n=`echo $d | sed 's/:.*//'`; m=`echo $d | sed 's/.*://'`
	mkdir /mnt/$n && chmod $m /mnt/$n && chown root /mnt/$n && chgrp sys /mnt/$n || die "mkdir $n"
done
# the new disk gets the stock rc script, not the build's hook
cp /rel/S75rpc.stock /mnt/etc/rc2.d/S75rpc || die "S75rpc restore"
umount /mnt || die "umount root"
echo "-- copying /var `date`"; flush
mount -F ufs /dev/dsk/c1d0s5 /mnt || die "mount var"
(cd /var && find . -mount -print | cpio -pdmu /mnt) || die "cpio var"
umount /mnt || die "umount var"
echo "-- copying /home `date`"; flush
mount -F ufs /dev/dsk/c1d0s6 /mnt || die "mount home"
(cd /home && find . -mount -print | cpio -pdmu /mnt) || die "cpio home"
umount /mnt || die "umount home"
echo "-- copying /stand `date`"; flush
mount -F bfs /dev/dsk/c1d0s3 /mnt || die "mount stand"
# mkfs makes the BFS root 777, where any user could replace the kernel:
# the stock /stand is 775 root sys
chmod 775 /mnt && chgrp sys /mnt || die "stand mode"
cp /stand/* /mnt || die "cp stand"
# the stock kernel and objects, bootable from the loader if ever needed
cp /rel/edt/unix.stock /mnt/unix.stock || die "unix.stock"
cp /rel/stock/KERNEL /mnt/KERNEL.stock || die "KERNEL.stock"
cp /rel/stock/PLATFORM /mnt/PLATFORM.stock || die "PLATFORM.stock"
# the mini kernel with the clock driver's 1968 patch (see CLOCK above)
# and the page bookkeeping in TT-RAM, as the kernel objects: the stock
# one panics at the relink on a TT with more than ~128 MB
cp /rel/edt/mUNIX.1968 /mnt/mUNIX || die "mUNIX.1968"
ls -l /mnt
umount /mnt || die "umount stand"
echo "-- boot slices"
dd if=/dev/rdsk/c0d0s0 of=/dev/rdsk/c1d0s0 bs=512 count=196 || die "dd s0"
dd if=/dev/rdsk/c0d0se of=/dev/rdsk/c1d0se bs=512 count=20000 || die "dd se"
sync; sync
echo "== stage 1 done `date`"
echo "STAGE1 DONE"
flush
exit 0
