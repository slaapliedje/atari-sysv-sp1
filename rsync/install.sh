# install.sh PC_ADDRESS - set the TT up as a backup source for
# ../tools/asvbackup.sh. Run as root on the TT, in a directory holding this
# script, rsync (from build.sh), rsyncd.conf and, the first time, a file
# `password' with the backup password (the same as the PC's
# tools/.rsyncpass). Re-running is safe; without `password' it keeps the
# existing /etc/rsyncd.secrets (to update the binary or the address).
#
# - /usr/local/bin/rsync, started by inetd on port 873 (nothing runs
#   between backups)
# - /etc/rsyncd.conf: one read-only module, `root' (the whole system but
#   /proc), for PC_ADDRESS only, with user `backup' and the password in
#   /etc/rsyncd.secrets (mode 600)
cd "`dirname $0`" || exit 1
if [ $# -ne 1 ]; then echo "usage: sh install.sh PC_ADDRESS"; exit 1; fi
for f in rsync rsyncd.conf; do
	if [ ! -f $f ]; then echo "missing $f"; exit 1; fi
done
if [ ! -f password -a ! -f /etc/rsyncd.secrets ]; then
	echo "missing password (first install)"; exit 1
fi

[ -d /usr/local/bin ] || mkdir -p /usr/local/bin
cp rsync /usr/local/bin/rsync
chmod 755 /usr/local/bin/rsync
sed "s/PC_ADDRESS/$1/" rsyncd.conf > /etc/rsyncd.conf
chmod 644 /etc/rsyncd.conf
if [ -f password ]; then
	umask 077
	echo "backup:`cat password`" > /etc/rsyncd.secrets
	chmod 600 /etc/rsyncd.secrets
	rm -f password
fi

if grep '^rsync' /etc/services > /dev/null; then :; else
	cp /etc/services /etc/services.pre-rsync
	echo "rsync		873/tcp			# rsync daemon" >> /etc/services
fi
if grep '^rsync' /etc/inetd.conf > /dev/null; then :; else
	cp /etc/inetd.conf /etc/inetd.conf.pre-rsync
	echo "rsync	stream	tcp	nowait	root	/usr/local/bin/rsync	rsyncd --daemon" >> /etc/inetd.conf
fi
# inetd rereads its configuration on SIGHUP
pid=`ps -e | grep ' inetd$' | awk '{print $1}'`
if [ -n "$pid" ]; then kill -1 $pid; fi
echo "rsync daemon installed; from the PC: ASV_HOST=<this machine> tools/asvbackup.sh DEST"
