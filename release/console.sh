#!/sbin/sh
# /usr/local/lib/sp1/console - init's cx line, run level 4 (sp1 release).
# The stock co line gives the TT's own screen a login in run levels 1-3
# only: in 4, X11R4's xdm has that screen. When the boot found the
# ATW800/2, xdm runs X on the card instead (/usr/local/lib/sp1/xdm), and
# this screen gets its console login after all; Ctrl+Alt+F10 on the card's
# X screen lends it the keyboard (Xatw; atwkbd gives it back). Without the
# card this only waits, so init has nothing to restart.
umask 022
if [ "`cat /etc/sp1/atw800 2>/dev/null`" = yes -a -x /usr/x11r6/bin/xdm ]; then
	echo "X is on the ATW800/2. Ctrl+Alt+F10 there brings the keyboard to this screen." > /dev/console
	exec /usr/lib/saf/ttymon -g -h -p "Console login: " -m gls,ldterm,ttcompat -d /dev/console -l 9600
fi
while :; do
	sleep 32767
done
