# OSMENU.PRG: the boot menu, first program in C:\AUTO of a TOS/MagiC/
# FreeMiNT/Unix disk.  A line on the console, a countdown, one key:
#
#     Boot:  1 TOS   2 MagiC   3 FreeMiNT   4 Unix
#            (MagiC in 5 s)
#
#   TOS       returns; the AUTO folder goes on as plain TOS
#   MagiC     runs \MAGXBOOT.PRG (MagiC re-runs the AUTO folder itself;
#             this program sees the MagX cookie then and returns at once)
#   FreeMiNT  leaves the kernel's path in \AUTO\OSMENU.OUT for
#             MINTBOOT.PRG, last in AUTO, so the VDI (XVDI) loads first
#   Unix      writes "Atari System V" into the NVRAM boot preference and
#             cold-starts the TT (a warm start ignores the preference); the Unix side's rc puts TOS back so that the
#             next boot shows the menu again (one-shot, as GRUB does it)
#
# \AUTO\OSMENU.INF, plain text:  default=tos|magic|mint|unix
#                                timeout=SECONDS      (0 = no menu)
#                                mint=C:\MINT\...\MINT030.PRG
# A MagiC preference in the NVRAM (0x08: BOOTCONF.APP, setboot magic)
# makes MagiC the default whatever the file says.
#
# Build: build.sh (the SVR4 m68k GNU as: & immediates, # comments).
# All data is reached through a4 (variables) or pc-relative (constants):
# the program has no relocation table.

	.set	o_dflt,0		# 1 TOS 2 MagiC 3 FreeMiNT 4 Unix
	.set	o_timeout,2
	.set	o_lastsec,4
	.set	o_nvbuf,6
	.set	o_mintpath,10
	.set	o_linebuf,10+80
	.set	o_infbuf,10+80+64

	.text
start:
	move.l	4(%sp),%a5		# basepage
	move.l	12(%a5),%d0
	add.l	20(%a5),%d0
	add.l	28(%a5),%d0
	add.l	&0x100,%d0
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

	bsr	readinf			# default, timeout, mint path from OSMENU.INF

	pea	o_nvbuf(%a4)		# the NVRAM preference
	move.w	&2,-(%sp)
	clr.w	-(%sp)
	clr.w	-(%sp)
	move.w	&46,-(%sp)		# NVMaccess read
	trap	&14
	lea	12(%sp),%sp
	tst.l	%d0
	bne.s	nonv
	cmp.b	&0x08,o_nvbuf+1(%a4)
	bne.s	nonv
	move.b	&2,o_dflt(%a4)		# MagiC
nonv:
	tst.w	o_timeout(%a4)
	beq	choose			# no menu: the default right away

	pea	banner(%pc)
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
	sub.b	&0x31,%d0		# '1'..'4'
	cmp.b	&3,%d0
	bhi.s	poll
	ext.w	%d0
	addq.w	&1,%d0
	move.b	%d0,o_dflt(%a4)
choose:
	pea	crlf(%pc)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	moveq	&0,%d0
	move.b	o_dflt(%a4),%d0
	cmp.b	&2,%d0
	beq.s	magic
	cmp.b	&3,%d0
	beq.s	mint
	cmp.b	&4,%d0
	beq.s	unix
quit:
	clr.w	-(%sp)
	trap	&1			# Pterm0: TOS

magic:
	clr.l	-(%sp)
	pea	cmd(%pc)
	pea	magxboot(%pc)
	clr.w	-(%sp)
	move.w	&0x4b,-(%sp)		# Pexec
	trap	&1
	lea	16(%sp),%sp
	pea	nomagic(%pc)		# only back when MagiC did not start
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	bra	quit

mint:
	clr.w	-(%sp)
	pea	outname(%pc)
	move.w	&0x3c,-(%sp)		# Fcreate \AUTO\OSMENU.OUT
	trap	&1
	addq.l	&8,%sp
	tst.l	%d0
	bmi	quit
	move.w	%d0,%d6
	lea	o_mintpath(%a4),%a0
	moveq	&-1,%d1
1:	addq.l	&1,%d1
	tst.b	(%a0)+
	bne.s	1b
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

unix:
	move.b	&0x40,o_nvbuf+1(%a4)
	clr.b	o_nvbuf(%a4)
	pea	o_nvbuf(%a4)
	move.w	&2,-(%sp)
	clr.w	-(%sp)
	move.w	&1,-(%sp)
	move.w	&46,-(%sp)		# NVMaccess write: Atari System V
	trap	&14
	lea	12(%sp),%sp
	tst.l	%d0
	bne.s	nonvw
	pea	reset(%pc)
	move.w	&38,-(%sp)		# Supexec: warm reset, never back
	trap	&14
nonvw:	pea	nonvram(%pc)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	bra	quit

# the menu's second line: "(NAME in N s)"
showline:
	lea	line(%pc),%a0
	lea	o_linebuf(%a4),%a1
1:	move.b	(%a0)+,(%a1)+
	bne.s	1b
	subq.l	&1,%a1
	moveq	&0,%d0
	move.b	o_dflt(%a4),%d0
	subq.w	&1,%d0
	lsl.w	&4,%d0
	lea	names(%pc),%a0
	add.w	%d0,%a0
2:	move.b	(%a0)+,(%a1)+
	bne.s	2b
	subq.l	&1,%a1
	move.b	&' ',(%a1)+
	move.b	&'i',(%a1)+
	move.b	&'n',(%a1)+
	move.b	&' ',(%a1)+
	move.w	o_lastsec(%a4),%d0
	ext.l	%d0
	divu	&10,%d0
	tst.w	%d0
	beq.s	3f
	add.b	&'0',%d0
	move.b	%d0,(%a1)+
3:	swap	%d0
	add.b	&'0',%d0
	move.b	%d0,(%a1)+
	move.b	&' ',(%a1)+
	move.b	&'s',(%a1)+
	move.b	&')',(%a1)+
	move.b	&' ',(%a1)+
	move.b	&' ',(%a1)+
	clr.b	(%a1)
	pea	o_linebuf(%a4)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
	rts

# OSMENU.INF: default=, timeout=, mint=
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
	move.l	&510,-(%sp)
	move.w	%d6,-(%sp)
	move.w	&0x3f,-(%sp)		# Fread
	trap	&1
	lea	12(%sp),%sp
	move.w	%d6,-(%sp)
	move.w	&0x3e,-(%sp)		# Fclose
	trap	&1
	addq.l	&4,%sp
	lea	o_infbuf(%a4),%a0
	move.l	%a0,%a2
	add.l	&510,%a2
	clr.b	(%a2)			# terminator
rline:	cmp.l	%a2,%a0
	bcc	rinf_x
	move.l	%a0,%a1
	lea	k_default(%pc),%a3
	bsr	match
	beq.s	r_default
	move.l	%a0,%a1
	lea	k_timeout(%pc),%a3
	bsr	match
	beq.s	r_timeout
	move.l	%a0,%a1
	lea	k_mint(%pc),%a3
	bsr	match
	beq.s	r_mint
rnext:	move.b	(%a0)+,%d0		# to the next line
	beq	rinf_x
	cmp.b	&10,%d0
	bne.s	rnext
	bra.s	rline
r_default:
	move.b	(%a1),%d0
	or.b	&0x20,%d0
	moveq	&1,%d1
	cmp.b	&'t',%d0
	beq.s	1f
	moveq	&2,%d1
	cmp.b	&'m',%d0
	bne.s	2f
	move.b	1(%a1),%d0
	or.b	&0x20,%d0
	cmp.b	&'i',%d0		# "mint" vs "magic"
	bne.s	1f
	moveq	&3,%d1
	bra.s	1f
2:	moveq	&4,%d1
	cmp.b	&'u',%d0
	bne.s	rnext
1:	move.b	%d1,o_dflt(%a4)
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
	bra.s	3b
4:	cmp.w	&99,%d1
	bls.s	5f
	move.w	&99,%d1
5:	move.w	%d1,o_timeout(%a4)
	bra.s	rnext
r_mint:
	lea	o_mintpath(%a4),%a3
	moveq	&78,%d1
6:	move.b	(%a1)+,%d0
	cmp.b	&' ',%d0		# ends at a control char or blank
	bls.s	7f
	move.b	%d0,(%a3)+
	dbf	%d1,6b
7:	clr.b	(%a3)
	bra	rnext
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
rinf_x:	rts

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
reset:	clr.l	0x420			# memvalid, memval2, memval3: a COLD start.  After a
	clr.l	0x43a			# warm one the ROM does not hand the NVRAM preference
	clr.l	0x51a			# to the root-sector loader, which then boots TOS again
	move.l	0x4f2,%a0		# _sysbase: the ROM's entry
	jmp	(%a0)

banner:	.byte	13,10
	.ascii	"Boot:  1 TOS   2 MagiC   3 FreeMiNT   4 Unix"
	.byte	13,10,0
line:	.byte	13
	.ascii	"       ("
	.byte	0
names:	.ascii	"TOS"
	.byte	0,0,0,0,0,0,0,0,0,0,0,0,0
	.ascii	"MagiC"
	.byte	0,0,0,0,0,0,0,0,0,0,0
	.ascii	"FreeMiNT"
	.byte	0,0,0,0,0,0,0,0
	.ascii	"Unix"
	.byte	0,0,0,0,0,0,0,0,0,0,0,0
crlf:	.byte	13,10,0
nomagic: .ascii	"OSMENU: MagiC did not start (\\MAGXBOOT.PRG, \\MAGIC.RAM?)"
	.byte	13,10,0
nonvram: .ascii	"OSMENU: no NVRAM access, staying with TOS"
	.byte	13,10,0
magxboot: .asciz "\\MAGXBOOT.PRG"
infname: .asciz	"\\AUTO\\OSMENU.INF"
outname: .asciz	"\\AUTO\\OSMENU.OUT"
k_default: .asciz "default="
k_timeout: .asciz "timeout="
k_mint:	.asciz	"mint="
cmd:	.byte	0,0
	.even
vars:	.byte	1,0			# dflt: 1 TOS 2 MagiC 3 FreeMiNT 4 Unix
	.short	5			# timeout
	.short	0			# lastsec
	.space	4			# nvbuf
	.ascii	"C:\\MINT\\MINT.PRG"	# mintpath, 80 bytes
	.space	64
	.space	64			# linebuf
	.space	512			# infbuf
