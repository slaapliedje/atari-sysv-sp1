#!/usr/bin/env python3
"""build.py - the Atari System V release image, built fresh in Hatari.

  build.py [--rel DIR] [--hatari BIN] [--tos IMG] [--from STAGE] [--only STAGE]

Stages (each leaves its result in DIR/work):
  prep     base-patched.img from the stock image (kernel/patch-asv-image.py)
           with the rc hook in /etc/rc2.d/S75rpc; payload.img (mkpayload.sh);
           new.img (2 GiB, empty); exchange.img (the log)
  stage1   boots the stock copy; the hook runs /rel/stage1.sh: drivers into
           the kernel, dhcpc, the new disk made and the system copied to it
  stage2   boots the new disk (telnet through slirp): stage2.sh installs the
           userland, cleanup.sh clears the build's traces, the system halts
  pack     HD0.bin (sparse) + sha256, and the card files, in DIR/out

Needs: the stock image (asv_boot.img, md5 54a30c77cf845284908258bea14e5673),
the tt-dev Hatari (slirp, --scsi-net), xvfb-run, and what mkpayload.sh lists.
"""
import argparse, os, re, shutil, socket, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
SP1 = os.path.dirname(HERE)
STOCK_MD5 = '54a30c77cf845284908258bea14e5673'
ROOT_BASE = 102400          # the stock root slice (sector 200)
STAND_BASE = 240000 * 512   # the stock /stand slice (BFS)
GUEST = '192.168.30.20'
TELNET_PORT = 2524
DISK_SECTORS = 4194300      # 2 GiB - 2 KB: every slice stays under the kernel's 2 GiB limit


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def run(cmd, **kw):
    log('$', ' '.join(cmd) if isinstance(cmd, list) else cmd)
    return subprocess.run(cmd, check=True, **kw)


class Hatari:
    """One emulated TT, headless, in its own session (killed as a group)."""

    def __init__(self, args, work, disks, telnet=None, extra=(), display=110, net=True):
        self.work = work; self.display = ':%d' % display
        os.makedirs(os.path.join(work, 'home'), exist_ok=True)
        self.log = open(os.path.join(work, 'hatari.log'), 'w')
        if net:
            net = '4=slirp,net=192.168.30.0/24,host=192.168.30.1,guest=%s' % GUEST
            if telnet:
                net += ',hostfwd=tcp:127.0.0.1:%d-:23' % telnet
        self.xauth = os.path.join(work, 'xauth'); open(self.xauth, 'w').close()
        cmd = ['xvfb-run', '-n', str(display), '-f', self.xauth, '-s', '-screen 0 1024x768x24', args.hatari,
               '--machine', 'tt', '--tos', args.tos, '--addr24', 'off', '--mmu', 'on',
               '--fpu', '68882', '--memsize', '4', '--ttram', '16',
               # ASV reads the TT's clock chip two years ahead of what Hatari
               # puts in it (2028 for 2026): the files it writes get this year
               '--rtc-year', str(time.localtime().tm_year - 2)]
        for i, d in sorted(disks.items()):
            cmd += ['--scsi', '%d=%s' % (i, d)]
        if net:
            cmd += ['--scsi-net', net]
        cmd += ['--monitor', 'vga', '--fast-forward', 'on',
                '--sound', 'off', '--confirm-quit', 'no'] + list(extra)
        env = dict(os.environ, HOME=os.path.join(work, 'home'), SDL_AUDIODRIVER='dummy')
        log('hatari:', ' '.join(cmd))
        self.p = subprocess.Popen(cmd, stdout=self.log, stderr=subprocess.STDOUT,
                                  env=env, start_new_session=True)

    def alive(self):
        return self.p.poll() is None

    def screenshot(self, name):
        try:
            subprocess.run(['import', '-window', 'root', os.path.join(self.work, name)],
                           env=dict(os.environ, DISPLAY=self.display, XAUTHORITY=self.xauth), timeout=20, check=False)
        except Exception:
            pass

    def stop(self):
        if self.alive():
            os.killpg(self.p.pid, 9)
        self.p.wait()
        self.log.close()


def telnet_sh(port, cmd, timeout=60):
    """Run CMD as root on the emulated system over telnet (stock root: no
    password); returns (status, output).  Lines over ~250 characters are
    dropped by ASV's tty: keep commands short, put the work in scripts."""
    IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240

    def recv(s, until, to):
        buf = b''; end = time.time() + to
        while time.time() < end:
            s.settimeout(max(0.1, end - time.time()))
            try:
                d = s.recv(4096)
            except socket.timeout:
                break
            if not d:
                break
            out = bytearray(); i = 0
            while i < len(d):
                c = d[i]
                if c == IAC and i + 1 < len(d):
                    k = d[i + 1]
                    if k in (DO, DONT, WILL, WONT) and i + 2 < len(d):
                        s.sendall(bytes([IAC, WONT if k in (DO, DONT) else DONT, d[i + 2]])); i += 3; continue
                    if k == SB:
                        j = d.find(bytes([IAC, SE]), i); i = (j + 2) if j >= 0 else len(d); continue
                    i += 2; continue
                out.append(c); i += 1
            buf += bytes(out)
            if any(u in buf for u in until):
                break
        return buf

    s = socket.create_connection(('127.0.0.1', port), timeout=15)
    b = recv(s, [b'ogin:'], 30)
    s.sendall(b'root\r\n')
    b = recv(s, [b'# ', b'assword:', b'terminal type'], 30)
    if b'assword:' in b:
        s.sendall(b'\r\n'); b = recv(s, [b'# ', b'terminal type'], 20)
    if b'terminal type' in b:
        s.sendall(b'vt100\r\n'); b = recv(s, [b'# '], 20)
    if b'New password' in b or b'# ' not in b:
        # login's aging check turns "no password" into "set one now" when
        # the clock is wrong (a 1970 clock did this once)
        s.close()
        raise SystemExit('telnet: no shell after the root login: %r' % b[-200:])
    mark = 'XX%dXX' % int(time.time())
    s.sendall(('%s; echo %s$?\r\n' % (cmd, mark)).encode())
    b = recv(s, [mark.encode() + bytes(str(n), 'ascii') for n in range(10)], timeout)
    b += recv(s, [b'# '], 3)
    txt = b.decode('latin1').replace('\r', '')
    m = re.search(re.escape(mark) + r'(\d+)', txt)
    out = txt[:m.start()] if m else txt
    try:
        s.sendall(b'exit\r\n'); s.close()
    except OSError:
        pass
    return (int(m.group(1)) if m else 99), out


def wait_login(port, deadline, hatari=None):
    start = time.time()
    while time.time() - start < deadline:
        if hatari and not hatari.alive():
            raise SystemExit('hatari exited: see its log')
        try:
            s = socket.create_connection(('127.0.0.1', port), timeout=5)
            s.settimeout(2); b = b''; end = time.time() + 15
            while time.time() < end and b'ogin:' not in b:
                try:
                    d = s.recv(512)
                except socket.timeout:
                    continue
                if not d:
                    break
                i = 0
                while i < len(d):
                    if d[i] == 255 and i + 2 < len(d) and d[i + 1] in (251, 252, 253, 254):
                        s.sendall(bytes([255, 252 if d[i + 1] in (253, 254) else 254, d[i + 2]])); i += 3
                    else:
                        b += d[i:i + 1]; i += 1
            s.close()
            if b'ogin:' in b:
                log('login prompt after %d s' % (time.time() - start))
                return True
        except OSError:
            pass
        time.sleep(10)
    return False


def exchange_text(path):
    with open(path, 'rb') as f:
        d = f.read(4 << 20)
    return d.split(b'\0', 1)[0].decode('latin1')


def wait_marker(path, done, failed, deadline, hatari, shots=None):
    start = time.time(); n = 0; last = ''
    while time.time() - start < deadline:
        if not hatari.alive():
            raise SystemExit('hatari exited: see its log')
        t = exchange_text(path)
        if t != last:
            new = t[len(last):]
            sys.stdout.write(new); sys.stdout.flush(); last = t
        # (the hook replays the log of the earlier boots: only the last
        # phase's part counts)
        cur = t[t.rfind('== stage 1, phase'):]
        if done in cur:
            return True
        if failed in cur:
            return False
        time.sleep(15); n += 1
        if shots and n % 4 == 0:
            hatari.screenshot('screen-%03d.png' % (n // 4))
    return False


def sparse_copy(src, dst):
    run(['cp', '--sparse=always', src, dst])


def stage_prep(a, w):
    stock = a.stock
    out = subprocess.run(['md5sum', stock], capture_output=True, text=True, check=True).stdout.split()[0]
    if out != STOCK_MD5:
        raise SystemExit('%s: md5 %s, expected the stock asv_boot.img %s' % (stock, out, STOCK_MD5))
    base = os.path.join(w, 'base-patched.img')
    sparse_copy(stock, base)
    run([sys.executable, '-I', os.path.join(SP1, 'kernel', 'patch-asv-image.py'), base])
    # the rc hook: S75rpc's bytes, exactly, on the throw-away copy
    orig = subprocess.run([sys.executable, '-I', os.path.join(SP1, 'tools', 'ufs.py'), base, str(ROOT_BASE),
                           'cat', '/etc/rc2.d/S75rpc'], capture_output=True, check=True).stdout
    hook = open(os.path.join(HERE, 'hook.sh'), 'rb').read()
    if len(hook) > len(orig):
        raise SystemExit('hook.sh is longer than S75rpc (%d > %d)' % (len(hook), len(orig)))
    hook += b'\n' * (len(orig) - len(hook))
    hp = os.path.join(w, 'hook.bin'); open(hp, 'wb').write(hook)
    run([sys.executable, '-I', os.path.join(SP1, 'tools', 'ufs.py'), base, str(ROOT_BASE), 'put', '/etc/rc2.d/S75rpc', hp])
    # the payload disk: the tar, padded to whole sectors
    run(['sh', os.path.join(HERE, 'mkpayload.sh'), w], env=dict(os.environ, REL=a.rel, STOCK=stock, MANX=a.manx))
    tar = os.path.join(w, 'payload.tar'); pay = os.path.join(w, 'payload.img')
    n = os.path.getsize(tar); pad = (-n) % 512 + 512 * 64
    shutil.copyfile(tar, pay)
    with open(pay, 'ab') as f:
        f.write(b'\0' * pad)
    # the new disk and the log disk
    for name, size in (('new.img', DISK_SECTORS * 512), ('exchange.img', 16 << 20)):
        p = os.path.join(w, name)
        with open(p, 'wb') as f:
            f.truncate(size)
    log('prep done')


def stage_1(a, w):
    base, new, pay, ex = (os.path.join(w, n) for n in ('base-patched.img', 'new.img', 'payload.img', 'exchange.img'))
    with open(ex, 'wb') as f:
        f.truncate(16 << 20)
    h = Hatari(a, os.path.join(w, 'run1'), {0: base, 1: new, 2: pay, 3: ex})
    try:
        ok = wait_marker(ex, 'STAGE1 DONE', 'STAGE1 FAILED', a.deadline, h, shots=True)
        time.sleep(20)       # the last sync
        h.screenshot('final.png')
    finally:
        h.stop()
    open(os.path.join(w, 'stage1.log'), 'w').write(exchange_text(ex))
    if not ok:
        raise SystemExit('stage 1 failed: see %s/stage1.log and %s/run1' % (w, w))
    log('stage 1 done')


def stage_2(a, w):
    new, pay, ex = (os.path.join(w, n) for n in ('new.img', 'payload.img', 'exchange.img'))
    h = Hatari(a, os.path.join(w, 'run2'), {0: new, 2: pay, 3: ex}, telnet=TELNET_PORT, display=111)
    try:
        if not wait_login(TELNET_PORT, 1500, h):
            h.screenshot('nologin.png')
            raise SystemExit('stage 2: no login prompt')
        for script, marker in (('stage2.sh', 'STAGE2 DONE'), ('cleanup.sh', 'CLEANUP DONE')):
            st, out = telnet_sh(TELNET_PORT, 'sh /rel/%s 2>&1' % script, a.deadline)
            open(os.path.join(w, script + '.log'), 'w').write(out)
            sys.stdout.write(out)
            if marker not in out:
                h.screenshot('failed.png')
                raise SystemExit('%s did not finish (status %d)' % (script, st))
        halt(h)
    finally:
        h.stop()
    log('stage 2 done')


def halt(h):
    """init 0: init runs /sbin/rc0 off on the console (inittab s0), which
    unmounts everything and halts; the telnet session dies with the rest.
    (A backgrounded nohup rc0 from the telnet shell never ran.)"""
    telnet_sh(TELNET_PORT, '/sbin/init 0', 10)
    log('halting: init 0')
    time.sleep(120)
    h.screenshot('halted.png')


def stage_halt(a, w):
    """Boot the new disk once more and halt it: for a disk that was
    imaged while running (its first boot then rewrites hosts.net)."""
    new = os.path.join(w, 'new.img')
    h = Hatari(a, os.path.join(w, 'run3'), {0: new}, telnet=TELNET_PORT, display=111)
    try:
        if not wait_login(TELNET_PORT, 1500, h):
            raise SystemExit('halt: no login prompt')
        halt(h)
    finally:
        h.stop()


def stage_pack(a, w):
    out = os.path.join(a.rel, 'out'); os.makedirs(out, exist_ok=True)
    hd0 = os.path.join(out, 'HD0.bin')
    sparse_copy(os.path.join(w, 'new.img'), hd0)
    open(os.path.join(out, 'NE4.img'), 'wb').close()
    for f in os.listdir(os.path.join(HERE, 'card')):
        shutil.copy(os.path.join(HERE, 'card', f), out)
    run('cd %s && sha256sum HD0.bin > HD0.bin.sha256 && cat HD0.bin.sha256' % out, shell=True)
    # the card's files in one zip (the 2 GiB image is mostly zeros)
    z = os.path.join(a.rel, 'asv-sp1-card.zip')
    if os.path.exists(z):
        os.unlink(z)
    run('cd %s && zip -q -9 %s HD0.bin HD0.bin.sha256 NE4.img README.txt zuluscsi.ini bluescsi.ini && ls -l %s' % (out, z, z), shell=True)
    log('packed in', out, 'and', z)


STAGES = [('prep', stage_prep), ('stage1', stage_1), ('stage2', stage_2), ('pack', stage_pack), ('halt', stage_halt)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rel', default=os.path.expanduser('~/dev/asv-release'))
    ap.add_argument('--stock', default=os.path.expanduser('~/dev/OpenUA/data/work/asv/asv-richard/asv_boot.img'))
    ap.add_argument('--hatari', default=os.path.expanduser('~/dev/hatari-tt/build/src/hatari'))
    ap.add_argument('--tos', default='/usr/share/hatari/tos306us.img')
    ap.add_argument('--manx', default=os.path.expanduser('~/dev/unix-browser'))
    ap.add_argument('--from', dest='frm', default='prep')
    ap.add_argument('--only')
    ap.add_argument('--deadline', type=int, default=5400, help='seconds for a stage')
    a = ap.parse_args()
    w = os.path.join(a.rel, 'work'); os.makedirs(w, exist_ok=True)
    names = [n for n, _ in STAGES]
    todo = [a.only] if a.only else [n for n in names[names.index(a.frm):] if n != 'halt']
    for n, f in STAGES:
        if n in todo:
            log('==', n); f(a, w)


if __name__ == '__main__':
    main()
