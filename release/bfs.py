#!/usr/bin/env python3
"""bfs.py IMG BASE ls | get NAME OUT | put NAME FILE | add NAME FILE [MODE]
ASV's /stand (SVR4 BFS, big-endian) read and written from the PC.
BASE = byte offset of the slice.  put overwrites a file within the blocks
it has; add appends a new file after the last one (BFS files are
contiguous and new ones only go at the end)."""
import struct, sys, time
img, base, cmd = sys.argv[1], int(sys.argv[2], 0), sys.argv[3]
f = open(img, 'r+b' if cmd in ('put', 'add') else 'rb')
f.seek(base); sb = f.read(512)
magic, start, end = struct.unpack('>III', sb[:12])
assert magic == 0x1BADFACE, 'bad magic %x' % magic
NREC = (start - 512) // 62
# 62-byte records from byte 512: ushort ino, then longs sblock, eblock,
# eoffset (byte offset of the last byte), vtype (1 file, 2 dir), mode,
# uid, gid, nlink, atime, mtime, ctime, pad[4]
FMT = '>HIIIIIIIIIII16s'
def rec(i):
    f.seek(base + 512 + i * 62); return list(struct.unpack(FMT, f.read(62)))
def wrec(i, r):
    f.seek(base + 512 + i * 62); f.write(struct.pack(FMT, *r))
def data(r):
    f.seek(base + r[1] * 512); return f.read(r[3] - r[1] * 512 + 1)
recs = {}
for i in range(NREC):
    r = rec(i)
    if r[0]: recs[r[0]] = (i, r)
rooti, root = recs[2]
rd = data(root); names = {}; slots = {}
for o in range(0, len(rd), 16):
    ino = struct.unpack('>H', rd[o:o+2])[0]
    if ino: names[rd[o+2:o+16].split(b'\0')[0].decode('latin1')] = ino
    else: slots.setdefault('free', o)
if cmd == 'ls':
    for n, ino in sorted(names.items()):
        i, r = recs[ino]; print('%8d  %06o  blocks %d-%d  %s' % (r[3] - r[1]*512 + 1, r[5], r[1], r[2], n))
    used = max(r[2] for i, r in recs.values()); print('last block %d of %d' % (used, (end + 1) // 512 - 1))
elif cmd == 'get':
    open(sys.argv[5], 'wb').write(data(recs[names[sys.argv[4]]][1])); print('got', sys.argv[4])
elif cmd == 'put':
    i, r = recs[names[sys.argv[4]]]; d = open(sys.argv[5], 'rb').read()
    room = (r[2] - r[1] + 1) * 512
    assert len(d) <= room, 'no room: %d > %d' % (len(d), room)
    f.seek(base + r[1] * 512); f.write(d + b'\0' * (room - len(d)))
    r[3] = r[1] * 512 + len(d) - 1; r[10] = r[11] = int(time.time()); wrec(i, r); print('put', sys.argv[4], len(d))
elif cmd == 'add':
    name, d = sys.argv[4], open(sys.argv[5], 'rb').read(); mode = int(sys.argv[6], 8) if len(sys.argv) > 6 else 0o444
    assert name not in names and len(name) <= 14
    free = [i for i in range(NREC) if rec(i)[0] == 0]; assert free, 'inode table full'
    i = free[0]; ino = i + 2
    sblk = max(r[2] for j, r in recs.values()) + 1; n = (len(d) + 511) // 512
    assert (sblk + n) * 512 - 1 <= end, 'no room on the slice'
    f.seek(base + sblk * 512); f.write(d + b'\0' * (n * 512 - len(d)))
    t = int(time.time())
    wrec(i, [ino, sblk, sblk + n - 1, sblk * 512 + len(d) - 1, 1, mode, 0, 3, 1, t, t, t, b'\0' * 16])
    # the directory entry: a free slot, else the entry after the last one
    o = slots.get('free', len(rd)); dirend = (root[2] - root[1] + 1) * 512
    assert o + 16 <= dirend, 'root directory full'
    f.seek(base + root[1] * 512 + o); f.write(struct.pack('>H', ino) + name.encode().ljust(14, b'\0'))
    if o + 16 > len(rd):
        root[3] = root[1] * 512 + o + 16 - 1; wrec(rooti, root)
    print('added', name, 'ino', ino, 'blocks %d-%d' % (sblk, sblk + n - 1))
f.close()
