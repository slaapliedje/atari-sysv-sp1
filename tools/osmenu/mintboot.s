# MINTBOOT.PRG: last program in C:\AUTO.  When OSMENU.PRG left
# \AUTO\OSMENU.OUT behind (the FreeMiNT kernel's path, written when
# FreeMiNT was chosen), deletes it and runs that kernel, after the rest
# of the AUTO folder (the VDI first, as with NVDI).  Nothing otherwise.
	.text
start:
	move.l	4(%sp),%a5
	move.l	12(%a5),%d0
	add.l	20(%a5),%d0
	add.l	28(%a5),%d0
	add.l	&0x100+1024+1,%d0	# the basepage, and a stack of its own: the
	and.w	&-2,%d0			# one TOS gave is at the TPA's top, which
	move.l	%a5,%sp			# Mshrink hands back (a program started
	add.l	%d0,%sp			# next would be loaded over it)
	move.l	%d0,-(%sp)
	move.l	%a5,-(%sp)
	clr.w	-(%sp)
	move.w	&0x4a,-(%sp)		# Mshrink
	trap	&1
	lea	12(%sp),%sp

	clr.w	-(%sp)
	pea	outname(%pc)
	move.w	&0x3d,-(%sp)		# Fopen
	trap	&1
	addq.l	&8,%sp
	tst.l	%d0
	bmi.s	quit
	move.w	%d0,%d6
	pea	path(%pc)
	move.l	&126,-(%sp)
	move.w	%d6,-(%sp)
	move.w	&0x3f,-(%sp)		# Fread
	trap	&1
	lea	12(%sp),%sp
	move.l	%d0,%d7
	move.w	%d6,-(%sp)
	move.w	&0x3e,-(%sp)		# Fclose
	trap	&1
	addq.l	&4,%sp
	pea	outname(%pc)
	move.w	&0x41,-(%sp)		# Fdelete: a one-shot
	trap	&1
	addq.l	&6,%sp
	tst.l	%d7
	ble.s	quit
	lea	path(%pc),%a0
	add.l	%d7,%a0
	clr.b	(%a0)

	pea	msg(%pc)
	move.w	&9,-(%sp)		# Cconws
	trap	&1
	addq.l	&6,%sp
	clr.l	-(%sp)
	pea	cmd(%pc)
	pea	path(%pc)
	clr.w	-(%sp)
	move.w	&0x4b,-(%sp)		# Pexec: the kernel; no return when it boots
	trap	&1
	lea	16(%sp),%sp
	pea	failed(%pc)
	move.w	&9,-(%sp)
	trap	&1
	addq.l	&6,%sp
quit:
	clr.w	-(%sp)
	trap	&1
msg:	.ascii	"MINTBOOT: starting FreeMiNT"
	.byte	13,10,0
failed:	.ascii	"MINTBOOT: the kernel did not start"
	.byte	13,10,0
outname: .asciz	"\\AUTO\\OSMENU.OUT"
cmd:	.byte	0,0
	.even
path:	.space	128
