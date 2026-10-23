# stage2.sh - the second half of the release build, run as root over
# telnet on the new disk's first boot (release/build.py): the userland.
# /rel came over from the stock copy with the root file system.
PATH=/sbin:/usr/sbin:/usr/bin:/usr/ccs/bin:/etc:/usr/local/bin; export PATH
ulimit -f 4194303		# root's 16 MB file limit would stop the X trees
die() { echo "STAGE2 FAILED: $*"; sync; sync; exit 1; }
echo "== stage 2 `date`"
uname -a
ifconfig en0
df -t /
echo "== the rest of the payload (SCSI 2)"
(cd / && tar xf /dev/rdsk/c2d0sf rel/files rel/manx) || die "payload tar"
ls -l /rel/files
echo "== AMIX's shared libraries -> /usr/amx"
(cd /usr && tar xf /rel/files/amx.tar) || die amx.tar
ls /usr/amx
echo "== apkg"
cd /rel/sp1/amix || die "cd amix"
sh apkg-setup.sh /rel/files/APKGENG.pkg /rel/files/APKG.pkg || die apkg-setup
cat /etc/apkg.conf
apkg update || die "apkg update"
# one by one: a package that would install outside /opt/amix (patch) is
# refused by the hook, and apkg stops at the first refusal
for pk in gzip less grep make tar zlib acompat; do
	apkg install $pk || echo "apkg: $pk not installed"
done
apkg list
echo "== bash, vim, nano, mtools"
[ -d /usr/local/bin ] || mkdir -p /usr/local/bin
cp /rel/files/bash /usr/local/bin/bash && chmod 755 /usr/local/bin/bash || die bash
grep "^/usr/local/bin/bash" /etc/shells > /dev/null || echo /usr/local/bin/bash >> /etc/shells
(cd / && /opt/amix/bin/gzip -dc /rel/files/vim.tar.gz | tar xf -) || die vim
(cd / && tar xf /rel/files/nano.tar) || die nano
(cd / && tar xf /rel/files/mtools.tar) || die mtools
ls -l /usr/local/bin/bash /usr/local/bin/vim /usr/local/bin/nano /usr/local/bin/mtools
echo "== Manx"
(cd /rel/manx && X11BIN=/usr/bin/X11 sh install.sh) || die "manx install"
[ -d /usr/local/share/manx ] || mkdir -p /usr/local/share/manx
cp /rel/files/cacert.pem /usr/local/share/manx/cacert.pem || die cacert
chmod 644 /usr/local/share/manx/cacert.pem
cp /rel/manx-profile /etc/profile.manx || die "manx profile"
grep "profile.manx" /etc/profile > /dev/null || echo '[ -r /etc/profile.manx ] && . /etc/profile.manx' >> /etc/profile
echo "== rsync, ttntp (and the clock from the network at boot: S99ttntp)"
cp /rel/files/rsync /usr/local/bin/rsync && chmod 755 /usr/local/bin/rsync || die rsync
cp /rel/files/ttntp /usr/local/bin/ttntp && chmod 755 /usr/local/bin/ttntp || die ttntp
cp /rel/files/S99ttntp /etc/rc2.d/S99ttntp && chmod 744 /etc/rc2.d/S99ttntp || die S99ttntp
echo "== X11R6.3 and XView for the ATW800/2 (used when the card is there)"
(cd /usr && cpio -icdum < /rel/files/xtrees.cpio) || die "X trees"
ls /usr/x11r6/bin/Xatw /usr/x11r6/bin/xdm /usr/openwin/bin/olwm || die "X trees incomplete"
echo "== the hardware detection at boot (sp1hw), swap to match the memory"
cp /rel/files/sp1hw /etc/init.d/sp1hw && chmod 744 /etc/init.d/sp1hw || die sp1hw
rm -f /etc/rc2.d/S06sp1hw; ln /etc/init.d/sp1hw /etc/rc2.d/S06sp1hw
cp /rel/files/swapfile.init /etc/init.d/swapfile && chmod 744 /etc/init.d/swapfile || die swapfile
rm -f /etc/rc2.d/S03swapfile /etc/rc0.d/K99swapfile
ln /etc/init.d/swapfile /etc/rc2.d/S03swapfile; ln /etc/init.d/swapfile /etc/rc0.d/K99swapfile
cp /rel/files/tprobe /usr/local/bin/tprobe && chmod 755 /usr/local/bin/tprobe || die tprobe
# the display manager is picked when init starts it (init reads inittab
# before rc2 runs, so sp1hw cannot change the line for the same boot)
[ -d /usr/local/lib/sp1 ] || mkdir -p /usr/local/lib/sp1
cp /rel/files/xdm.sh /usr/local/lib/sp1/xdm && chmod 755 /usr/local/lib/sp1/xdm || die "sp1 xdm"
sed 's|^xd:4:respawn:.*|xd:4:respawn:/usr/local/lib/sp1/xdm|' /etc/inittab > /tmp/inittab && cp /tmp/inittab /etc/inittab || die inittab
grep "^xd:4:respawn:/usr/local/lib/sp1/xdm$" /etc/inittab > /dev/null || die "inittab xd line"
echo auto > /etc/inet/sp1net
# the 16 MB per-process data limit made sense with 4 MB of ST-RAM; the
# kernel now takes all the TT-RAM, with swap to match
grep "ulimit -d" /etc/profile > /dev/null || echo 'ulimit -d 262144 2>/dev/null' >> /etc/profile
echo "== NetUSBee helper"
[ -d /usr/local/lib/netusbee ] || mkdir -p /usr/local/lib/netusbee
cp /rel/sp1/driver-nu/nuup.sh /rel/sp1/driver-nu/strcf.nu /usr/local/lib/netusbee/ || die nuup
echo "== a first look"
/usr/bin/manx -v 2>&1 | head -2
/usr/local/bin/vim --version 2>&1 | head -1
/usr/local/bin/nano --version 2>&1 | head -1
/usr/local/bin/bash -c 'echo bash $BASH_VERSION'
echo "STAGE2 DONE"
sync; sync
exit 0
