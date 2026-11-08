#!/usr/bin/env python3
"""scrubpaths.py IMG [-n]: the build machine's directories out of the
programs on the card image.  xserver/fixneeded.py wrote a library's bare
name over the start of the host path in .dynstr and left the rest of it
there, until it cleared that too: the programs linked before, and the X
trees taken from the TT, still carry such paths.  In each ELF file on
the UFS slices, a .dynstr string that holds a host path and that nothing
refers to (the dynamic section, the dynamic symbols) is zeroed in place:
same size, same blocks.  One that is referred to is named, and the exit
status is 1.  -n: only say what would be cleared."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ufs import UFS
from permscan import slices

HOST = (b'/home/', b'/tmp/', b'OpenUA', b'scratchpad', b'sysroot')

def scrub(d):
	"""(new contents or None, [cleared strings], [referenced host strings]) of one ELF file"""
	if d[:4] != b'\x7fELF' or d[4] != 1 or d[5] != 2:
		return None, [], []
	shoff, = struct.unpack('>I', d[32:36]); shentsize, shnum = struct.unpack('>HH', d[46:50])
	if not shoff or not shnum:
		return None, [], []
	secs = [struct.unpack('>10I', d[shoff + i * shentsize:shoff + i * shentsize + 40]) for i in range(shnum)]
	if any(s[1] in (0x6ffffffd, 0x6ffffffe, 0x6fffffff) for s in secs):
		return None, [], [b'(symbol versions: left alone)']
	out = bytearray(d); cleared = []; badref = []
	for strndx in sorted({s[6] for s in secs if s[1] in (6, 11)}):	# SHT_DYNAMIC, SHT_DYNSYM -> sh_link
		off, size = secs[strndx][4], secs[strndx][5]
		refs = set()
		for s in secs:
			if s[6] != strndx: continue
			if s[1] == 6:
				for o in range(s[4], s[4] + s[5], 8):
					tag, val = struct.unpack('>II', d[o:o + 8])
					if tag in (1, 14, 15, 29): refs.add(val)	# NEEDED SONAME RPATH RUNPATH
			elif s[1] == 11:
				for o in range(s[4], s[4] + s[5], 16):
					refs.add(struct.unpack('>I', d[o:o + 4])[0])
		i = 0
		while i < size:
			j = d.index(b'\0', off + i) - off
			st = d[off + i:off + j]
			if st and any(h in st for h in HOST):
				if any(i <= r <= j for r in refs):
					badref.append(st)
				else:
					out[off + i:off + j] = bytes(j - i); cleared.append(st)
			i = j + 1
	return (bytes(out) if cleared else None), cleared, badref

def main():
	img = sys.argv[1]; dry = '-n' in sys.argv
	s = slices(img); files = nfix = 0; bad = []
	for top, prefix, skip in (('/', '', ('/var', '/home')), ('/var', '/var', ()), ('/home', '/home', ())):
		u = UFS(img, s[top], rw=not dry); seen = set()
		def walk(ino, path):
			nonlocal files, nfix
			mode = u.inode(ino)[0]; t = mode & 0xf000
			if t == 0x8000:
				d = u.read(ino)
				if d[:4] != b'\x7fELF' or not any(h in d for h in HOST): return
				new, cleared, badref = scrub(d)
				files += 1
				for b in badref: bad.append('%s: %s' % (path, b.decode('latin1')))
				if new:
					nfix += 1
					if not dry: u.overwrite(ino, new)
			elif t == 0x4000 and ino not in seen:
				seen.add(ino)
				for n, i in u.dir(ino).items():
					if n not in ('.', '..') and (path + '/' + n) not in skip: walk(i, path + '/' + n)
		walk(2, prefix)
	for b in bad: print('SCRUB referred to, left: ' + b)
	print('scrubpaths: %d programs with a host path, %s %d' % (files, 'would clear' if dry else 'cleared', nfix))
	sys.exit(1 if bad else 0)

if __name__ == '__main__':
	main()
