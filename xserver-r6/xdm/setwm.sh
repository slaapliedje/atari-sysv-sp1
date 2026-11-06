#!/sbin/sh
# setwm - the window manager X sessions start (sp1 release)
#   setwm            the ones installed, and which one you get
#   setwm NAME       yours, from the next login on (~/.wm)
#   setwm -d NAME    everyone's default, for users with no ~/.wm (root)
#   setwm -u         forget yours: the default again
# The names come from /etc/X11/wms. The chooser on the login screen sets
# ~/.wm too.
L=/etc/X11/wms
D=/etc/X11/wm
installed() {
	grep -v '^#' $L | while read n p rest; do
		test -n "$n" -a -x "$p" && echo "$n"
	done
}
known() {
	installed | grep "^$1\$" > /dev/null
}
W=`echo "$HOME/.wm" | sed 's|//*|/|g'`
mine=`sed -n 1p $W 2>/dev/null`
dflt=`sed -n 1p $D 2>/dev/null`
case "$1" in
"")
	echo "installed: "`installed`
	echo "default:   ${dflt:-olvwm}  ($D)"
	if test -n "$mine"; then
		echo "yours:     $mine  ($W)"
	else
		echo "yours:     the default"
	fi
	;;
-d)
	known "$2" || { echo "setwm: no window manager \"$2\" (setwm lists them)" >&2; exit 1; }
	echo "$2" > $D.new && mv $D.new $D || exit 1
	echo "the default from the next login on: $2"
	;;
-u)
	rm -f $W
	echo "yours from the next login on: the default (${dflt:-olvwm})"
	;;
-*)
	echo "usage: setwm [NAME | -d NAME | -u]" >&2
	exit 2
	;;
*)
	known "$1" || { echo "setwm: no window manager \"$1\" (setwm lists them)" >&2; exit 1; }
	echo "$1" > $W || exit 1
	echo "yours from the next login on: $1"
	;;
esac
