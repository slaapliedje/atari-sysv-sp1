#!/usr/bin/env python3
"""Run a shell command on the TT030 (Atari System V) over telnet.
usage: ASV_HOST=<tt address> asvsh.py 'command' [timeout]   (root; password read from .asvpass beside this script; LAN only)"""
import os, socket, sys, time, re
HOST, PORT = os.environ.get('ASV_HOST'), 23
if not HOST:
    sys.exit('set ASV_HOST to the TT\'s address (the one in its /etc/inet/hosts.net)')
IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
def recv(s, until, timeout):
    buf = b''; end = time.time() + timeout
    while time.time() < end:
        s.settimeout(max(0.1, end - time.time()))
        try: d = s.recv(4096)
        except socket.timeout: break
        if not d: break
        out = bytearray(); i = 0
        while i < len(d):
            c = d[i]
            if c == IAC and i + 1 < len(d):
                cmd = d[i+1]
                if cmd in (DO, DONT, WILL, WONT) and i + 2 < len(d):
                    opt = d[i+2]
                    s.sendall(bytes([IAC, WONT if cmd in (DO, DONT) else DONT, opt]))
                    i += 3; continue
                if cmd == SB:
                    j = d.find(bytes([IAC, SE]), i); i = (j + 2) if j >= 0 else len(d); continue
                i += 2; continue
            out.append(c); i += 1
        buf += bytes(out)
        if any(u in buf for u in until): break
    return buf
def main():
    cmd = sys.argv[1]; to = float(sys.argv[2]) if len(sys.argv) > 2 else 30
    s = socket.create_connection((HOST, PORT), timeout=15)
    b = recv(s, [b'ogin:'], 20)
    s.sendall(b'root\r\n')
    b = recv(s, [b'# ', b'assword:', b'TERM', b'terminal type'], 25)
    if b'assword:' in b:
        import os
        pw = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '.asvpass')).read().strip()
        s.sendall(pw.encode() + b'\r\n'); b = recv(s, [b'# ', b'terminal type', b'ogin incorrect'], 20)
        if b'ogin incorrect' in b: print('LOGIN FAILED'); sys.exit(98)
    if b'terminal type' in b: s.sendall(b'vt100\r\n'); b = recv(s, [b'# '], 20)
    mark = 'XX%dXX' % int(time.time())
    s.sendall(('%s; echo %s$?\r\n' % (cmd, mark)).encode())
    b = recv(s, [(mark + str(n)).encode() for n in range(0, 256)][:0] or [mark.encode() + b'0', mark.encode() + b'1', mark.encode() + b'2'], to)
    b += recv(s, [b'# '], 3)
    txt = b.decode('latin1').replace('\r', '')
    first = txt.split('\n', 1)[0]
    if 'echo ' + mark in first:                                  # drop the echoed command line only
        txt = txt.split('\n', 1)[1] if '\n' in txt else ''
    m = re.search(re.escape(mark) + r'(\d+)', txt)
    print(txt[:m.start()] if m else txt, end='')
    s.sendall(b'exit\r\n'); s.close()
    sys.exit(int(m.group(1)) if m else 99)
main()
