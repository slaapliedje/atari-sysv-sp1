# cleanup.sh - the last thing run on the release disk before it is halted
# and imaged: nothing of the build session stays behind.
PATH=/sbin:/usr/sbin:/usr/bin:/etc; export PATH
echo "== cleanup `date`"
# the network files as the stock image has them; dhcpc rewrites the
# address, the name servers and the route at every boot
cp /rel/stock/hosts.net /etc/inet/hosts.net
rm -f /etc/inet/dhcp.sh /etc/resolv.conf /etc/defaultrouter
# apkg asks a name server itself (AMIX has no resolver): a public one,
# not the build's
if [ -f /etc/apkg.conf ]; then
	grep -v "^nameserver=" /etc/apkg.conf > /tmp/apkg.conf
	echo "nameserver=1.1.1.1" >> /tmp/apkg.conf
	cp /tmp/apkg.conf /etc/apkg.conf
fi
# the build's leftovers (/.manx: stage 2 ran manx once; the packages
# apkg downloaded, installed or not)
rm -rf /rel /var/tmp/* /tmp/* /var/apkg/tmp/* /.manx 2>/dev/null
rm -f /etc/sp1/boot /etc/sp1/transputers
rm -f /.sh_history /.history /core /var/apkg/cache/*.pkg
# logs and accounting: of the build session, and of the stock image's
# own history before it (the files stay, empty: the daemons expect them)
for f in /var/adm/wtmp /var/adm/wtmpx /var/adm/utmp /var/adm/utmpx /var/adm/log/console.log /var/adm/sulog /var/cron/log /var/adm/pacct /var/adm/log/osm /var/adm/syslog.log /var/saf/_log /var/saf/ttymon1/log /var/saf/ttymon2/log /var/lp/logs/lpNet /var/lp/logs/lpsched /var/lp/logs/requests /var/spool/smtpq/LOG /var/mail/uucp; do
	[ -f $f ] && cp /dev/null $f
done
# (syslogd wants its files in /var/adm/log to exist: emptied, not removed)
for f in /var/adm/log/*; do
	[ -f $f ] && cp /dev/null $f
done
rm -f /var/adm/sa/* /var/spool/rwho/whod.* 2>/dev/null
rm -f /usr/x11r6/lib/X11/xdm/xdm-init.out /var/mail/root 2>/dev/null
echo "CLEANUP DONE"
sync; sync; sync
