#
# strcf.nu - link the NetUSBee (nu0) under the running IP, by hand:
#   slink -c strcf.nu nuup
# The functions are the stock strcf's.
#
linkint {
	x = link $1 $2
	sifname $1 x $3
}

aplinkint {
	push $2 app
	linkint $1 $2 $3
}

cenet {
	devname = strcat $2 $4
	ifname = strcat $3 $4
	dev = open devname
	aplinkint $1 dev ifname
	dev = open devname
	arp = open /dev/arp
	linkint arp dev ifname
}

nuup {
	ip = open /dev/ip
	cenet ip /dev/nu nu 0
}
