#!/bin/sh
# xatwrun - run one X program on a second ATW800/2 X server of its own,
# for a game that wants a low resolution full screen:
#
#   xatwrun [-mode WxH] [-vt N] program [args...]
#
# The desktop's server must run with -vt 1 (xdm's Xservers). This starts
# Xatw at -mode (default 640x480) as Ctrl+Alt+F<N> (default 2), in video
# memory past the desktop's screen, runs the program on it, and when the
# program exits stops the server - which hands the card back to the
# desktop. Ctrl+Alt+F1 and F<N> switch between the two meanwhile.
#
# Run it from an X session: the new server gets that session's cookie.
# (Without one it would refuse even local clients: X cannot list ASV's
# own addresses, so no host counts as local - the reason xdm hands out
# cookies too.)
MODE=640x480
VT=2
while [ $# -gt 0 ]; do
	case "$1" in
	-mode)	MODE=$2; shift 2 ;;
	-vt)	VT=$2; shift 2 ;;
	*)	break ;;
	esac
done
[ $# -gt 0 ] || { echo "usage: xatwrun [-mode WxH] [-vt N] program [args...]" >&2; exit 2; }
XB=/usr/x11r6/bin
D=`uname -n`:`expr $VT - 1`
KEY=`$XB/xauth list 2> /dev/null | awk '$2 == "MIT-MAGIC-COOKIE-1" { print $3; exit }'`
if [ -z "$KEY" ]; then
	echo "xatwrun: no X cookie to lend the new server; run it from an X session" >&2
	exit 1
fi
# The new screen goes past the desktop's and the 64 bytes of its 2D-engine
# fill source, at a 64 KB boundary: 832 KB in under an 8-bit 1024x768
# desktop, 3 MB + 64 KB under a 32-bit one. The desktop publishes its place
# as _ATW_FRAMEBUFFER (offset, bytes per line, width, height are fields 3
# to 6); the system's R4 xprop takes no atom starting with "_", so all the
# root's properties are listed (asking the desktop, so before XAUTHORITY
# changes). Without it, 1 MB in.
OFF=`/usr/bin/X11/xprop -root 2> /dev/null |
	sed -n 's/^_ATW_FRAMEBUFFER(INTEGER) = //p' |
	awk -F, '{ e = $3 + $4 * $6 + 64; printf "%d\n", int((e + 65535) / 65536) * 65536 }'`
[ -n "$OFF" ] || OFF=1048576
XAUTHORITY=/tmp/.xatwrun.$VT.auth
export XAUTHORITY
rm -f $XAUTHORITY
$XB/xauth add $D MIT-MAGIC-COOKIE-1 $KEY 2> /dev/null
chmod 600 $XAUTHORITY
$XB/Xatw :`expr $VT - 1` -mode $MODE -vt $VT -fboffset $OFF -auth $XAUTHORITY \
	> /tmp/xatwrun.$VT.log 2>&1 &
XPID=$!
# the server takes a few seconds on a TT: wait until it answers
n=0
until $XB/xset -display $D q > /dev/null 2>&1; do
	n=`expr $n + 1`
	kill -0 $XPID 2> /dev/null || n=99	# (ASV's sh has no "!")
	if [ $n -gt 60 ]; then
		echo "xatwrun: the server did not start, see /tmp/xatwrun.$VT.log" >&2
		kill $XPID 2> /dev/null
		exit 1
	fi
	sleep 1
done
DISPLAY=$D "$@"
rc=$?
kill $XPID
wait $XPID 2> /dev/null
rm -f $XAUTHORITY
exit $rc
