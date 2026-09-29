#!/usr/bin/env python3
"""uaeamix.py - run AMIX 2.1 (Amiga UNIX) in an emulated A3000, headless,
and drive it: the Amiga counterpart of tools/hatari-asv.sh.

  uaeamix.py start [install|run] [--emu E] [--from IMAGE] [--wait] [-s key=value]...
                                    --from: start from a copy of IMAGE (a clean
                                    system); --wait: until telnet answers
  uaeamix.py halt                   shut AMIX down (then: stop)
  uaeamix.py stop                   close the emulator
  uaeamix.py setup                  after installing: networking, clock
  uaeamix.py shot FILE.png          the emulator's screen
  uaeamix.py type 'TEXT'            \\n is Return; {ctrl-d}, {esc}, {f1}...
  uaeamix.py key NAME...            return, ctrl-d, up, f1, ...
  uaeamix.py floppy N PATH|-        insert into (or eject from) DF<N>
  uaeamix.py warp on|off
  uaeamix.py sh 'COMMAND' [TIMEOUT] as root over telnet; exits with its status
  uaeamix.py put LOCAL REMOTE       ftp, binary
  uaeamix.py get REMOTE LOCAL

The machine is an A3000: 68030 with MMU and 68882, ECS, 2 MB chip and
16 MB motherboard fast RAM (AMIX takes 4-16 MB), the A3000's WD33C93 SCSI
with the disk at ID 6 (and, for "install", the 2.1 tape at ID 4, as the
installer insists), and an A2065 on user-mode NAT (SLIRP). The guest is
10.0.2.15, the host 10.0.2.2, DNS 10.0.2.3; the host's 127.0.0.1:2323
reaches the guest's telnet and 127.0.0.1:2121 its ftp.

Emulators (--emu, or $UAEAMIX_EMU):
  amiberry  Amiberry ($AMIBERRY, default: the flatpak; the default)
  wine      WinUAE for Windows under Wine ($WINUAE_EXE, a winuae64.exe)
  winuae    WinUAE's Unix port ($WINUAE, default: winuae on $PATH)
All three run AMIX with the settings below, which turn cycle-exact off:
WinUAE 6 and Amiberry default to it, and their cycle-exact 68030 MMU
code breaks AMIX (README.md).

Files live in $AMIX_EMU (default ../work/emu): media/ (the 2.1 floppies
and tape, and the Kickstart), amix.hdf (the disk), run/ (this run's
config, log and screenshots), .amixpass (root's password, if set).
"""
import ftplib, glob, json, os, re, shutil, signal, socket, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get('AMIX_EMU', os.path.join(HERE, '..', 'work', 'emu'))
WORK = os.path.abspath(WORK)
RUN = os.path.join(WORK, 'run')
STATE = os.path.join(RUN, 'state.json')
TELNET_PORT, FTP_PORT = 2323, 2121

# Amiga raw key codes (US layout)
RAW = {}
for i, c in enumerate('`1234567890-=\\'):
	RAW[c] = i
for row, base in (('qwertyuiop[]', 0x10), ("asdfghjkl;'", 0x20), ('zxcvbnm,./', 0x31)):
	for i, c in enumerate(row):
		RAW[c] = base + i
RAW[' '] = 0x40
SHIFTED = dict(zip('~!@#$%^&*()_+|{}:"<>?', '`1234567890-=\\[];\',./'))
for c in 'abcdefghijklmnopqrstuvwxyz':
	SHIFTED[c.upper()] = c
NAMED = {'space': 0x40, 'backspace': 0x41, 'tab': 0x42, 'enter': 0x43,
	'return': 0x44, 'esc': 0x45, 'delete': 0x46, 'up': 0x4c, 'down': 0x4d,
	'right': 0x4e, 'left': 0x4f, 'shift': 0x60, 'rshift': 0x61,
	'capslock': 0x62, 'ctrl': 0x63, 'alt': 0x64, 'ralt': 0x65,
	'lamiga': 0x66, 'ramiga': 0x67, 'help': 0x5f}
for i in range(10):
	NAMED['f%d' % (i + 1)] = 0x50 + i

def die(msg):
	sys.exit('uaeamix: ' + msg)

def load_state():
	try:
		return json.load(open(STATE))
	except (OSError, ValueError):
		die('not running (start it first)')

def media(name):
	p = os.path.join(WORK, 'media', name)
	if not os.path.exists(p):
		die('missing %s (see README.md)' % p)
	return p

def kickstart():
	k = os.environ.get('AMIX_KICKSTART')
	if k:
		return k
	found = sorted(glob.glob(os.path.join(WORK, 'media', '*a3000*.rom')) +
		glob.glob(os.path.join(WORK, 'media', '*A3000*.rom')))
	if not found:
		die('no A3000 Kickstart: put one in media/ or set AMIX_KICKSTART')
	return found[0]

def config(mode, emu, extra):
	"""The A3000 AMIX needs, as .uae lines."""
	hdf = os.path.join(WORK, 'amix.hdf')
	if not os.path.exists(hdf):
		if mode != 'install':
			die('no %s: run "start install" first' % hdf)
		with open(hdf, 'wb') as f:
			f.truncate(int(os.environ.get('AMIX_DISK_MB', '1024')) << 20)
	kick = kickstart()
	c = {
		'use_gui': 'no', 'win32.start_not_captured': 'true',
		'kickstart_rom_file': kick,
		'chipset': 'ecs', 'chipset_compatible': 'A3000', 'ntsc': 'false',
		'cpu_model': '68030', 'mmu_model': '68030', 'fpu_model': '68882',
		# "more compatible" and JIT both break AMIX. So does cycle-exact,
		# which WinUAE 6 and Amiberry turn on by default: their pipelined
		# 68030 MMU tables mishandle MOVES (see README.md).
		'cpu_compatible': 'false', 'cpu_24bit_addressing': 'false',
		'cpu_cycle_exact': 'false', 'cpu_memory_cycle_exact': 'false',
		'cpu_speed': 'max', 'cachesize': '0',
		'chipmem_size': '4', 'bogomem_size': '0', 'fastmem_size': '0',
		'a3000mem_size': '16', 'collision_level': 'playfields',
		'nr_floppies': '1', 'floppy0type': '0', 'floppy_speed': '100',
		'scsi_a3000': 'true',
		'a2065': 'slirp',
		'sound_output': 'none',
		'gfx_width': '720', 'gfx_height': '568',
		'gfx_width_windowed': '720', 'gfx_height_windowed': '568',
		'gfx_linemode': 'double', 'gfx_resolution': 'hires',
	}
	keyfile = os.path.join(os.path.dirname(kick), 'rom.key')
	if os.path.exists(keyfile):
		c['kickstart_key_file'] = keyfile
	lines = []
	multi = [
		'hardfile2=rw,DH0:%s,0,0,0,512,0,,scsi6_a3000' % hdf,
		'uaehf0=hdf,rw,DH0:%s,0,0,0,512,0,,scsi6_a3000' % hdf,
		'slirp_redir=tcp:%d:23:10.0.2.15' % TELNET_PORT,
		'slirp_redir=tcp:%d:21:10.0.2.15' % FTP_PORT,
	]
	if mode == 'install':
		c['floppy0'] = media('amix_21_boot.adf')
		multi.append('uaehf1=tape0,ro,:%s,0,0,0,512,0,,scsi4_a3000' %
			media('amix_21_tape.zip'))
	if emu != 'amiberry':
		multi.append('lua=%s' % os.path.join(HERE, 'uaectl.lua'))
	if emu == 'wine':
		c['gfx_api'] = 'direct3d'	# Wine's Direct3D 11 fails on Xvfb
	for kv in extra:
		k, v = kv.split('=', 1)
		if k in ('slirp_redir', 'uaehf', 'hardfile2'):
			multi.append(kv)
		else:
			c[k] = v
	lines = ['%s=%s' % kv for kv in c.items()] + multi
	if emu == 'wine':
		# Wine maps / to Z:
		lines = [re.sub(r'(?<=[=:,])/', 'Z:/', l, count=1) if '/' in l else l
			for l in lines]
	return '\n'.join(lines) + '\n'

def free_display():
	for n in range(70, 80):
		if not os.path.exists('/tmp/.X%d-lock' % n):
			return ':%d' % n
	die('no free X display in :70-:79')

def amiberry_sockets():
	return set(glob.glob('/run/user/%d/.flatpak/com.blitterstudio.amiberry/xdg-run/amiberry*.sock' % os.getuid()) +
		glob.glob('/run/user/%d/amiberry*.sock' % os.getuid()))

def start(args):
	mode = 'run'
	if args and args[0] in ('install', 'run'):
		mode = args.pop(0)
	emu = os.environ.get('UAEAMIX_EMU', 'amiberry')
	extra = []
	image = None
	wait = False
	while args:
		a = args.pop(0)
		if a == '--emu':
			emu = args.pop(0)
		elif a == '--from':
			image = args.pop(0)
		elif a == '--wait':
			wait = True
		elif a == '-s':
			extra.append(args.pop(0))
		else:
			die('start: unknown argument ' + a)
	if os.path.exists(STATE):
		st = json.load(open(STATE))
		if any(pid_alive(p) for p in st['pids']):
			die('already running (stop it first)')
	if image:
		# a sparse copy: the image is mostly empty space
		subprocess.run(['cp', '--sparse=always', image, os.path.join(WORK, 'amix.hdf')], check=True)
		os.chmod(os.path.join(WORK, 'amix.hdf'), 0o644)
	os.makedirs(RUN, exist_ok=True)
	for f in glob.glob(os.path.join(RUN, '*')):
		if os.path.isfile(f):
			os.remove(f)
	cfg = os.path.join(RUN, 'amix.uae')
	open(cfg, 'w').write(config(mode, emu, extra))
	disp = os.environ.get('UAEAMIX_DISPLAY') or free_display()
	xv = subprocess.Popen(['Xvfb', disp, '-screen', '0', '1024x768x24', '-nolisten', 'tcp'],
		stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
	time.sleep(1)
	ctl = os.path.join(RUN, 'ctl')
	# The emulated clock chip reads the emulator's local time and AMIX
	# keeps GMT: run the emulator in UTC so the two agree.
	env = dict(os.environ, DISPLAY=disp, SDL_AUDIODRIVER='dummy', UAECTL_FILE=ctl, TZ='UTC')
	socks = amiberry_sockets()
	if emu == 'wine':
		exe = os.environ.get('WINUAE_EXE') or die('set WINUAE_EXE to a winuae64.exe (5.3.1)')
		env.update(WINEPREFIX=os.environ.get('WINEPREFIX', os.path.join(WORK, 'wine')),
			WINEDEBUG='-all', UAECTL_FILE='Z:' + ctl)
		cmd = ['wine', exe, '-log', '-f', 'Z:' + cfg]
	elif emu == 'winuae':
		env['SDL_VIDEODRIVER'] = 'x11'
		cmd = [os.environ.get('WINUAE', 'winuae'), '-f', cfg]
	elif emu == 'amiberry':
		env['SDL_VIDEODRIVER'] = 'x11'
		ab = os.environ.get('AMIBERRY')
		cmd = ([ab] if ab else ['flatpak', 'run', '--env=SDL_VIDEODRIVER=x11',
			'--env=SDL_AUDIODRIVER=dummy', 'com.blitterstudio.amiberry']) + \
			['--log', '--config', cfg, '-G']
	else:
		die('unknown emulator ' + emu)
	uae = subprocess.Popen(cmd, env=env, stdin=subprocess.DEVNULL,
		stdout=open(os.path.join(RUN, 'log'), 'w'), stderr=subprocess.STDOUT,
		start_new_session=True)
	st = dict(emu=emu, mode=mode, display=disp, pids=[uae.pid, xv.pid], ctl=ctl)
	if emu == 'amiberry':
		for _ in range(30):
			new = amiberry_sockets() - socks
			if new:
				st['sock'] = new.pop()
				break
			time.sleep(0.5)
		else:
			die('amiberry did not open its control socket')
	json.dump(st, open(STATE, 'w'))
	print('%s: %s on %s, log in %s' % (mode, emu, disp, os.path.join(RUN, 'log')))
	if wait:
		time.sleep(5)
		warp(st, True)
		ok = wait_telnet(300)
		warp(st, False)
		if not ok:
			die('no telnet login after 5 minutes (see "shot")')
		print('up')

def wait_telnet(secs):
	end = time.time() + secs
	while time.time() < end:
		try:
			t = Telnet()
			if t.read_until([b'ogin:'], 10) is not None:
				t.s.close()
				return True
			t.s.close()
		except OSError:
			pass
		time.sleep(3)
	return False

def pid_alive(pid):
	try:
		os.kill(pid, 0)
		return True
	except OSError:
		return False

def stop(args):
	st = load_state()
	try:
		if st['emu'] == 'amiberry':
			ipc(st, 'QUIT')
		else:
			ctl(st, ['AKS_QUIT 1'], wait=False)
	except Exception:
		pass
	uae, xv = st['pids']
	for _ in range(20):
		if not pid_alive(uae):
			break
		time.sleep(0.25)
	else:
		try:
			os.killpg(uae, signal.SIGKILL)
		except OSError:
			pass
	try:
		os.kill(xv, signal.SIGTERM)	# (SIGKILL would leave its lock file)
	except OSError:
		pass
	if st['emu'] == 'wine':
		env = dict(os.environ, WINEPREFIX=os.environ.get('WINEPREFIX', os.path.join(WORK, 'wine')))
		subprocess.run(['wineserver', '-k'], env=env, stderr=subprocess.DEVNULL)
	os.remove(STATE)

# -- control -------------------------------------------------------------

def ipc(st, *words):
	s = socket.socket(socket.AF_UNIX)
	s.settimeout(10)
	s.connect(st['sock'])
	s.sendall(('\t'.join(words) + '\n').encode())
	out = b''
	while not out.endswith(b'\n'):
		d = s.recv(65536)
		if not d:
			break
		out += d
	s.close()
	r = out.decode(errors='replace').rstrip('\n')
	if not r.startswith('OK'):
		die('amiberry: ' + r)
	return r

def ctl(st, lines, wait=True):
	path = st['ctl']
	for _ in range(600):
		if not os.path.exists(path):
			break
		time.sleep(0.1)
	tmp = path + '.tmp'
	open(tmp, 'w').write('\n'.join(lines) + '\n')
	if os.path.exists(path + '.done'):
		os.remove(path + '.done')
	os.rename(tmp, path)
	if wait:
		t = time.time()
		while not os.path.exists(path + '.done'):
			if time.time() - t > 120:
				die('the emulator is not taking commands')
			time.sleep(0.1)

def keystrokes(text):
	"""(code, shift, ctrl) for each key in text; {name} for named keys."""
	out = []
	i = 0
	while i < len(text):
		c = text[i]
		if c == '{':
			j = text.index('}', i)
			name = text[i + 1:j].lower()
			i = j + 1
			ctrl = name.startswith('ctrl-')
			if ctrl:
				name = name[5:]
			code = NAMED.get(name, RAW.get(name))
			if code is None:
				die('unknown key {%s}' % name)
			out.append((code, False, ctrl))
			continue
		i += 1
		if c == '\n':
			out.append((NAMED['return'], False, False))
		elif c == '\t':
			out.append((NAMED['tab'], False, False))
		elif c in RAW:
			out.append((RAW[c], False, False))
		elif c in SHIFTED:
			out.append((RAW[SHIFTED[c]], True, False))
		else:
			die('no key for %r' % c)
	return out

def press(st, keys):
	if st['emu'] == 'amiberry':
		for code, shift, ctrl in keys:
			mods = ([NAMED['shift']] if shift else []) + ([NAMED['ctrl']] if ctrl else [])
			for m in mods:
				ipc(st, 'SEND_KEY', str(m), '1')
			ipc(st, 'SEND_KEY', str(code), '1')
			time.sleep(0.04)
			ipc(st, 'SEND_KEY', str(code), '0')
			for m in mods:
				ipc(st, 'SEND_KEY', str(m), '0')
			time.sleep(0.04)
		return
	lines = []
	for code, shift, ctrl in keys:
		mods = ([NAMED['shift']] if shift else []) + ([NAMED['ctrl']] if ctrl else [])
		lines += ['KEY_RAW_DOWN 0x%02x' % m for m in mods]
		lines += ['KEY_RAW_DOWN 0x%02x' % code, 'KEY_RAW_UP 0x%02x' % code]
		lines += ['KEY_RAW_UP 0x%02x' % m for m in mods]
	for i in range(0, len(lines), 400):
		ctl(st, lines[i:i + 400])

def window(st):
	env = dict(os.environ, DISPLAY=st['display'])
	for name in ('WinUAE', 'Amiberry'):
		r = subprocess.run(['xdotool', 'search', '--name', name], env=env,
			capture_output=True, text=True).stdout.split()
		for wid in r:
			g = subprocess.run(['xwininfo', '-id', wid], env=env, capture_output=True, text=True).stdout
			m = re.search(r'Width: (\d+)\s+Height: (\d+)', g)
			if m and int(m.group(1)) > 300:
				return wid
	die('no emulator window on ' + st['display'])

def shot(st, path):
	env = dict(os.environ, DISPLAY=st['display'])
	wid = window(st)
	# WinUAE under Wine opens a window too small for AMIX's 640x400 console
	g = subprocess.run(['xwininfo', '-id', wid], env=env, capture_output=True, text=True).stdout
	w, h = map(int, re.search(r'Width: (\d+)\s+Height: (\d+)', g).groups())
	if w < 760 or h < 600:
		subprocess.run(['xdotool', 'windowsize', wid, '760', '600'], env=env)
		time.sleep(1.5)
	subprocess.run('xwd -silent -id %s | convert xwd:- "%s"' % (wid, path), shell=True,
		env=env, check=True)

def floppy(st, n, path):
	if st['emu'] == 'amiberry':
		if path == '-':
			ipc(st, 'EJECT_FLOPPY', n)
		else:
			ipc(st, 'INSERTFLOPPY', os.path.abspath(path), n)
		return
	p = '' if path == '-' else os.path.abspath(path)
	if st['emu'] == 'wine' and p:
		p = 'Z:' + p
	ctl(st, ['floppy%s %s' % (n, p)])

def warp(st, on):
	if st['emu'] == 'amiberry':
		ipc(st, 'SET_WARP', '1' if on else '0')
	else:
		ctl(st, ['AKS_WARP %d' % on])

# -- telnet and ftp (after installation) ---------------------------------

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240

class Telnet:
	def __init__(self, port=TELNET_PORT):
		self.s = socket.create_connection(('127.0.0.1', port), timeout=30)
		self.buf = b''

	def read_until(self, marks, timeout):
		end = time.time() + timeout
		while time.time() < end:
			for m in marks:
				if m in self.buf:
					i = self.buf.index(m) + len(m)
					out, self.buf = self.buf[:i], self.buf[i:]
					return out
			self.s.settimeout(max(0.1, end - time.time()))
			try:
				d = self.s.recv(4096)
			except socket.timeout:
				break
			if not d:
				break
			self.buf += self.strip(d)
		return None

	def strip(self, d):
		out = bytearray()
		i = 0
		while i < len(d):
			c = d[i]
			if c == IAC and i + 1 < len(d):
				cmd = d[i + 1]
				if cmd in (DO, DONT, WILL, WONT) and i + 2 < len(d):
					self.s.sendall(bytes([IAC, WONT if cmd in (DO, DONT) else DONT, d[i + 2]]))
					i += 3
					continue
				if cmd == SB:
					j = d.find(bytes([IAC, SE]), i)
					i = (j + 2) if j >= 0 else len(d)
					continue
				i += 2
				continue
			out.append(c)
			i += 1
		return bytes(out)

	def send(self, text):
		self.s.sendall(text.encode())

def password():
	try:
		return open(os.path.join(WORK, '.amixpass')).read().strip()
	except OSError:
		return ''

def run(cmd, timeout=60):
	"""cmd as root over telnet: (output, exit status)."""
	# AMIX's terminal line holds 256 characters, with the marker below
	if len(cmd) > 220:
		die('command longer than 220 characters: put a script and run that')
	# A connection that arrives while AMIX is still tearing down the last
	# session can get no login prompt (more often the slower the
	# emulation): try again a few times.
	for attempt in range(4):
		t = Telnet()
		if t.read_until([b'ogin:'], 15) is not None:
			break
		t.s.close()
		time.sleep(3)
	else:
		die('no login prompt on 127.0.0.1:%d' % TELNET_PORT)
	t.send('root\r\n')
	r = t.read_until([b'assword:', b'# ', b'$ '], 20)
	if r and b'assword:' in r:
		t.send(password() + '\r\n')
		r = t.read_until([b'# ', b'$ ', b'ogin incorrect', b'TERM'], 20)
	if r is None or b'ogin incorrect' in r:
		die('login failed')
	if b'TERM' in r:
		t.send('\r\n')
		t.read_until([b'# '], 20)
	t.send('stty -echo; PS1=; export PS1\r\n')
	t.read_until([b'\n'], 5)
	time.sleep(0.3)
	t.buf = b''
	# ("@@""END" so that the echoed command line can't match)
	t.send('%s\r\necho "@@""END $?"\r\n' % cmd)
	out = t.read_until([b'@@END '], timeout)
	if out is None:
		sys.stdout.write(t.buf.decode(errors='replace'))
		die('timed out')
	st = t.read_until([b'\n'], 5) or b'1'
	t.send('exit\r\n')
	return out[:-len(b'@@END ')].decode(errors='replace').replace('\r\n', '\n'), int(st.strip() or 1)

def sh(args):
	out, status = run(args[0], float(args[1]) if len(args) > 1 else 60)
	sys.stdout.write(out)
	sys.exit(status)

def halt(args):
	# nohup: the shutdown outlives this telnet session
	run('nohup /usr/sbin/shutdown -y -g0 -i0 > /dev/null 2>&1 &', 20)
	time.sleep(float(args[0]) if args else 50)

# What the first boot after installing doesn't set up. SLIRP learns the
# guest's MAC address from the guest's first packet, and only then can it
# forward a connection in, so the guest pings the host at every boot.
RC_SCRIPT = """# uaeamix (sp1/amix/emu): the emulator's network is SLIRP. It learns our
# MAC address from our first packet, so send one; and route through it.
route add default 10.0.2.2 1 >/dev/null 2>&1
ping 10.0.2.2 1 >/dev/null 2>&1
"""

def setup(args):
	st = load_state()
	# The first time, nothing has pinged: log in on the console to do it.
	# (Blind typing: this expects the console's login prompt.)
	press(st, keystrokes('root\n'))
	time.sleep(3)
	press(st, keystrokes(password() + '\n'))
	time.sleep(5)
	press(st, keystrokes('ping 10.0.2.2 1\n'))
	time.sleep(5)
	press(st, keystrokes('exit\n'))
	if not wait_telnet(60):
		die('telnet still not answering: check the console ("shot")')
	tmp = os.path.join(RUN, 'S99uaeamix')
	open(tmp, 'w').write(RC_SCRIPT)
	f = ftp()
	f.storbinary('STOR /etc/rc2.d/S99uaeamix', open(tmp, 'rb'))
	f.quit()
	out, status = run('chmod 744 /etc/rc2.d/S99uaeamix; echo nameserver 10.0.2.3 > /etc/resolv.conf', 60)
	if status:
		die('rc script: ' + out)
	# AMIX's setclk reads the clock chip's year as 19xx; the fixed one from
	# amigaunix.com doesn't. (date(1) still refuses years after 1999.)
	y2k = os.path.join(WORK, 'media', 'setclk.bz2')
	if os.path.exists(y2k):
		tmp = os.path.join(RUN, 'setclk')
		with open(tmp, 'wb') as f:
			subprocess.run(['bunzip2', '-c', y2k], stdout=f, check=True)
		f = ftp()
		f.storbinary('STOR /usr/amiga/bin/setclk.y2k', open(tmp, 'rb'))
		f.quit()
		out, status = run('cd /usr/amiga/bin; [ -f setclk.orig ] || cp setclk setclk.orig; '
			'cp setclk.y2k setclk; chmod 755 setclk; ./setclk; date', 60)
		sys.stdout.write(out)
	else:
		print('no media/setclk.bz2: the clock stays in the 1990s')
	print('set up; now "halt", "stop", and keep a copy of amix.hdf')

class GuestFTP(ftplib.FTP):
	"""Active-mode ftp through SLIRP: the guest connects back to 10.0.2.2,
	which SLIRP turns into the host's 127.0.0.1."""
	def makeport(self):
		sock = socket.create_server(('127.0.0.1', 0))
		self.sendport('10.0.2.2', sock.getsockname()[1])
		return sock

def ftp():
	f = GuestFTP()
	f.connect('127.0.0.1', FTP_PORT, timeout=60)
	f.login('root', password())
	f.set_pasv(False)
	return f

def main():
	if len(sys.argv) < 2 or sys.argv[1] in ('-h', '--help'):
		print(__doc__.strip())
		return
	c, args = sys.argv[1], sys.argv[2:]
	if c == 'start':
		start(args)
	elif c == 'stop':
		stop(args)
	elif c == 'sh':
		sh(args)
	elif c == 'halt':
		halt(args)
	elif c == 'setup':
		setup(args)
	elif c == 'put':
		f = ftp()
		f.storbinary('STOR ' + args[1], open(args[0], 'rb'))
		f.quit()
	elif c == 'get':
		f = ftp()
		f.retrbinary('RETR ' + args[0], open(args[1], 'wb').write)
		f.quit()
	else:
		st = load_state()
		if c == 'shot':
			shot(st, args[0])
		elif c == 'type':
			press(st, keystrokes(args[0].encode().decode('unicode_escape')))
		elif c == 'key':
			press(st, keystrokes(''.join('{%s}' % k for k in args)))
		elif c == 'floppy':
			floppy(st, args[0], args[1])
		elif c == 'warp':
			warp(st, args[0] in ('on', '1'))
		elif c == 'status':
			print(json.dumps(st))
		else:
			die('unknown command ' + c)

if __name__ == '__main__':
	main()
