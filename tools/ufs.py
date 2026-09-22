#!/usr/bin/env python3
"""Minimal read-only big-endian SVR4 UFS (BSD FFS) reader for the ASV image.
usage: ufs.py IMG BASE ls PATH | cat PATH | get PATH OUT | put PATH LOCALFILE (in-place overwrite)
BASE = byte offset of the filesystem start (primary superblock - 8192)."""
import struct, sys, mmap

class UFS:
	def __init__(s, img, base, rw=False):
		f = open(img, 'r+b' if rw else 'rb'); s.m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_WRITE if rw else mmap.ACCESS_READ); s.base = base
		sb = s.m[base+8192:base+8192+1400]
		g = lambda o: struct.unpack('>i', sb[o:o+4])[0]
		assert g(0x55c) == 0x011954, 'bad magic'
		s.iblkno, s.cgoffset, s.cgmask = g(16), g(24), g(28)
		s.bsize, s.fsize, s.frag, s.nindir, s.inopb = g(48), g(52), g(56), g(116), g(120)
		s.ipg, s.fpg = g(184), g(188)
	def fb(s, frag, n): o = s.base + frag*s.fsize; return s.m[o:o+n]
	def inode(s, ino):
		c = ino // s.ipg; start = s.fpg*c + s.cgoffset*(c & ~s.cgmask)
		frag = start + s.iblkno + ((ino % s.ipg)//s.inopb)*s.frag
		d = s.fb(frag, s.bsize)[(ino % s.ipg % s.inopb)*128:][:128]
		mode = struct.unpack('>H', d[0:2])[0]; size = struct.unpack('>I', d[12:16])[0]
		return mode, size, struct.unpack('>12i', d[40:88]), struct.unpack('>3i', d[88:100]), d
	def blocks(s, db, ib, nblk):
		out = list(db)
		def ind(fr, lvl):
			if fr == 0: return [0]*(s.nindir**lvl)
			p = struct.unpack('>%di' % s.nindir, s.fb(fr, s.bsize)); r = []
			for x in p: r += [x] if lvl == 1 else ind(x, lvl-1)
			return r
		for lvl, fr in enumerate(ib, 1):
			if len(out) >= nblk: break
			out += ind(fr, lvl)
		return out[:nblk]
	def read(s, ino):
		mode, size, db, ib, raw = s.inode(ino)
		n = (size + s.bsize - 1)//s.bsize; out = bytearray()
		for b in s.blocks(db, ib, n): out += s.fb(b, s.bsize) if b else bytes(s.bsize)
		return bytes(out[:size])
	def overwrite(s, ino, data):
		"""Replace an existing file's CONTENTS in place: same inode, same
		blocks, same size (data is zero-padded). Touches no metadata."""
		mode, size, db, ib, raw = s.inode(ino)
		assert (mode & 0xf000) == 0x8000 and len(data) <= size, 'too big for %d' % size
		data = data + bytes(size - len(data)); n = (size + s.bsize - 1)//s.bsize
		bl = s.blocks(db, ib, n); assert all(bl), 'file is sparse - pick another'
		for i, b in enumerate(bl):
			chunk = data[i*s.bsize:(i+1)*s.bsize]; o = s.base + b*s.fsize
			s.m[o:o+len(chunk)] = chunk
		s.m.flush()
	def dir(s, ino):
		d = s.read(ino); o = 0; r = {}
		while o + 8 <= len(d):
			i, rl, nl = struct.unpack('>IHH', d[o:o+8])
			if rl == 0: break
			if i: r[d[o+8:o+8+nl].decode('latin1')] = i
			o += rl
		return r
	def lookup(s, path):
		ino = 2
		for p in [x for x in path.split('/') if x]:
			ino = s.dir(ino)[p]
			mode = s.inode(ino)[0]
			if (mode & 0xf000) == 0xa000:
				t = s.read(ino).decode('latin1'); ino = s.lookup(t if t[0] == '/' else path[:path.rfind(p)] + t)
		return ino

if __name__ == '__main__':
	cmd = sys.argv[3]; u = UFS(sys.argv[1], int(sys.argv[2], 0), rw=(cmd == 'put'))
	if cmd == 'ls':
		for n, i in sorted(u.dir(u.lookup(sys.argv[4])).items()):
			mode, size, db, ib, raw = u.inode(i)
			extra = ''
			if (mode & 0xf000) in (0x2000, 0x6000): extra = ' dev=%d,%d' % (raw[40+2], raw[40+3]) + ' raw=' + raw[40:44].hex()
			print('%06o %9d %s%s' % (mode, size, n, extra))
	elif cmd == 'cat': sys.stdout.buffer.write(u.read(u.lookup(sys.argv[4])))
	elif cmd == 'get': open(sys.argv[5], 'wb').write(u.read(u.lookup(sys.argv[4])))
	elif cmd == 'put': u.overwrite(u.lookup(sys.argv[4]), open(sys.argv[5], 'rb').read())
