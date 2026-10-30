# OSMENU.PRG: a boot menu for the Atari TT, Falcon030, Mega STE and ST,
# the first program in C:\AUTO.  A line on the console, a countdown, one
# key (a digit; Return takes the default):
#
#     Boot:  1 TOS   2 MagiC   3 FreeMiNT   4 System V
#            (MagiC in 5 s)
#
#   tos       returns; the AUTO folder goes on as plain TOS
#   magic     runs \MAGXBOOT.PRG (magic= names another): MagiC re-runs
#             the AUTO folder itself, and this program sees the MagX
#             cookie then and returns at once
#   mint      leaves the kernel's path (mint=) in \AUTO\OSMENU.OUT for
#             MINTBOOT.PRG, last in AUTO, which runs it after the rest
#             of the folder (the VDI, NVDI or another, loads first)
#   sysv      Atari System V, NetBSD, Linux: the NVRAM boot preference
#   netbsd    (0x40, 0x20, 0x10) is set for the root-sector loader of
#   linux     the hard disk, and the program says to switch the machine
#             off and on (the loader is handed the preference at power-up:
#             on a TT a reset does not get there), waits for a key and
#             goes on with TOS.  The machine then boots that system at
#             every power-up until the preference is set back to TOS
#             (setboot under System V, bootpref under NetBSD).
#
# \AUTO\OSMENU.INF, plain text, one setting a line:
#   menu=tos,magic,mint,sysv     the entries, in this order (also netbsd,
#                                linux; unix = sysv, freemint = mint)
#   default=magic                taken on Return or when the time is up
#   timeout=5                    seconds (at most 99); 0: the default at once
#   mint=C:\MINT\MINT030.PRG     the FreeMiNT kernel, or a loader for it
#   magic=C:\MAGXBOOT.PRG        MagiC's booter (\MAGXBOOT.PRG if not given)
# Without menu= the entries are TOS, MagiC when its booter is there and
# FreeMiNT when its kernel and \AUTO\MINTBOOT.PRG are there.  The NVRAM
# entries are left out on a machine without NVRAM (NVMaccess answers
# with its opcode).  A MagiC preference in the NVRAM (0x08, BOOTCONF's
# MagiC button) makes MagiC the default.  With one entry there is no
# menu: it is taken at once.
#
# Build: build.sh (the SVR4 m68k GNU as: & immediates, # comments).
# All data is reached through a4 (variables) or pc-relative (constants):
# the program has no relocation table.

	.set	o_menu,0		# the entries' kinds, 0..5 (see words)
	.set	o_nmenu,6
	.set	o_dkind,7		# default=: a kind, -1 none
	.set	o_dflt,8		# the default's index in the menu
	.set	o_timeout,10
	.set	o_lastsec,12
	.set	o_hasnv,14		# NVMaccess is there
	.set	o_magicpref,15		# the NVRAM preference is MagiC's
	.set	o_nvbuf,16
	.set	o_mintpath,18
	.set	o_magxpath,98
	.set	o_linebuf,178
	.set	o_infbuf,306

	.text
start:
	move.l	4(%sp),%a5		# basepage
	move.l	12(%a5),%d0
	add.l	20(%a5),%d0
	add.l	28(%a5),%d0
	add.l	&0x100+2048+1,%d0	# the basepage, and a stack of its own: the
	and.w	&-2,%d0			# one TOS gave is at the TPA's top, which
	move.l	%a5,%sp			# Mshrink hands back (a program started
	add.l	%d0,%sp			# next would be loaded over it)
	move.l	%d0,-(%sp)
	move.l	%a5,-(%sp)
	clr.w	-(%sp)
	move.w	&0x4a,-(%sp)		# Mshrink
	trap	&1
	lea	12(%sp),%sp
	lea	vars(%pc),%a4

	pea	cookie_magx(%pc)	# already under MagiC (its second AUTO pass)?
	move.w	&38,-(%sp)		# Supexec
	trap	&14
	addq.l	&6,%sp
	tst.l	%d0
	bne	quit

	bsr	readinf			# menu, default, timeout, paths

	pea	o_nvbuf(%a4)		# the NVRAM preference
	move.w	&2,-(%sp)
	clr.w	-(%sp)
	clr.w	-(%sp)
	move.w	&46,-(%sp)		# NVMaccess read
	trap	&14
	lea	12(%sp),%sp
	tst.l	%d0
	bgt.s	1f			# 0x2e, its opcode: no NVRAM here
	st	o_hasnv(%a4)
	tst.l	%d0
	bne.s	1f			# an error: the contents are not looked at
	cmp.b	&0x08,o_nvbuf+1(%a4)
	bne.s	1f
	st	o_magicpref(%a4)
1:
	tst.b	o_nmenu(%a4)
	bne.s	2f
	bsr	automenu
2:	bsr	dropnv
	tst.b	o_nmenu(%a4)
	beq	quit

	moveq	&0,%d3			# the default: the first entry,
	move.b	o_dkind(%a4),%d0
	bsr	findkind
	bmi.s	3f
	move.w	%d1,%d3			# or default=,
3:	tst.b	o_magicpref(%a4)
	beq.s	4f
	moveq	&1,%d0			# or MagiC for MagiC's preference
	bsr	findkind
	bmi.s	4f
	move.w	%d1,%d3
4:	move.w	%d3,o_dflt(%a4)

	cmp.b	&1,o_nmenu(%a4)
	beq	choose			# one entry: no menu
	tst.w	o_timeout(%a4)
	beq	choose			# no menu: the default right away

	lea	o_linebuf(%a4),%a1	# "Boot:  1 TOS   2 MagiC ..."
	lea	s_boot(%pc),%a0
	bsr	cpy
	lea	o_menu(%a4),%a3
	moveq	&0,%d4
5:	move.b	&' ',(%a1)+
	move.b	&' ',(%a1)+
	move.b	%d4,%d0
	add.b	&0x31,%d0
	move.b	%d0,(%a1)+
	move.b	&' ',(%a1)+
	moveq	&0,%d0
	move.b	(%a3)+,%d0
	bsr	label
	bsr	cpy
	move.b	&' ',(%a1)+
	addq.b	&1,%d4
	cmp.b	o_nmenu(%a4),%d4
	bcs.s	5b
	move.b	&13,(%a1)+
	move.b	&10,(%a1)+
	clr.b	(%a1)
	pea	o_linebuf(%a4)
	move.w	&9,-(%sp)		# Cconws
	trap	&1
	addq.l	&6,%sp

	pea	hz200(%pc)
	move.w	&38,-(%sp)
	trap	&14
	addq.l	&6,%sp
	move.l	%d0,%d7			# start tick
	move.w	o_timeout(%a4),%d0
	mulu	&200,%d0
	add.l	%d0,%d7			# deadline
	move.w	&-1,o_lastsec(%a4)
poll:
	pea	hz200(%pc)
	move.w	&38,-(%sp)
	trap	&14
	addq.l	&6,%sp
	move.l	%d7,%d1
	sub.l	%d0,%d1			# ticks left
	ble	choose			# time is up: the default
	add.l	&199,%d1
	divu	&200,%d1		# seconds left
	cmp.w	o_lastsec(%a4),%d1
	beq.s	nokey
	move.w	%d1,o_lastsec(%a4)
	bsr	showline		# "(MagiC in N s)"
nokey:
	move.w	&11,-(%sp)		# Cconis
	trap	&1
	addq.l	&2,%sp
	tst.w	%d0
	beq.s	poll
	move.w	&8,-(%sp)		# Cnecin
	trap	&1
	addq.l	&2,%sp
	cmp.b	&13,%d0			# Return: the default
	beq.s	choose
	sub.b	&0x31,%d0		# '1'.. : an entry
	cmp.b	o_nmenu(%a4),%d0
	bcc.s	poll
	and.w	&0xff,%d0
	move.w	%d0,o_dflt(%a4)
choose:
	pea	crlf(%pc)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	lea	o_menu(%a4),%a0
	add.w	o_dflt(%a4),%a0
	moveq	&0,%d3
	move.b	(%a0),%d3		# the kind
	beq.s	quit			# TOS
	cmp.b	&1,%d3
	beq.s	magic
	cmp.b	&2,%d3
	beq	mint
	bra	nvpref
quit:
	clr.w	-(%sp)
	trap	&1			# Pterm0: TOS

magic:
	clr.l	-(%sp)
	pea	cmd(%pc)
	pea	o_magxpath(%a4)
	clr.w	-(%sp)
	move.w	&0x4b,-(%sp)		# Pexec
	trap	&1
	lea	16(%sp),%sp
	pea	s_nomagic(%pc)		# only back when MagiC did not start
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	pea	o_magxpath(%a4)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	pea	crlf(%pc)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	bra.s	quit

mint:
	tst.b	o_mintpath(%a4)
	beq.s	1f
	lea	mintboot(%pc),%a0
	bsr	exists
	bne.s	2f
1:	pea	s_nomint(%pc)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	bra.s	quit
2:	clr.w	-(%sp)
	pea	outname(%pc)
	move.w	&0x3c,-(%sp)		# Fcreate \AUTO\OSMENU.OUT
	trap	&1
	addq.l	&8,%sp
	tst.l	%d0
	bmi.s	quit
	move.w	%d0,%d6
	lea	o_mintpath(%a4),%a0
	moveq	&-1,%d1
3:	addq.l	&1,%d1
	tst.b	(%a0)+
	bne.s	3b
	pea	o_mintpath(%a4)
	move.l	%d1,-(%sp)
	move.w	%d6,-(%sp)
	move.w	&0x40,-(%sp)		# Fwrite
	trap	&1
	lea	12(%sp),%sp
	move.w	%d6,-(%sp)
	move.w	&0x3e,-(%sp)		# Fclose
	trap	&1
	addq.l	&4,%sp
	bra	quit

nvpref:					# d3: sysv, netbsd, linux
	lea	nvvals(%pc),%a0
	clr.b	o_nvbuf(%a4)
	move.b	(%a0,%d3.w),o_nvbuf+1(%a4)
	pea	o_nvbuf(%a4)
	move.w	&2,-(%sp)
	clr.w	-(%sp)
	move.w	&1,-(%sp)
	move.w	&46,-(%sp)		# NVMaccess write
	trap	&14
	lea	12(%sp),%sp
	tst.l	%d0
	bne.s	1f
	pea	s_pref1(%pc)
	move.w	&9,-(%sp)		# Cconws
	trap	&1
	addq.l	&6,%sp
	move.w	%d3,%d0
	bsr	label
	move.l	%a0,-(%sp)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	pea	s_pref2(%pc)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	move.w	&8,-(%sp)		# Cnecin: a key, then TOS
	trap	&1
	addq.l	&2,%sp
	bra	quit
1:	pea	s_nonvw(%pc)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	bra	quit

# the menu's second line: "(NAME in N s)"
showline:
	lea	o_linebuf(%a4),%a1
	lea	s_line(%pc),%a0
	bsr	cpy
	lea	o_menu(%a4),%a0
	add.w	o_dflt(%a4),%a0
	moveq	&0,%d0
	move.b	(%a0),%d0
	bsr	label
	bsr	cpy
	lea	s_in(%pc),%a0
	bsr	cpy
	move.w	o_lastsec(%a4),%d0
	ext.l	%d0
	divu	&10,%d0
	tst.w	%d0
	beq.s	1f
	add.b	&'0',%d0
	move.b	%d0,(%a1)+
1:	swap	%d0
	add.b	&'0',%d0
	move.b	%d0,(%a1)+
	lea	s_secs(%pc),%a0
	bsr	cpy
	pea	o_linebuf(%a4)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	rts

# cpy: the string at a0 to a1; a1 is left on its terminator
cpy:	move.b	(%a0)+,(%a1)+
	bne.s	cpy
	subq.l	&1,%a1
	rts

# label: kind d0 (a word) -> a0 its name for the screen
label:	lea	labels(%pc),%a0
	lsl.w	&4,%d0
	add.w	%d0,%a0
	rts

# exists: is there a file at the path a0?  Z clear when there is
exists:	clr.w	-(%sp)
	move.l	%a0,-(%sp)
	move.w	&0x3d,-(%sp)		# Fopen
	trap	&1
	addq.l	&8,%sp
	tst.l	%d0
	bmi.s	1f
	move.w	%d0,-(%sp)
	move.w	&0x3e,-(%sp)		# Fclose
	trap	&1
	addq.l	&4,%sp
	moveq	&1,%d0
	rts
1:	moveq	&0,%d0
	rts

# addkind: kind d0 to the end of the menu, unless it is in already
addkind:
	moveq	&0,%d1
	move.b	o_nmenu(%a4),%d1
	lea	o_menu(%a4),%a0
	move.w	%d1,%d2
	bra.s	2f
1:	cmp.b	(%a0)+,%d0
	beq.s	3f
2:	dbf	%d2,1b
	cmp.w	&6,%d1
	bcc.s	3f
	move.b	%d0,(%a0)
	addq.b	&1,o_nmenu(%a4)
3:	rts

# findkind: kind d0 -> d1 its index in the menu, -1 (N set) if not in it
findkind:
	lea	o_menu(%a4),%a0
	moveq	&0,%d2
	move.b	o_nmenu(%a4),%d2
	moveq	&0,%d1
	bra.s	2f
1:	cmp.b	(%a0)+,%d0
	beq.s	3f
	addq.w	&1,%d1
2:	cmp.w	%d2,%d1
	blt.s	1b
	moveq	&-1,%d1
3:	tst.w	%d1
	rts

# automenu: no menu= line; TOS, and MagiC and FreeMiNT when they are there
automenu:
	moveq	&0,%d0
	bsr	addkind
	lea	o_magxpath(%a4),%a0
	bsr	exists
	beq.s	1f
	moveq	&1,%d0
	bsr	addkind
1:	tst.b	o_mintpath(%a4)
	beq.s	2f
	lea	o_mintpath(%a4),%a0
	bsr	exists
	beq.s	2f
	lea	mintboot(%pc),%a0
	bsr	exists
	beq.s	2f
	moveq	&2,%d0
	bsr	addkind
2:	rts

# dropnv: without NVRAM the NVRAM entries go
dropnv:	tst.b	o_hasnv(%a4)
	bne.s	3f
	lea	o_menu(%a4),%a0
	move.l	%a0,%a1
	moveq	&0,%d1
	move.b	o_nmenu(%a4),%d1
	moveq	&0,%d2
	bra.s	2f
1:	move.b	(%a0)+,%d0
	cmp.b	&3,%d0
	bcc.s	2f
	move.b	%d0,(%a1)+
	addq.b	&1,%d2
2:	dbf	%d1,1b
	move.b	%d2,o_nmenu(%a4)
3:	rts

# OSMENU.INF: menu=, default=, timeout=, mint=, magic=
readinf:
	clr.w	-(%sp)
	pea	infname(%pc)
	move.w	&0x3d,-(%sp)		# Fopen
	trap	&1
	addq.l	&8,%sp
	tst.l	%d0
	bmi	rinf_x
	move.w	%d0,%d6
	pea	o_infbuf(%a4)
	move.l	&512,-(%sp)
	move.w	%d6,-(%sp)
	move.w	&0x3f,-(%sp)		# Fread
	trap	&1
	lea	12(%sp),%sp
	move.l	%d0,%d5
	move.w	%d6,-(%sp)
	move.w	&0x3e,-(%sp)		# Fclose
	trap	&1
	addq.l	&4,%sp
	tst.l	%d5
	bmi	rinf_x
	lea	o_infbuf(%a4),%a6
	clr.b	(%a6,%d5.w)		# terminator
rline:	tst.b	(%a6)
	beq	rinf_x
	move.l	%a6,%a1
	lea	k_menu(%pc),%a3
	bsr	match
	beq.s	r_menu
	move.l	%a6,%a1
	lea	k_default(%pc),%a3
	bsr	match
	beq.s	r_default
	move.l	%a6,%a1
	lea	k_timeout(%pc),%a3
	bsr	match
	beq.s	r_timeout
	move.l	%a6,%a1
	lea	k_mint(%pc),%a3
	bsr	match
	bne.s	1f
	lea	o_mintpath(%a4),%a3
	bra.s	r_path
1:	move.l	%a6,%a1
	lea	k_magic(%pc),%a3
	bsr	match
	bne.s	rnext
	lea	o_magxpath(%a4),%a3
	bra.s	r_path
rnext:	move.b	(%a6)+,%d0		# to the next line
	beq.s	rinf_x
	cmp.b	&10,%d0
	bne.s	rnext
	bra.s	rline
r_menu:	bsr	word			# names, split by commas or blanks
	tst.l	%d0
	bmi.s	1f
	bsr	addkind
1:	move.b	(%a1),%d0
	cmp.b	&',',%d0
	beq.s	2f
	cmp.b	&' ',%d0
	beq.s	r_menu
	cmp.b	&9,%d0
	beq.s	r_menu
	bra.s	rnext
2:	addq.l	&1,%a1
	bra.s	r_menu
r_default:
	bsr	word
	move.b	%d0,o_dkind(%a4)
	bra.s	rnext
r_timeout:
	moveq	&0,%d1
3:	move.b	(%a1)+,%d0
	sub.b	&'0',%d0
	bcs.s	4f
	cmp.b	&9,%d0
	bhi.s	4f
	mulu	&10,%d1
	ext.w	%d0
	add.w	%d0,%d1
	cmp.w	&99,%d1
	bls.s	3b
	moveq	&99,%d1
	bra.s	3b
4:	move.w	%d1,o_timeout(%a4)
	bra.s	rnext
r_path:					# into a3, 79 characters at most
	moveq	&78,%d1
6:	move.b	(%a1)+,%d0
	cmp.b	&' ',%d0		# ends at a control char or blank
	bls.s	7f
	move.b	%d0,(%a3)+
	dbf	%d1,6b
7:	clr.b	(%a3)
	bra.s	rnext
rinf_x:	rts

# match: keyword at a3 against the text at a1 (case-blind); Z set and
# a1 after the keyword on a match
match:	move.b	(%a3)+,%d0
	beq.s	m_yes
	move.b	(%a1)+,%d1
	or.b	&0x20,%d1
	cmp.b	%d0,%d1
	beq.s	match
	moveq	&1,%d0			# clears Z
	rts
m_yes:	moveq	&0,%d0
	rts

# word: the word at a1 (after blanks; it ends at a blank, a comma or a
# control character) -> d0 its kind, or -1; a1 after it.  Uses a0 a2 a3.
word:	cmp.b	&' ',(%a1)
	beq.s	1f
	cmp.b	&9,(%a1)
	bne.s	2f
1:	addq.l	&1,%a1
	bra.s	word
2:	move.l	%a1,%a2			# the word's start
3:	move.b	(%a1),%d0
	cmp.b	&' ',%d0
	bls.s	4f
	cmp.b	&',',%d0
	beq.s	4f
	addq.l	&1,%a1
	bra.s	3b
4:	lea	words(%pc),%a3
5:	tst.b	(%a3)
	beq.s	9f			# the table's end: no such name
	move.l	%a2,%a0
6:	move.b	(%a3)+,%d0
	beq.s	7f			# the name's end
	cmp.l	%a1,%a0
	bcc.s	8f			# the word is shorter
	move.b	(%a0)+,%d1
	or.b	&0x20,%d1
	cmp.b	%d0,%d1
	beq.s	6b
8:	tst.b	(%a3)+			# to the name's end,
	bne.s	8b
	addq.l	&1,%a3			# past its kind
	bra.s	5b
7:	cmp.l	%a1,%a0
	beq.s	10f
	addq.l	&1,%a3			# the word is longer
	bra.s	5b
10:	moveq	&0,%d0
	move.b	(%a3),%d0
	rts
9:	moveq	&-1,%d0
	rts

# supervisor pieces
hz200:	move.l	0x4ba,%d0
	rts
cookie_magx:
	move.l	0x5a0,%d0
	beq.s	c_no
	move.l	%d0,%a0
c_loop:	move.l	(%a0),%d0
	beq.s	c_no
	cmp.l	&0x4d616758,%d0		# 'MagX'
	beq.s	c_yes
	addq.l	&8,%a0
	bra.s	c_loop
c_no:	moveq	&0,%d0
	rts
c_yes:	moveq	&1,%d0
	rts

# the kinds: the names OSMENU.INF uses, then the names on the screen
words:	.asciz	"tos"
	.byte	0
	.asciz	"magic"
	.byte	1
	.asciz	"mint"
	.byte	2
	.asciz	"freemint"
	.byte	2
	.asciz	"sysv"
	.byte	3
	.asciz	"unix"
	.byte	3
	.asciz	"netbsd"
	.byte	4
	.asciz	"linux"
	.byte	5
	.byte	0
labels:	.ascii	"TOS"			# 16 bytes each
	.space	13
	.ascii	"MagiC"
	.space	11
	.ascii	"FreeMiNT"
	.space	8
	.ascii	"System V"
	.space	8
	.ascii	"NetBSD"
	.space	10
	.ascii	"Linux"
	.space	11
nvvals:	.byte	0,0,0,0x40,0x20,0x10	# the NVRAM preference of each kind

s_boot:	.byte	13,10
	.ascii	"Boot:"
	.byte	0
s_line:	.byte	13
	.ascii	"       ("
	.byte	0
s_in:	.asciz	" in "
s_secs:	.asciz	" s)  "
crlf:	.byte	13,10,0
s_nomagic: .asciz "OSMENU: MagiC did not start: "
s_nomint: .ascii "OSMENU: FreeMiNT needs mint= in \\AUTO\\OSMENU.INF and"
	.byte	13,10
	.ascii	"\\AUTO\\MINTBOOT.PRG last in the AUTO folder"
	.byte	13,10,0
s_pref1: .byte	13,10
	.asciz	"OSMENU: "
s_pref2: .ascii	" is now the boot preference.  Switch the"
	.byte	13,10
	.ascii	"computer off and on to start it (on a TT a reset is not"
	.byte	13,10
	.ascii	"enough).  It then starts at every power-up, until its own"
	.byte	13,10
	.ascii	"tool sets the preference back to TOS (setboot under"
	.byte	13,10
	.ascii	"System V, bootpref under NetBSD)."
	.byte	13,10
	.ascii	"Any key goes on with TOS for now."
	.byte	13,10,0
s_nonvw: .ascii	"OSMENU: the NVRAM could not be written; going on with TOS"
	.byte	13,10,0
infname: .asciz	"\\AUTO\\OSMENU.INF"
outname: .asciz	"\\AUTO\\OSMENU.OUT"
mintboot: .asciz "\\AUTO\\MINTBOOT.PRG"
k_menu:	.asciz	"menu="
k_default: .asciz "default="
k_timeout: .asciz "timeout="
k_mint:	.asciz	"mint="
k_magic: .asciz	"magic="
cmd:	.byte	0,0
	.even
vars:	.space	6			# menu
	.byte	0			# nmenu
	.byte	0xff			# dkind: none
	.short	0			# dflt
	.short	5			# timeout
	.short	0			# lastsec
	.byte	0,0			# hasnv, magicpref
	.space	2			# nvbuf
	.space	80			# mintpath
	.asciz	"\\MAGXBOOT.PRG"	# magxpath, 80 bytes
	.space	66
	.space	128			# linebuf
	.space	514			# infbuf
