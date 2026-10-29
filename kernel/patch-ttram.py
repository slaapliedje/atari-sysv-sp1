#!/usr/bin/env python3
"""patch-ttram.py KERNEL-IN KERNEL-OUT [PLATFORM-IN PLATFORM-OUT [MB]]
patch-ttram.py --linked UNIX-IN UNIX-OUT [MB]

Lift ASV's ~128 MB RAM ceiling: the page structures move from ST-RAM to
TT-RAM, and (optionally) the TT-RAM probe limit is raised.

--linked patches a kernel the two objects are already linked into: the
loader's mini kernel /stand/mUNIX, which relinks unix at boot and is never
relinked itself, so it keeps the ceiling (with 256 MB of TT-RAM it panics
before the relink writes anything, and every boot relinks again). kvm_init's
range is found by its bytes with the five relocated addresses masked, and
pdesc by the symbol table; the same 92 bytes go in, the addresses stay.

Why there is a ceiling: kvm_init (in /boot/KERNEL) carves the per-page
structures - 80 bytes a page, plus the page hash - out of physical memory
right after the kernel, which is in ST-RAM, maps them with sptalloc, and
panics ("page accounting (nextfree ...)") when they don't end inside the
4 MB of ST-RAM. That is ~21 KB of ST-RAM per MB of RAM: 128 MB was the
most a 4 MB TT could boot. (sptalloc's virtual space, the 4 MB kvseg,
would have been the next limit, at ~200 MB.)

The patch (92 bytes of kvm_init, same size, every relocation left where it
was): the structures go at the start of TT-RAM instead, and pdesc[1] - the
TT-RAM segment - is moved up past them so they are never handed out as
free pages. The kernel reaches them through TT0, which the start-up code
sets to map 0-1 GB one to one for all supervisor accesses (cacheable),
so no sptalloc and no kvseg space is needed. The ST-RAM they used to take
(~1.5-5 MB) stays free for what only ST-RAM can do: DMA buffers, video.
Needs TT-RAM (pdesc[1]) bigger than the structures: fine on any TT that
has TT-RAM; a TT without it must keep the stock KERNEL.

  kvm_init+0x392 (unix 0x1807e)      was                 now
    bra.w  PREP                      clrl -(sp) ...      (arguments gone)
    nop x3                           ...
    movel  #sptalloc,d0              jsr sptalloc        (keeps the reloc)
    movel  d1,d6                     movel d0,d6         d6 = TT-RAM start
    ...page_hash = d6 + n*80 (unchanged)...
    bra.s  L%75                      addl d2,d5          no ST-RAM taken,
                                                         no ST-RAM check
  PREP:
    lea    pdesc,a0                  (the same lea, same reloc)
    movel  a0@(28),d1                ; pdesc[1].start
    movel  d2,d0 ; lsll #8,#4,d0     ; the structures' pages -> bytes
    addl   d0,a0@(28)                ; TT-RAM segment starts after them
    subl   d0,a0@(32)                ; ... and is that much shorter
    bra.w  back to "movel d1,d6"
    nop x4, then the old (now unreachable) panic call

PLATFORM: pdesc[1]'s second word is the TT-RAM probe limit (as
patch-asv-image.py --ttram-cap sets it in whole images). MB: the new
limit; the default 256 is the most TT-RAM a TT takes.

Every changed byte is checked against the original first, and so is every
relocation in the patched range: a different KERNEL or PLATFORM is
refused, not damaged.
"""
import struct
import sys

KVM = 0x15900                  # kvm_init in /boot/KERNEL's .text
AT = KVM + 0x392               # the patched range: 92 bytes

OLD = bytes.fromhex(
    "42a7 2f05 4878 0001 2f02 4eb9 0000 0000"   # push args, jsr sptalloc
    "2c00 43f2 ac00 2009 e980 d086 23c0 0000"   # d6 = va; page_hash =
    "0000 da82 41f9 0000 0000 2010 d0a8 0004"   # d5 += pages; pdesc[0]
    "0680 0000 0fff 720c e2a8 defc 0010 b085"   # end in pages vs d5
    "62ff 0000 001a 2f05 4879 0000 0000 4878"   # bhi ok; panic(...)
    "0003 4eb9 0000 0000 defc 000c")
NEW = bytes.fromhex(
    "6000 0022 4e71 4e71 4e71 203c 0000 0000"   # bra PREP; movel #sptalloc
    "2c01 43f2 ac00 2009 e980 d086 23c0 0000"   # d6 = d1; page_hash (same)
    "0000 6038 41f9 0000 0000 2228 001c 2002"   # bra.s out; PREP: lea pdesc
    "e188 e988 d1a8 001c 91a8 0020 6000 ffd2"   # bytes; move pdesc[1]; back
    "4e71 4e71 4e71 4e71 4879 0000 0000 4878"   # nop; (dead) panic call
    "0003 4eb9 0000 0000 defc 000c")
# relocations in the range, relative to AT: kept at these offsets
RELOCS = {0x0c: "sptalloc", 0x1e: "page_hash", 0x26: "pdesc",
          0x4a: "panic string", 0x54: "cmn_err"}

PDESC = 0x0c                   # pdesc in PLATFORM's .data
TT_ENTRY = PDESC + 28          # pdesc[1]: start, limit, flags, ...


def sections(d):
    """(name, type, offset, size, link, info) of every section"""
    shoff, = struct.unpack(">I", d[0x20:0x24])
    shentsize, shnum, shstrndx = struct.unpack(">HHH", d[0x2e:0x34])
    raw = [struct.unpack(">IIIIIIIIII", d[shoff + i * shentsize:
                                         shoff + (i + 1) * shentsize])
           for i in range(shnum)]
    stroff = raw[shstrndx][4]
    out = []
    for s in raw:
        name = d[stroff + s[0]:d.index(b"\0", stroff + s[0])].decode()
        out.append((name, s[1], s[4], s[5], s[6], s[7]))
    return out


def text_and_relocs(d, want):
    secs = sections(d)
    idx = [i for i, s in enumerate(secs) if s[0] == want][0]
    rel = [s for s in secs if s[1] == 4 and s[5] == idx]   # SHT_RELA for it
    offs = []
    for s in rel:
        for i in range(0, s[3], 12):
            offs.append(struct.unpack(">I", d[s[2] + i:s[2] + i + 4])[0])
    return secs[idx][2], offs


def symbol(d, name):
    """the value of symbol NAME from .symtab, or None"""
    secs = sections(d)
    for s in secs:
        if s[1] != 2:                     # SHT_SYMTAB
            continue
        strtab = secs[s[4]][2]
        for i in range(0, s[3], 16):
            st_name, st_value = struct.unpack(">II", d[s[2] + i:s[2] + i + 8])
            if d[strtab + st_name:d.index(b"\0", strtab + st_name)] == name.encode():
                return st_value
    return None


def file_offset(d, vaddr):
    """the file offset of virtual address VADDR (in a loaded section)"""
    shoff, = struct.unpack(">I", d[0x20:0x24])
    shentsize, shnum = struct.unpack(">HH", d[0x2e:0x32])
    for i in range(shnum):
        s = struct.unpack(">IIIIIIIIII", d[shoff + i * shentsize:shoff + (i + 1) * shentsize])
        if s[1] != 8 and s[3] <= vaddr < s[3] + s[5] and s[2] & 2:   # not NOBITS, SHF_ALLOC
            return s[4] + vaddr - s[3]
    sys.exit("virtual address 0x%x is in no loaded section" % vaddr)


def patch_linked(src, dst, mb):
    """a linked kernel (mUNIX): kvm_init's bytes found with the relocated
    addresses masked, pdesc[1] through the symbol table"""
    d = bytearray(open(src, "rb").read())
    if d[:4] != b"\x7fELF" or struct.unpack(">H", d[16:18])[0] != 2:
        sys.exit("%s: not a linked ELF kernel" % src)
    slots = sorted(RELOCS)
    hits = [o for o in range(len(d) - len(OLD))
            if all(d[o + a:o + b] == OLD[a:b] for a, b in
                   zip([0] + [r + 4 for r in slots], slots + [len(OLD)]))]
    if len(hits) != 1:
        done = [o for o in range(len(d) - len(NEW))
                if all(d[o + a:o + b] == NEW[a:b] for a, b in
                       zip([0] + [r + 4 for r in slots], slots + [len(NEW)]))]
        if len(done) == 1:
            sys.exit("%s: already patched" % src)
        sys.exit("%s: kvm_init's range found %d times, expected once" % (src, len(hits)))
    o = hits[0]
    want = {"sptalloc": 0x0c, "page_hash": 0x1e, "pdesc": 0x26, "cmn_err": 0x54}
    for name, r in want.items():
        v = struct.unpack(">I", d[o + r:o + r + 4])[0]
        if symbol(d, name) != v:
            sys.exit("%s: the address at kvm_init's %s slot is 0x%x, the symbol is %s"
                     % (src, name, v, symbol(d, name)))
    new = bytearray(NEW)
    for r in slots:
        new[r:r + 4] = d[o + r:o + r + 4]
    d[o:o + len(new)] = new
    print("kvm_init's page structures now go to TT-RAM (file offset 0x%x, %d bytes)" % (o, len(new)))
    pdesc = symbol(d, "pdesc")
    if pdesc is None:
        sys.exit("%s: no pdesc symbol" % src)
    e = file_offset(d, pdesc + 28)
    start, limit, flags = struct.unpack(">III", d[e:e + 12])
    if start != 0x01000000 or flags != 0xA0000000:
        sys.exit("%s: pdesc[1] is not the TT-RAM entry (%08x %08x %08x)" % (src, start, limit, flags))
    d[e + 4:e + 8] = struct.pack(">I", mb << 20)
    print("pdesc[1]: TT-RAM limit %d MB -> %d MB" % (limit >> 20, mb))
    open(dst, "wb").write(d)


def patch_kernel(src, dst):
    d = bytearray(open(src, "rb").read())
    if d[:4] != b"\x7fELF":
        sys.exit("%s: not an ELF object" % src)
    text, relocs = text_and_relocs(d, ".text")
    o = text + AT
    if d[o:o + len(OLD)] != OLD:
        if d[o:o + len(NEW)] == NEW:
            sys.exit("%s: already patched" % src)
        sys.exit("%s: not the KERNEL this was made for (kvm_init+0x392 "
                 "reads %s)" % (src, d[o:o + 16].hex()))
    inside = sorted(r - AT for r in relocs if AT <= r < AT + len(OLD))
    if inside != sorted(RELOCS):
        sys.exit("%s: relocations in the range are at %s, expected %s"
                 % (src, [hex(x) for x in inside],
                    [hex(x) for x in sorted(RELOCS)]))
    d[o:o + len(NEW)] = NEW
    open(dst, "wb").write(d)
    print("KERNEL: kvm_init's page structures now go to TT-RAM "
          "(0x%x, %d bytes)" % (o, len(NEW)))


def patch_platform(src, dst, mb):
    d = bytearray(open(src, "rb").read())
    data, _ = text_and_relocs(d, ".data")
    o = data + TT_ENTRY
    start, limit, flags = struct.unpack(">III", d[o:o + 12])
    if start != 0x01000000 or flags != 0xA0000000:
        sys.exit("%s: pdesc[1] is not the TT-RAM entry (%08x %08x %08x)"
                 % (src, start, limit, flags))
    d[o + 4:o + 8] = struct.pack(">I", mb << 20)
    open(dst, "wb").write(d)
    print("PLATFORM: TT-RAM limit %d MB -> %d MB" % (limit >> 20, mb))


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--linked":
        if len(a) not in (3, 4):
            sys.exit(__doc__.split("\n\n")[0])
        mb = int(a[3]) if len(a) == 4 else 256
        if not 16 <= mb <= 256:
            sys.exit("TT-RAM limit: 16..256 MB")
        patch_linked(a[1], a[2], mb)
        sys.exit(0)
    if len(a) not in (2, 4, 5):
        sys.exit(__doc__.split("\n\n")[0])
    patch_kernel(a[0], a[1])
    if len(a) >= 4:
        mb = int(a[4]) if len(a) == 5 else 256
        if not 16 <= mb <= 256:
            sys.exit("TT-RAM limit: 16..256 MB")
        patch_platform(a[2], a[3], mb)
