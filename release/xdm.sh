#!/sbin/sh
# /usr/local/lib/sp1/xdm - the release's xd line in /etc/inittab (sp1
# release).  init reads its entries for the run level before rc2 runs,
# so a line changed by sp1hw would only count from the next boot on;
# this picks the display manager when init starts it instead:
# X11R6.3's xdm on the ATW800/2's Xatw server when the boot found the
# card (/etc/sp1/atw800), the stock X11R4 one otherwise.
#
# The hard memory limits, 16 MB of data and of address space by default,
# raised for the sessions xdm starts: a user's program may then raise
# its own soft limits (Manx does; Xatw, root's, raises its hard ones).
ulimit -Hd 131072
ulimit -Hv 131072
if [ "`cat /etc/sp1/atw800 2>/dev/null`" = yes -a -x /usr/x11r6/bin/xdm ]; then
	exec /usr/x11r6/bin/xdm -nodaemon -config /usr/x11r6/lib/X11/xdm/xdm-config > /usr/x11r6/lib/X11/xdm/xdm-init.out 2>&1 < /dev/null
fi
exec /usr/bin/X11/xdm -nodaemon > /usr/lib/X11/xdm/xdm-init.out 2>&1 < /dev/null
