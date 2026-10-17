# install.sh - dhcpc on ASV: the client, its rc script and links, and the
# device file that turns it on.  Run as root in this directory:
#   sh install.sh [/dev/en0 | /dev/nu0 | off]
# Default /dev/en0 (the DaynaPORT).  "off" keeps the files but removes
# /etc/inet/dhcpc.dev: the static address is used again.
cd "`dirname $0`" || exit 1
DEV=${1:-/dev/en0}
if [ ! -f dhcpc ]; then
	cc -O -o dhcpc dhcpc.c || { echo "dhcpc: build failed"; exit 1; }
fi
[ -d /usr/local/bin ] || mkdir -p /usr/local/bin
cp dhcpc /usr/local/bin/dhcpc && chmod 755 /usr/local/bin/dhcpc
cp dhcpc.init /etc/init.d/dhcpc && chmod 744 /etc/init.d/dhcpc
rm -f /etc/rc2.d/S68dhcpc /etc/rc2.d/S71dhcpc
ln /etc/init.d/dhcpc /etc/rc2.d/S68dhcpc
ln /etc/init.d/dhcpc /etc/rc2.d/S71dhcpc
if [ "$DEV" = off ]; then
	rm -f /etc/inet/dhcpc.dev
	echo "dhcpc installed, off: the static address in /etc/inet/hosts.net is used"
else
	echo "$DEV" > /etc/inet/dhcpc.dev
	echo "dhcpc installed: at boot $DEV gets its address from DHCP"
fi
