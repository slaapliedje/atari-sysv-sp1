# nuup.sh ADDR [NETMASK] - bring nu0 up by hand: link it under IP (once
# per boot) and give it an address.  Run as root.
#   sh nuup.sh 192.168.3.111 255.255.255.0
cd "`dirname $0`" || exit 1
[ -n "$1" ] || { echo "usage: nuup.sh ADDR [NETMASK]"; exit 1; }
if /usr/sbin/ifconfig nu0 >/dev/null 2>&1; then :; else
	/usr/sbin/slink -c strcf.nu nuup || { echo "slink failed"; exit 1; }
fi
/usr/sbin/ifconfig nu0 $1 netmask ${2:-255.255.255.0} up -trailers
/usr/sbin/ifconfig nu0
