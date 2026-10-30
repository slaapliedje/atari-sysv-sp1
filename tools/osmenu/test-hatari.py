#!/usr/bin/env python3
"""test-hatari.py [CASE...]: OSMENU.PRG and MINTBOOT.PRG in Hatari on a TT,
a Falcon030, an STE and an ST, from a GEMDOS-emulated C: (no disk image).

Each case boots a directory holding \\AUTO\\AOSMENU.PRG (Hatari lists AUTO
alphabetically, and the menu has to come before MINTBOOT.PRG), its
OSMENU.INF, and stand-ins for MagiC's booter and the FreeMiNT kernel that
leave a file behind (\\MAGXRAN.TXT, \\MINTRAN.TXT).  The NVRAM starts with
the case's boot preference and is read back from hatari.nvram; keys go in
through Hatari's command fifo once the GEMDOS trace shows the menu polling
the keyboard.  Screenshots stay in WORK/CASE/shots.

Environment: WORK (default ~/dev/osmenu-test), HATARI (a Hatari with
--cmd-fifo), TOSDIR (tos306us.img, tos404.img, tos206us.img,
tos104us.img; default /usr/share/hatari), AS and OBJCOPY as for build.sh.
"""
import os, sys, struct, shutil, subprocess, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get('WORK', os.path.expanduser('~/dev/osmenu-test'))
HATARI = os.environ.get('HATARI', os.path.expanduser('~/dev/hatari-tt/build/src/hatari'))
TOSDIR = os.environ.get('TOSDIR', '/usr/share/hatari')
AS = os.environ.get('AS', os.path.expanduser('~/opt/asv-cross/bin/m68k-cbm-sysv4-as'))
OBJCOPY = os.environ.get('OBJCOPY', os.path.expanduser('~/opt/asv-cross/bin/m68k-cbm-sysv4-objcopy'))

MACHINES = {
    'tt': ['--machine', 'tt', '--tos', 'tos306us.img', '--memsize', '4', '--monitor', 'vga'],
    'falcon': ['--machine', 'falcon', '--tos', 'tos404.img', '--memsize', '4', '--monitor', 'vga',
               '--dsp', 'none'],
    'ste': ['--machine', 'ste', '--tos', 'tos206us.img', '--memsize', '1', '--monitor', 'mono'],
    'st': ['--machine', 'st', '--tos', 'tos104us.img', '--memsize', '1', '--monitor', 'mono'],
}

FULL = 'menu=tos,magic,freemint,unix,netbsd,linux\r\ndefault=magic\r\ntimeout=30\r\nmint=C:\\MINT\\FAKEMINT.PRG\r\n'
# name: machine, OSMENU.INF, NVRAM preference at the start, keys (an ASCII
# character or an ST scancode, 28 = Return; None = wait for the time-out),
# what is expected: preference after, MagiC's and FreeMiNT's stand-ins run,
# words on the screen with the menu
CASES = {
    'tt-netbsd': ('tt', FULL, 0x80, ['5', 'x'], dict(pref=0x20, magx=0, mint=0)),
    'tt-magic': ('tt', FULL, 0x80, ['2'], dict(pref=0x80, magx=1, mint=0)),
    'tt-mint': ('tt', FULL, 0x80, ['3'], dict(pref=0x80, magx=0, mint=1)),
    'tt-return': ('tt', FULL.replace('default=magic', 'default=sysv'), 0x80, ['28', 'x'],
                  dict(pref=0x40, magx=0, mint=0)),
    'tt-auto': ('tt', 'timeout=3\r\n', 0x80, [None], dict(pref=0x80, magx=0, mint=0)),
    'tt-magicpref': ('tt', 'menu=tos,magic,sysv\r\ndefault=tos\r\ntimeout=3\r\n', 0x08, [None],
                     dict(pref=0x08, magx=1, mint=0)),
    'tt-nomenu': ('tt', 'menu=tos,magic\r\ndefault=magic\r\ntimeout=0\r\n', 0x80, [],
                  dict(pref=0x80, magx=1, mint=0)),
    'tt-nomint': ('tt', 'menu=tos,mint\r\ntimeout=30\r\n', 0x80, ['2'], dict(pref=0x80, magx=0, mint=0, out=0)),
    'falcon-linux': ('falcon', 'menu=tos,linux,mint\r\ntimeout=30\r\nmint=C:\\MINT\\FAKEMINT.PRG\r\n', 0x80,
                     ['2', 'x'], dict(pref=0x10, magx=0, mint=0)),
    'ste-nonvram': ('ste', 'menu=tos,sysv,magic,netbsd\r\ntimeout=30\r\n', 0x80, ['2'],
                    dict(pref=None, magx=1, mint=0)),
    'st-nonvram': ('st', 'menu=tos,sysv,magic,netbsd\r\ntimeout=30\r\n', 0x80, ['2'],
                   dict(pref=None, magx=1, mint=0)),
    # the sample OSMENU.INF as handed out: comment lines, then the time-out
    'tt-sample': ('tt', open(os.path.join(HERE, 'OSMENU.INF')).read().replace('\n', '\r\n'), 0x80,
                  [None], dict(pref=0x80, magx=0, mint=0)),
}

# Hatari's NVRAM file: bytes 14..63 of the chip; the preference is the word
# at 0, the checksum (~sum, sum of the 48 bytes before) at 48..49
NVRAM = bytes.fromhex('0080000000000000112e2001ff00001a87') + bytes(31)

def nvram(pref):
    b = bytearray(NVRAM + b'\0\0')
    b[0], b[1] = 0, pref
    s = sum(b[:48]) & 0xff
    b[48], b[49] = (~s) & 0xff, s
    return bytes(b)

def prg(src, out, tmp):
    s = os.path.join(tmp, 'p.s'); o = os.path.join(tmp, 'p.o'); b = os.path.join(tmp, 'p.bin')
    open(s, 'w').write(src)
    subprocess.run([AS, '-m68000', '-o', o, s], check=True)
    subprocess.run([OBJCOPY, '-O', 'binary', o, b], check=True)
    t = open(b, 'rb').read()
    open(out, 'wb').write(struct.pack('>HLLLLLLH', 0x601a, len(t), 0, 0, 0, 0, 0, 0) + t + b'\0\0\0\0')

# a stand-in: creates a file, says so, ends
STANDIN = r'''	.text
	clr.w	-(%%sp)
	pea	name(%%pc)
	move.w	&0x3c,-(%%sp)
	trap	&1
	addq.l	&8,%%sp
	move.w	%%d0,-(%%sp)
	move.w	&0x3e,-(%%sp)
	trap	&1
	addq.l	&4,%%sp
	pea	msg(%%pc)
	move.w	&9,-(%%sp)
	trap	&1
	addq.l	&6,%%sp
	clr.w	-(%%sp)
	trap	&1
name:	.asciz	"%s"
msg:	.ascii	"%s"
	.byte	13,10,0
'''

def run(name, case, results):
    machine, inf, pref0, keys, want = case
    d = os.path.join(WORK, name)
    shutil.rmtree(d, ignore_errors=True)
    c = os.path.join(d, 'c'); shots = os.path.join(d, 'shots'); home = os.path.join(d, 'home')
    for p in (os.path.join(c, 'AUTO'), os.path.join(c, 'MINT'), shots, os.path.join(home, '.config', 'hatari')):
        os.makedirs(p)
    shutil.copy(os.path.join(HERE, 'OSMENU.PRG'), os.path.join(c, 'AUTO', 'AOSMENU.PRG'))
    shutil.copy(os.path.join(HERE, 'MINTBOOT.PRG'), os.path.join(c, 'AUTO', 'MINTBOOT.PRG'))
    shutil.copy(os.path.join(WORK, 'MAGXBOOT.PRG'), os.path.join(c, 'MAGXBOOT.PRG'))
    shutil.copy(os.path.join(WORK, 'FAKEMINT.PRG'), os.path.join(c, 'MINT', 'FAKEMINT.PRG'))
    open(os.path.join(c, 'AUTO', 'OSMENU.INF'), 'w', newline='').write(inf)
    nv = os.path.join(home, '.config', 'hatari', 'hatari.nvram')
    open(nv, 'wb').write(nvram(pref0))
    fifo = os.path.join(d, 'fifo'); trace = os.path.join(d, 'trace.log')
    m = MACHINES[machine]
    m = [x if not x.endswith('.img') else os.path.join(TOSDIR, x) for x in m]
    cmd = [HATARI] + m + ['--harddrive', c, '--gemdos-drive', 'C', '--protect-hd', 'off',
           '--fast-boot', 'on', '--sound', 'off', '--confirm-quit', 'no', '--statusbar', 'no',
           '--cmd-fifo', fifo, '--screenshot-dir', shots, '--screenshot-format', 'png',
           '--trace', 'gemdos', '--trace-file', trace, '--log-file', os.path.join(d, 'hatari.log'),
           '--alert-level', 'fatal']
    env = dict(os.environ, HOME=home, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy')
    p = subprocess.Popen(cmd, env=env, stdout=open(os.path.join(d, 'hatari.out'), 'w'),
                         stderr=subprocess.STDOUT)
    def send(s):
        with open(fifo, 'w') as f:
            f.write(s + '\n')
    def traced(word, t):
        end = time.time() + t
        while time.time() < end:
            try:
                if word in open(trace, errors='replace').read():
                    return True
            except OSError:
                pass
            time.sleep(0.5)
        return False
    n = [0]
    def shot(tag):
        before = set(os.listdir(shots))
        send('hatari-shortcut screenshot')
        for _ in range(20):
            time.sleep(0.25)
            new = set(os.listdir(shots)) - before
            if new:
                os.rename(os.path.join(shots, new.pop()), os.path.join(shots, '%d-%s.png' % (n[0], tag)))
                n[0] += 1
                return
    note = []
    try:
        if not traced('OSMENU.INF', 120):
            note.append('the menu never read OSMENU.INF')
        elif keys:
            if not traced('Cconis', 30):
                note.append('no keyboard polling (no menu)')
            send('hatari-debug trace none')
            time.sleep(2)
            shot('menu')
            for k in keys:
                if k is None:
                    time.sleep(6)          # the time-out
                    continue
                send('hatari-event keypress %s' % k)
                time.sleep(3)
                shot('key%s' % k)
        else:
            send('hatari-debug trace none')
        time.sleep(6)
        shot('end')
        send('hatari-shortcut quit')
        p.wait(20)
    except subprocess.TimeoutExpired:
        p.kill(); note.append('killed')
    pref = open(nv, 'rb').read()[1]
    got = dict(pref=pref,
               magx=int(os.path.exists(os.path.join(c, 'MAGXRAN.TXT'))),
               mint=int(os.path.exists(os.path.join(c, 'MINTRAN.TXT'))),
               out=int(os.path.exists(os.path.join(c, 'AUTO', 'OSMENU.OUT'))))
    bad = [k for k, v in want.items() if v is not None and got[k] != v]
    if want.get('pref') is None and got['pref'] != pref0:
        bad.append('pref')
    if got['out'] and 'out' not in want:
        bad.append('out')
    results[name] = (not bad and not note, got, bad, note)

def main():
    names = sys.argv[1:] or list(CASES)
    os.makedirs(WORK, exist_ok=True)
    tmp = os.path.join(WORK, 'build'); os.makedirs(tmp, exist_ok=True)
    prg(STANDIN % ('\\\\MAGXRAN.TXT', 'stand-in MAGXBOOT.PRG ran'), os.path.join(WORK, 'MAGXBOOT.PRG'), tmp)
    prg(STANDIN % ('\\\\MINTRAN.TXT', 'stand-in FreeMiNT kernel ran'), os.path.join(WORK, 'FAKEMINT.PRG'), tmp)
    results = {}
    th = [threading.Thread(target=run, args=(n, CASES[n], results)) for n in names]
    for t in th:
        t.start(); time.sleep(1)
    for t in th:
        t.join()
    ok = True
    for n in names:
        good, got, bad, note = results.get(n, (False, {}, ['no result'], []))
        ok &= good
        print('%-14s %s  pref=%s magx=%s mint=%s out=%s %s %s' % (
            n, 'ok  ' if good else 'FAIL', hex(got.get('pref', 0)), got.get('magx'), got.get('mint'),
            got.get('out'), ' '.join(bad), '; '.join(note)))
    sys.exit(0 if ok else 1)

main()
