# dhcpc - DHCP for Atari System V

ASV's TCP/IP (1989) has no DHCP client: the address comes from the node's
line in `/etc/inet/hosts.net`, which `/etc/inet/rc.inet` feeds to
`ifconfig en0 \`uname -n\``.  `dhcpc` gets it from the network instead.

It talks DLPI to the Ethernet driver (`/dev/en0`: the DaynaPORT, sp1
driver-dp; `/dev/nu0`: the NetUSBee, driver-nu) and does the
DISCOVER / OFFER / REQUEST / ACK exchange itself, on raw IP datagrams.
Because the drivers allow one stream per SAP, it must run while IP is not
linked under the device: at boot before `S69inet`, which is how it is
installed.

    dhcpc [-n | -w] [-t SECS] [-v] /dev/en0
      -n  print the lease (default)
      -w  write it: the node's line in /etc/inet/hosts.net (aliases kept),
          /etc/inet/dhcp.sh (netmask, broadcast, default route), and
          /etc/resolv.conf (domain, nameservers)

Install (as root, on the TT, from this directory):

    sh install.sh /dev/en0      # or /dev/nu0; "off" turns it off again

This puts `dhcpc` in `/usr/local/bin`, the rc script in `/etc/init.d/dhcpc`
with links `S68dhcpc` (asks, writes the files) and `S71dhcpc` (runs
`dhcp.sh` after `rc.inet`), and the device name in `/etc/inet/dhcpc.dev`.
Without that file the boot is as before.  With no answer in 40 s the
static address in `hosts.net` stays.

The client sends the node name (option 12), so the router's DHCP table
shows it.  Leases are not renewed: home routers hand the same address
back at the next boot, and the TT is rebooted more often than a lease
expires.

Built with the TT's `cc -O -o dhcpc dhcpc.c` or the sysv4 cross compiler.
Tested in Hatari (hatari-net, slirp's DHCP server) and on the TT.
