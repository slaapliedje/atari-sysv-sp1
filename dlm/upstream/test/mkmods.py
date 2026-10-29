#!/usr/bin/env python3
# mkmods.py -- write the assembler sources of the state-harness modules.
#
#   mkmods.py outdir
#
# Each module <p> has global <p>_load and <p>_unload (the harness binds
# behaviour to these names), a wrapper with one mod_miscops linkage, and
# a .moddata section with its dependency names.  Prints one line per
# module: "name file".
import os, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def mod(p, deps='', rev=1, text='', data='', ops='mod_miscops', pdata='0'):
    s = '''	.text
	.globl	{p}_load
	.globl	{p}_unload
{p}_load:
	moveq	&0,%d0
	rts
{p}_unload:
	moveq	&0,%d0
	rts
{text}
	.data
	.globl	{p}_wrapper
{p}_wrapper:
	.long	{rev}, {p}_load, {p}_unload, 0, 0, {p}_link
{p}_link:
	.long	{ops}, {p}_td, 0, 0
{p}_td:
	.long	{p}_desc, {pdata}
{p}_desc:
	.asciz	"{p} test module"
	.balign	4
{data}
	.section .moddata,"aw",@progbits
	.balign	4
	.long	{p}_wrapper
	.ascii	"{deps}"
	.byte	0
'''.format(p=p, deps=deps, rev=rev, text=text, data=data, ops=ops, pdata=pdata)
    open(os.path.join(out, p + '.s'), 'w').write(s)
    print(p, os.path.join(out, p + '.s'))


# a 3-chain for moduload(0): ha -> hb -> hc
mod('ha', 'hb')
mod('hb', 'hc')
mod('hc')
# a cycle
mod('cyca', 'cycb')
mod('cycb', 'cyca')
# depth: d1 -> d2 -> ... -> d9
for i in range(1, 10):
    mod('d%d' % i, 'd%d' % (i + 1) if i < 9 else '')
# wrapper revision 2
mod('rev2', rev=2)
# shadowing: shu uses bcmp; its dependency shd defines one
mod('shd', text='\t.globl\tbcmp\nbcmp:\n\trts\n')
mod('shu', 'shd', data='\t.globl\tshu_ref\nshu_ref:\n\t.long\tbcmp\n')
# transitive symbols are not searched: tra -> trb -> trc
mod('trc', data='\t.globl\ttrc_sym\ntrc_sym:\n\t.long\t0\n')
mod('trb', 'trc')
mod('tra', 'trb', data='\t.long\ttrc_sym\n')
# weak undefined resolves to 0
mod('wk', data='\t.weak\twk_none\n\t.globl\twk_ref\nwk_ref:\n\t.long\twk_none\n')
# commons: one bound to the kernel, one allocated
mod('cm', data='\t.comm\tlbolt,4\n\t.comm\tcm_own,64\n\t.globl\tcm_ref\n'
    'cm_ref:\n\t.long\tlbolt\n\t.long\tcm_own\n')
# load and unload failures, an image too large for a small dlm_maximage
mod('ldf')
mod('unf')
mod('big', data='\t.space\t4096\n')
# dependency that is a static module (adds no search entry)
mod('usest', 'stat1')
# several dependencies, all honoured, blank/tab/NUL separated
mod('multi', 'hc\\tshd  trc')
# three independent modules for the id and modstat tests
mod('m1')
mod('m2')
mod('m3')

# exec formats: xa claims 0x150 (its handler is bound by the harness);
# xb carries a magic nobody registered for it
def execmod(p, magic):
    mod(p, ops='mod_execops', pdata=p + '_xd',
        text='\t.globl\t{p}_exec\n{p}_exec:\n\tmoveq\t&0,%d0\n\trts\n'.format(p=p),
        data='{p}_xd:\n\t.word\t{m}, 0\n\t.long\t{p}_exec, 0\n\t.long\t0, 0, 0\n'.format(p=p, m=magic))


execmod('xa', 0x150)
execmod('xb', 0x151)


# hooks: hk sets h_one and h_two; hk2 also wants h_one; hk3 names none
def hookmod(p, names):
    ents = ''.join('\t.long\t{p}_n{i}, {p}_f{i}\n'.format(p=p, i=i) for i in range(len(names)))
    strs = ''.join('{p}_n{i}:\n\t.asciz\t"{n}"\n'.format(p=p, i=i, n=n) for i, n in enumerate(names))
    fns = ''.join('\t.globl\t{p}_f{i}\n{p}_f{i}:\n\trts\n'.format(p=p, i=i) for i in range(len(names)))
    mod(p, ops='mod_hookops', pdata=p + '_hd', text=fns,
        data='{p}_hd:\n{e}\t.long\t0, 0\n{s}\t.balign\t4\n'.format(p=p, e=ents, s=strs))


hookmod('hk', ['h_one', 'h_two'])
hookmod('hk2', ['h_one'])
hookmod('hk3', ['h_none'])


# character drivers: cd for major 60 (read, ioctl; no write), cdold is
# D_OLD, cdblk also asks for a block major
def drvmod(p, major, flag='0', bcount=0):
    fns = ''.join('\t.globl\t{p}_{f}\n{p}_{f}:\n\tmoveq\t&0,%d0\n\trts\n'.format(p=p, f=f)
                  for f in ('open', 'close', 'read', 'ioctl'))
    flagdef = '{p}_flag:\n\t.long\t{v}\n'.format(p=p, v=flag)
    mod(p, ops='mod_drvops', pdata=p + '_dd', text=fns,
        data=flagdef + '{p}_dd:\n\t.space\t32\n\t.long\t0, {bc}\n'
        '\t.long\t{p}_open, {p}_close, {p}_read, 0, {p}_ioctl, 0, 0, 0, 0, 0\n'
        '\t.long\t0, 0, {p}_flag\n\t.long\t{mj}, 1\n'.format(p=p, bc=bcount, mj=major))


drvmod('cd', 60)
drvmod('cdold', 61, flag='1')
drvmod('cdblk', 62, bcount=1)
