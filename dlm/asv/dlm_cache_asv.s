| dlm_cache_asv.s -- upstream's dlm_cache.s for Atari System V, whose
| kernel keeps the CPU's full number in cputype (68030, 68040: set by the
| start code, see start+0x536), where Ash Nazag's 040/060 port keeps 30,
| 40 or 60.  Upstream's tests (>= 60, >= 40) read 68030 as a 68060 and ran
| cpusha on a 68030: the machine reset on the first module load (Hatari,
| 2026-10-29).  Same code otherwise:
|
| Whole-cache, at IPL 7:
|   68060  cpusha bc, then CACR.CABC (clear the branch cache)
|   68040  cpusha bc (pushes dirty data lines, invalidates both caches)
|   68020/030  CACR.CD | CACR.CI
| plat_dmainit is upstream's weak default (unused on ASV: dlminit starts
| the loader).

	.text
	.weak	cputype

	.globl	dlm_cacheflush
dlm_cacheflush:
	movew	%sr,%d1
	movew	&0x2700,%sr
	clrl	%d0
	lea	cputype,%a0
	cmpal	&0,%a0
	beq.b	L1
	movel	%a0@,%d0
L1:	cmpil	&68060,%d0
	blt.b	L2
	.word	0xf4f8			| cpusha bc
	.long	0x4e7a0002		| movec %cacr,%d0
	oril	&0x00400000,%d0		| CABC
	.long	0x4e7b0002		| movec %d0,%cacr
	bra.b	L4
L2:	cmpil	&68040,%d0
	blt.b	L3
	.word	0xf4f8			| cpusha bc
	bra.b	L4
L3:	.long	0x4e7a0002		| movec %cacr,%d0
	oriw	&0x0808,%d0		| CD | CI
	.long	0x4e7b0002		| movec %d0,%cacr
L4:	movew	%d1,%sr
	rts

	.weak	plat_dmainit
	.globl	plat_dmainit
plat_dmainit:
	rts

| dlm_cachepush(pa, n): push and invalidate the data cache over physical
| [pa, pa+n), before a device reads it and again before the CPU reads
| what a device wrote.  Lines for short ranges, pages for long ones, at
| IPL 7 so nothing dirties a line between its push and invalidate.  The
| invalidate covers a 68060 whose CPUSH keeps lines (CACR.DPI).  The
| 020/030 data cache is write-through, so it is only cleared.
	.globl	dlm_cachepush
dlm_cachepush:
	movel	%sp@(4),%d0
	movel	%sp@(8),%d1
	beq.b	P9
	lea	cputype,%a0
	cmpal	&0,%a0
	beq.b	P3
	cmpil	&68040,%a0@
	blt.b	P3
	movew	%sr,%sp@-
	movew	&0x2700,%sr
	cmpil	&512,%d1
	bhi.b	P2
	addl	%d0,%d1
	andiw	&0xfff0,%d0
P1:	movel	%d0,%a0
	.word	0xf468			| cpushl dc,(%a0)
	.word	0xf448			| cinvl dc,(%a0)
	addl	&16,%d0
	cmpl	%d1,%d0
	bcs.b	P1
	bra.b	P5
P2:	addl	%d0,%d1
	andiw	&0xf000,%d0
P4:	movel	%d0,%a0
	.word	0xf470			| cpushp dc,(%a0)
	.word	0xf450			| cinvp dc,(%a0)
	addl	&4096,%d0
	cmpl	%d1,%d0
	bcs.b	P4
P5:	nop
	movew	%sp@+,%sr
	rts
P3:	.long	0x4e7a0002		| movec %cacr,%d0
	oriw	&0x0800,%d0		| CD
	.long	0x4e7b0002		| movec %d0,%cacr
P9:	rts
