#!/usr/bin/env python3
"""tasm - a small INMOS transputer assembler for boot-from-link programs.

    tasm.py prog.tas [-o prog.bin] [-c NAME]   (-c: emit a C byte array)

Source, one statement per line, ';' comments:
    label:              a code address
    ldc 5  /  ldc -3    direct functions take a number, a label or
    j loop              label-relative arithmetic (j/cj/call operands are
    ldc end-here        made relative to the next instruction by tasm)
    opname              an operation (rev, out, startp, ... see OPS)
    .word 0x12345678    a little-endian data word
    .byte 1,2,3         data bytes
    .align              pad to a word boundary with pfix 0 (a no-op)
Operands are prefixed with pfix/nfix as needed; sizes are iterated until
every jump is stable. The output starts with the boot length byte.
"""
import re, sys

DIRECT = dict(j=0, ldlp=1, pfix=2, ldnl=3, ldc=4, ldnlp=5, nfix=6, ldl=7,
              adc=8, call=9, cj=0xA, ajw=0xB, eqc=0xC, stl=0xD, stnl=0xE, opr=0xF)
RELATIVE = {'j', 'cj', 'call'}
OPS = dict(rev=0x00, lb=0x01, bsub=0x02, endp=0x03, diff=0x04, add=0x05, gcall=0x06,
           in_=0x07, prod=0x08, gt=0x09, wsub=0x0A, out=0x0B, sub=0x0C, startp=0x0D,
           outbyte=0x0E, outword=0x0F, seterr=0x10, resetch=0x12, csub0=0x13, stopp=0x15,
           ladd=0x16, stlb=0x17, sthf=0x18, norm=0x19, ldiv=0x1A, ldpi=0x1B, stlf=0x1C,
           xdble=0x1D, ldpri=0x1E, rem=0x1F, ret=0x20, lend=0x21, ldtimer=0x22,
           testerr=0x29, testpranal=0x2A, tin=0x2B, div=0x2C, dist=0x2E, disc=0x2F,
           xor=0x33, bcnt=0x34, lshr=0x35, lshl=0x36, lsum=0x37, lsub=0x38, runp=0x39,
           xword=0x3A, sb=0x3B, gajw=0x3C, savel=0x3D, saveh=0x3E, wcnt=0x3F, shr=0x40,
           shl=0x41, mint=0x42, alt=0x43, altwt=0x44, altend=0x45, and_=0x46, enbt=0x47,
           enbc=0x48, enbs=0x49, move=0x4A, or_=0x4B, csngl=0x4C, ccnt1=0x4D, talt=0x4E,
           ldiff=0x4F, sthb=0x50, taltwt=0x51, sum=0x52, mul=0x53, sttimer=0x54,
           stoperr=0x55, cword=0x56, clrhalterr=0x57, sethalterr=0x58, testhalterr=0x59,
           dup=0x5A, fpstnlsn=0x88, fpldnlsn=0x8E, fptesterr=0x9C, fpldzerosn=0x9F,
           lddevid=0x17C, start=0x1FF)
OPS['in'] = OPS.pop('in_'); OPS['and'] = OPS.pop('and_'); OPS['or'] = OPS.pop('or_')

def encode(fn, v):
    """fn with operand v, prefixed (Transputer Instruction Set, ch. 2)."""
    if 0 <= v < 16:
        return [fn << 4 | v]
    if v >= 16:
        return encode(DIRECT['pfix'], v >> 4) + [fn << 4 | (v & 15)]
    return encode(DIRECT['nfix'], (~v) >> 4) + [fn << 4 | (v & 15)]

def evaluate(expr, labels, here):
    env = dict(labels); env['here'] = here
    return int(eval(re.sub(r'\b0x', '0x', expr), {'__builtins__': {}}, env))

def assemble(text):
    stmts = []
    for n, line in enumerate(text.splitlines(), 1):
        line = line.split(';', 1)[0].strip()
        while ':' in line.split()[0] if line else False:
            lab, line = line.split(':', 1)
            stmts.append(('label', lab.strip(), None, n)); line = line.strip()
        if line:
            p = line.split(None, 1)
            stmts.append((p[0], p[1] if len(p) > 1 else None, None, n))
    sizes = [1] * len(stmts)
    for _ in range(50):
        labels, addr, out, pos = {}, 0, [], []
        for i, s in enumerate(stmts):
            if s[0] == 'label':
                labels[s[1]] = addr
            pos.append(addr)
            addr += 0 if s[0] == 'label' else sizes[i]
        new, code = [], []
        for i, (op, arg, _, n) in enumerate(stmts):
            here = pos[i]
            if op == 'label':
                b = []
            elif op == '.word':
                v = evaluate(arg, labels, here) & 0xFFFFFFFF
                b = [v & 255, v >> 8 & 255, v >> 16 & 255, v >> 24]
            elif op == '.byte':
                b = [evaluate(x, labels, here) & 255 for x in arg.split(',')]
            elif op == '.align':
                b = [0x20] * (-here % 4)
            elif op in DIRECT:
                v = evaluate(arg, labels, here)
                if op in RELATIVE:
                    # relative to the end of this instruction: try sizes
                    for size in range(1, 9):
                        b = encode(DIRECT[op], v - (here + size))
                        if len(b) == size:
                            break
                else:
                    b = encode(DIRECT[op], v)
            elif op in OPS:
                b = encode(DIRECT['opr'], OPS[op])
            else:
                sys.exit('line %d: unknown %s' % (n, op))
            new.append(len(b)); code += b
        if new == sizes:
            return bytes(code), labels
        sizes = new
    sys.exit('sizes did not settle')

if __name__ == '__main__':
    args = sys.argv[1:]
    src = open(args[0]).read()
    code, labels = assemble(src)
    if len(code) > 255:
        sys.exit('%d bytes: a boot program is at most 255' % len(code))
    blob = bytes([len(code)]) + code
    if '-c' in args:
        name = args[args.index('-c') + 1]
        print('/* %s, from tasm.py %s */' % (name, args[0]))
        print('static const unsigned char %s[%d] = {' % (name, len(blob)))
        for i in range(0, len(blob), 12):
            print('\t' + ', '.join('0x%02X' % c for c in blob[i:i + 12]) + ',')
        print('};')
    else:
        open(args[args.index('-o') + 1] if '-o' in args else 'a.bin', 'wb').write(blob)
