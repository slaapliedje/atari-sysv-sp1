#!/usr/bin/env python3
"""mkedtp.py - loader probe programs for DP, NU and TLK, and the EDT rows.

Atari System V's loader runs /stand/EDTP.NAME for every NAME in
/stand/edt_data before the kernel, and hands the kernel the devices
found; /usr/sbin/prtconf (rc2 S20sysetup) prints them as the boot's
hardware page, and cunix refuses a hardware driver (master.d flag h)
without a row.  The stock probes are two templates with the name
patched in: EDTP.hd (scsiprobe: a table of names to SCSI device types,
found when such a device answers INQUIRY) and EDTP.cen (found unless
reading the row's IO_ADDR bus-errors).  So:

  EDTP.dp   from EDTP.hd, the HD entry renamed DP with type 3 (processor,
            the DaynaPORT): found only with a DaynaPORT on the bus
  EDTP.nu   from EDTP.cen at 0xfa0000: the cartridge port never
            bus-errors, so always found (the driver probes the chip itself)
  EDTP.tlk  from EDTP.cen at 0xfedffac0: found only with the ATW800/2

  python3 mkedtp.py DIR     DIR holds EDTP.hd, EDTP.cen and edt_data
                            copied from the TT's /stand; writes EDTP.dp,
                            EDTP.nu, EDTP.tlk and edt_data.new there

The templates are Atari's and are not in this repository.  Install: the
three EDTP files into /stand (new files: BFS grows only at its end),
edt_data.new over /stand/edt_data (same 3 blocks), then reboot: the
kernel sees hardware it was not built for and auto-configures at boot,
which fails there (no /opt, where /usr/ccs/bin lives on this TT) - in
its shell: mount /opt; mount /work; /sbin/buildsys; sync; exit; reset.
Or run buildsys yourself after that first reboot.
"""
import sys, os

def patch(data, at, old, new):
    assert data[at:at + len(old)] == old, (hex(at), data[at:at + len(old)])
    data[at:at + len(new)] = new

def main(d):
    hd = bytearray(open(os.path.join(d, 'EDTP.hd'), 'rb').read())
    cen = bytearray(open(os.path.join(d, 'EDTP.cen'), 'rb').read())
    dp = bytearray(hd)
    patch(dp, 1432, b'HD\0', b'DP\0')           # its own name
    patch(dp, 1443, b'HD\0', b'DP\0')           # the table's entry ...
    patch(dp, 1460, b'\0\0\x87\xa3\0\0\0\0', b'\0\0\x87\xa3\0\0\0\3')  # ... type 3
    patch(dp, 2085, b'hd\0', b'dp\0')           # the source name (cosmetic)
    open(os.path.join(d, 'EDTP.dp'), 'wb').write(dp)
    for up, lo in ((b'NU\0\0', b'nu\0\0'), (b'TLK\0', b'tlk\0')):
        b = bytearray(cen)
        patch(b, 1680, b'CEN\0', up)
        patch(b, 2353, b'cen\0', lo)
        open(os.path.join(d, 'EDTP.' + lo.rstrip(b'\0').decode()), 'wb').write(b)
    ed = open(os.path.join(d, 'edt_data')).read()
    rows = ''
    for name, addr in (('NU', '0x00fa0000'), ('DP', '0'), ('TLK', '0xfedffac0')):
        if not any(l.split()[:1] == [name] for l in ed.splitlines()):
            rows += "%-9s -     0       %-15s -       -       -       -       -       6       3       - - - -\n" % (name, addr)
    open(os.path.join(d, 'edt_data.new'), 'w').write(ed + rows)
    print('written EDTP.dp EDTP.nu EDTP.tlk edt_data.new' + (' (rows added)' if rows else ' (rows were there)'))

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
