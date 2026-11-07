#!/usr/bin/env python3
"""permscan.py STOCK.img NEW.img [--list OUT]: owners and modes of every path
in the new card image (/, /var, /home and the BFS /stand) against the stock
image's.  Exit 1, naming each path, when the new image has
  - anything writable by any user that the stock image doesn't have so
    (or more of it: a directory without the sticky bit),
  - a path that differs from stock or isn't in it, writable by a group
    ordinary users are in (other, users: a new user's group is other),
  - an owner or group that isn't in the image's passwd or group file,
  - a setuid or setgid file that isn't stock.
The finds the smoke runs on a booted copy cover what the first boot makes;
this covers what the build wrote, including what is under mount points."""
import mmap, os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from ufs import UFS

T = {0x4000: 'd', 0x8000: 'f', 0xa000: 'l', 0x2000: 'c', 0x6000: 'b', 0x1000: 'p', 0xc000: 's'}

def slices(img):
	"""{'/': base, '/var': base, '/home': base, '/stand': base} by what is in them"""
	f = open(img, 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ); r = {}
	off = 0
	while True:
		i = m.find(b'\x00\x01\x19\x54', off)
		if i < 0: break
		off = i + 1; base = i - 1372 - 8192
		if base < 0 or base % 512: continue
		try: d = UFS(img, base).dir(2)
		except Exception: continue
		if 'lost+found' not in d: continue
		if 'etc' in d and 'usr' in d: r.setdefault('/', base)
		elif 'adm' in d and 'spool' in d: r.setdefault('/var', base)
		elif 'rootshadow' in d: r.setdefault('/home', base)
	off = 0
	while '/stand' not in r:
		i = m.find(b'\x1b\xad\xfa\xce', off)
		if i < 0: break
		if i % 512 == 0: r['/stand'] = i
		off = i + 1
	assert len(r) == 4, '%s: found only %s' % (img, sorted(r))
	return r

def ufs_walk(img, base, prefix, out):
	u = UFS(img, base); seen = set()
	def walk(ino, path):
		raw = u.inode(ino)[4]
		mode, = struct.unpack('>H', raw[0:2]); uid, gid = struct.unpack('>ii', raw[116:124])
		t = T.get(mode & 0xf000, '?'); out[path or '/'] = (t, mode & 0o7777, uid, gid)
		if t != 'd' or ino in seen: return
		seen.add(ino)
		for n, i in u.dir(ino).items():
			if n not in ('.', '..'): walk(i, path + '/' + n)
	walk(2, prefix.rstrip('/'))

def bfs_walk(img, base, out):
	f = open(img, 'rb'); f.seek(base); sb = f.read(512)
	magic, start, end = struct.unpack('>III', sb[:12]); assert magic == 0x1BADFACE
	FMT = '>HIIIIIIIIIII16s'; recs = {}
	for i in range((start - 512) // 62):
		f.seek(base + 512 + i * 62); r = struct.unpack(FMT, f.read(62))
		if r[0]: recs[r[0]] = r
	attr = lambda r: ('d' if r[4] == 2 else 'f', r[5] & 0o7777, r[6], r[7])
	root = recs[2]; out['/stand'] = attr(root)
	f.seek(base + root[1] * 512); rd = f.read(root[3] - root[1] * 512 + 1)
	for o in range(0, len(rd), 16):
		ino, = struct.unpack('>H', rd[o:o+2]); name = rd[o+2:o+16].split(b'\0')[0].decode('latin1')
		if ino and name not in ('.', '..'): out['/stand/' + name] = attr(recs[ino])

def scan(img):
	"""path -> (type, mode, uid, gid); the file systems' roots replace their mount points"""
	s = slices(img); out = {}
	ufs_walk(img, s['/'], '/', out)
	for p in ('/var', '/home'):
		for k in [k for k in out if k.startswith(p + '/')]: del out[k]
		ufs_walk(img, s[p], p, out)
	bfs_walk(img, s['/stand'], out)
	return out, s

def ids(img, s, path):
	u = UFS(img, s['/']); r = {}
	for l in u.read(u.lookup(path)).decode('latin1').splitlines():
		f = l.split(':')
		if len(f) >= 3 and f[2].isdigit(): r.setdefault(int(f[2]), f[0])
	return r

def main():
	stock, new = sys.argv[1], sys.argv[2]
	S, _ = scan(stock); B, s = scan(new)
	users, groups = ids(new, s, '/etc/passwd'), ids(new, s, '/etc/group')
	if '--list' in sys.argv:
		with open(sys.argv[sys.argv.index('--list') + 1], 'w') as f:
			for p in sorted(B): f.write('%s\t%s\t%04o\t%d\t%d\n' % ((p,) + B[p]))
	open_groups = {g for g, n in groups.items() if n in ('other', 'users')}
	skip = ('/proc/', '/dev/', '/tmp/', '/var/tmp/')
	name = lambda a: '%s %04o %s:%s' % (a[0], a[1], users.get(a[2], a[2]), groups.get(a[3], a[3]))
	world = lambda a: a[1] & 0o002 and not (a[0] == 'd' and a[1] & 0o1000)
	bad = []
	for p in sorted(B):
		t, m, u, g = a = B[p]
		if t == 'l' or p.startswith(skip): continue
		st = S.get(p)
		why = []
		if world(a) and not (st and world(st)): why.append('writable by any user')
		if st != a and m & 0o020 and g in open_groups: why.append('writable by group %s' % groups[g])
		if u not in users: why.append('owner uid %d is in no passwd entry' % u)
		if g not in groups: why.append('group gid %d is in no group entry' % g)
		if t == 'f' and m & 0o6000 and not (st and st[1] & 0o6000 == m & 0o6000): why.append('setuid/setgid, not stock')
		if why: bad.append('%s  %s%s: %s' % (p, name(a), '' if not st else ' (stock %s)' % name(st), ', '.join(why)))
	for l in bad: print('PERMS ' + l)
	print('permscan: %d paths, %d not stock, %d problems' % (len(B), len(set(B) - set(S)), len(bad)))
	sys.exit(1 if bad else 0)

if __name__ == '__main__':
	main()
