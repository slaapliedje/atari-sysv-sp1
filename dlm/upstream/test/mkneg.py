#!/usr/bin/env python3
# mkneg.py -- damaged copies of a good module for the loader's error
# paths.
#
#   mkneg.py good outdir
#
# Prints "file expected-errno what" per variant; "0" means it must still
# load (and goes through the oracle).
import os, struct, sys

EINVAL, ERELOC = 22, 165
good, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
d0 = open(good, 'rb').read()
u16 = lambda d, o: struct.unpack('>H', d[o:o+2])[0]
u32 = lambda d, o: struct.unpack('>I', d[o:o+4])[0]
shoff, shnum, shstr = u32(d0, 32), u16(d0, 48), u16(d0, 50)


def sh(i, f):
    return u32(d0, shoff + 40 * i + f)


so = sh(shstr, 16)
names = [d0[so + sh(i, 0):d0.index(b'\0', so + sh(i, 0))].decode() for i in range(shnum)]
types = [sh(i, 4) for i in range(shnum)]
mod = types.index(13)
rela = [i for i in range(shnum) if types[i] == 4 and sh(sh(i, 28), 8) & 2]
symtab = types.index(2)
strtab = sh(symtab, 24)
nsym = sh(symtab, 20) // 16


def variant(name, errno, what, edit):
    d = bytearray(d0)
    edit(d)
    p = os.path.join(out, name)
    open(p, 'wb').write(d)
    print(p, errno, what)


def put32(d, o, v):
    d[o:o+4] = struct.pack('>I', v & 0xffffffff)


def put16(d, o, v):
    d[o:o+2] = struct.pack('>H', v)


def reltype(t):
    def e(d):
        o = sh(rela[0], 16)
        put32(d, o + 4, (u32(d0, o + 4) & ~0xff) | t)
    return e


variant('badmagic', EINVAL, 'bad ELF magic', lambda d: d.__setitem__(1, ord('X')))
variant('em386', EINVAL, 'EM_386', lambda d: put16(d, 18, 3))
variant('etexec', EINVAL, 'ET_EXEC', lambda d: put16(d, 16, 2))
variant('class64', EINVAL, 'ELFCLASS64', lambda d: d.__setitem__(4, 2))
variant('little', EINVAL, 'ELFDATA2LSB', lambda d: d.__setitem__(5, 1))
variant('no13', EINVAL, 'no type-13 section', lambda d: put32(d, shoff + 40 * mod + 4, 1))
variant('short13', EINVAL, '2-byte type-13 section', lambda d: put32(d, shoff + 40 * mod + 20, 2))
variant('noalloc13', EINVAL, 'type-13 not allocated', lambda d: put32(d, shoff + 40 * mod + 8, 0))
two = [i for i in range(1, shnum) if types[i] == 1 and sh(i, 8) & 2][0]
variant('two13', EINVAL, 'two type-13 sections', lambda d: put32(d, shoff + 40 * two + 4, 13))
variant('shtrel', ERELOC, 'SHT_REL section', lambda d: put32(d, shoff + 40 * rela[0] + 4, 9))
variant('got', ERELOC, 'R_68K_GOT32 relocation', reltype(7))
variant('plt', ERELOC, 'R_68K_PLT32 relocation', reltype(13))
variant('relative', ERELOC, 'R_68K_RELATIVE relocation', reltype(22))
variant('reltype99', ERELOC, 'unknown relocation type', reltype(99))
variant('relnone', 0, 'R_68K_NONE relocation', reltype(0))


def badsym(d):
    o = sh(rela[0], 16)
    put32(d, o + 4, ((nsym + 5) << 8) | (u32(d0, o + 4) & 0xff))


variant('badsymidx', ERELOC, 'relocation symbol index out of range', badsym)


def badoff(d):
    o = sh(rela[0], 16)
    put32(d, o, sh(sh(rela[0], 28), 20) - 1)


variant('reloffset', ERELOC, 'relocation beyond its section', badoff)


def badshndx(d):
    for k in range(1, nsym):
        o = sh(symtab, 16) + 16 * k
        if d0[o + 12] >> 4 == 1 and u16(d0, o + 14) not in (0, 0xfff1, 0xfff2):
            put16(d, o + 14, 0xff05)
            return


variant('badshndx', ERELOC, 'symbol in a reserved section index', badshndx)


def nonalloc(d):
    # the first relocation's symbol moves to a non-allocated section
    o = sh(rela[0], 16)
    k = u32(d0, o + 4) >> 8
    put16(d, sh(symtab, 16) + 16 * k + 14, shstr)


variant('nonalloc', ERELOC, 'relocation against a non-allocated section', nonalloc)
variant('twosymtab', ERELOC, 'two symbol tables',
        lambda d: put32(d, shoff + 40 * [i for i in range(1, shnum) if types[i] == 3 and i != strtab][0] + 4, 2))
variant('reltolink', ERELOC, 'RELA not linked to the symbol table',
        lambda d: put32(d, shoff + 40 * rela[0] + 24, strtab))
variant('strnul', ERELOC, 'string table not NUL-terminated',
        lambda d: d.__setitem__(sh(strtab, 16) + sh(strtab, 20) - 1, ord('x')))
variant('shentsize', EINVAL, 'bad e_shentsize', lambda d: put16(d, 46, 32))


def trunc(d):
    del d[sh(symtab, 16) + 8:]


variant('truncated', ERELOC, 'file ends inside the symbol table', trunc)
