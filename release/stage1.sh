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
#   phase 1 (the stock boot, no new probes yet): every driver's objects
#           into /boot and its INCLUDE line, the device nodes, dhcpc,
#           then the loader probes into /stand, and a reboot;
#   phase 2 (the next boot: the loader finds DP and NU, ckmunix's
#           buildsys links the drivers in): the new disk, and the system
#           copied onto it.
PATH=/sbin:/usr/sbin:/usr/bin:/usr/ccs/bin:/etc; export PATH
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
	for d in driver-dp driver-amix driver-snd driver-midi driver-nu driver-ub driver-tlk; do
		echo "-- $d"; flush
		sh /rel/sp1/$d/install.sh || { mv /sbin/buildsys.real /sbin/buildsys; die "$d install"; }
	done
	mv /sbin/buildsys.real /sbin/buildsys || die "buildsys back"
	# DP and NU are hardware drivers: cunix takes them from the loader's
	# device table (the EDT) when the probes find the cards, and an
	# INCLUDE line for a card that is not there stops the boot's
	# auto-configure ("INCLUDE of hardware driver not in the EDT").
	# The installers write the line for a machine that has the card;
	# this image must boot on any TT.
	grep -v "^INCLUDE:DP\$" /stand/system | grep -v "^INCLUDE:NU\$" | grep -v "^INCLUDE:TLK\$" > /tmp/system && cp /tmp/system /stand/system || die "system file"
	rm -f /dev/en0
	mknod /dev/en0 c 63 41 || die "mknod en0"
	chmod 644 /dev/en0
	ls -l /boot/DP /boot/NU /boot/UB /boot/AMX /boot/SND /boot/MIDI /boot/TLK
	grep INCLUDE /stand/system
	flush
	echo "== dhcpc (the DaynaPORT, /dev/en0)"
	sh /rel/sp1/dhcpc/install.sh /dev/en0 || die "dhcpc install"
	# the build's own address while dhcpc has not run (slirp's guest)
	sed 's/^192.58.149.33/192.168.30.20/' /etc/inet/hosts.net > /tmp/hosts.net && cp /tmp/hosts.net /etc/inet/hosts.net
	echo "== loader probes (EDT) into /stand"
	cp /rel/edt/EDTP.dp /rel/edt/EDTP.nu /rel/edt/EDTP.tlk /stand/ || die "EDTP files"
	chmod 555 /stand/EDTP.dp /stand/EDTP.nu /stand/EDTP.tlk
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
# the kernel ckmunix just built must carry the drivers: their names are
# in its symbol table
for n in dpstart amxinit nustart ubinit sndinit midiinit; do
	nm /stand/unix | grep "$n" > /dev/null || die "the kernel has no $n"
done
echo "kernel carries dp nu ub amx snd midi"
# tlk is a hardware driver too: in the kernel only where the loader's
# probe finds the ATW800/2's link (not in this emulator)
nm /stand/unix | grep tlkinit > /dev/null && echo "and tlk" || echo "(no tlk: no ATW800/2 here, as expected)"
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
