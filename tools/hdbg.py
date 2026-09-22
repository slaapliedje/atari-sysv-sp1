#!/usr/bin/env python3
"""Drive a running Hatari's debugger through its command fifo, and read the
ASV kernel's memory through the 68030 page tables.

    hdbg.py FIFO OUTFILE 'debugger command' ...

Start Hatari with `--cmd-fifo FIFO > OUTFILE 2>&1`. Every command is sent
as `hatari-debug <cmd>` with a timeout (a write to a fifo nobody reads
blocks forever), then the new output is printed.

As a module: send(cmd) -> output text, mem(addr, n) -> physical bytes
(the debugger's `m` reads physical memory; count is DECIMAL), regs() ->
dict of the `r` dump, xlate(va) / vmem(va, n) -> supervisor logical
addresses through the tables the SRP points at (TC = 82c07760: 7/7/6-bit
indices, 4 KB pages, short descriptors, as ASV sets it up).
"""
import os, re, subprocess, sys, time

FIFO, OUT = (sys.argv[1], sys.argv[2]) if len(sys.argv) > 2 else (None, None)

def send(cmd, wait=1.5):
    before = os.path.getsize(OUT)
    p = subprocess.run(['timeout', '5', 'sh', '-c',
                        "echo 'hatari-debug %s' > %s" % (cmd, FIFO)])
    if p.returncode:
        raise SystemExit('fifo write timed out (is Hatari running?)')
    time.sleep(wait)
    with open(OUT, 'rb') as f:
        f.seek(before)
        return f.read().decode('latin1')

def mem(addr, n):
    txt = send('m $%x %d' % (addr, n))
    data = bytearray()
    for line in txt.splitlines():
        m = re.match(r'^([0-9A-F]{8}): ((?:[0-9a-f]{2} ?)+)', line)
        if m:
            data += bytes.fromhex(m.group(2).replace(' ', ''))
    return bytes(data[:n])

def regs():
    txt = send('r')
    d = dict(re.findall(r'\b([A-Z]+[0-9]?)\s+([0-9A-F]{8})\b', txt))
    for k in ('SRP', 'CRP'):
        m = re.search(k + r': ([0-9A-F]+)', txt)
        if m:
            d[k] = m.group(1)
    return d

def root():
    return int(regs()['SRP'][-8:], 16) & 0xfffffff0

def xlate(va, table=None):
    table = table or root()
    for bits, shift in ((7, 25), (7, 18), (6, 12)):
        idx = (va >> shift) & ((1 << bits) - 1)
        d = int.from_bytes(mem(table + 4 * idx, 4), 'big')
        dt = d & 3
        if dt == 0:
            raise ValueError('invalid descriptor for %08x' % va)
        if dt == 1:                       # page, or early termination
            return (d & 0xffffff00) + (va & ((1 << shift) - 1))
        if dt == 3:
            raise ValueError('long descriptor at %08x' % va)
        table = d & 0xfffffff0
    raise ValueError('walk ran off the end for %08x' % va)

def vmem(va, n):
    out, t = bytearray(), root()
    while n:
        pa = xlate(va, t)
        k = min(n, 0x1000 - (pa & 0xfff))
        out += mem(pa, k); va += k; n -= k
    return bytes(out)

if __name__ == '__main__':
    for c in sys.argv[3:]:
        print(send(c))
