# stage2.sh - the second half of the release build, run as root over
# telnet on the new disk's first boot (release/build.py): the userland.
# /rel came over from the stock copy with the root file system.
PATH=/sbin:/usr/sbin:/usr/bin:/usr/ccs/bin:/etc:/usr/local/bin; export PATH
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
echo "== rsync, ttntp (the programs only)"
cp /rel/files/rsync /usr/local/bin/rsync && chmod 755 /usr/local/bin/rsync || die rsync
cp /rel/files/ttntp /usr/local/bin/ttntp && chmod 755 /usr/local/bin/ttntp || die ttntp
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
