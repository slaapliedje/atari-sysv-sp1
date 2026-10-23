# MAGXNVM.PRG: the first program in C:\AUTO on a TOS/MagiC/Unix disk.
# Reads the TT's NVRAM boot preference (XBIOS NVMaccess) and starts
# \MAGXBOOT.PRG, MagiC's booter, only when it says MagiC (0x08: what
# BOOTCONF.APP's MagiC button and `setboot magic` write).  TOS (0x80)
# and no preference run the AUTO folder on as plain TOS.
#
# The root-sector loader must boot for 0x08 at all: it boots the
# partition whose flag byte (masked 0xf8) equals the preference, so a
# partition flagged 0x09 with HDDriver's boot sector and HDDRIVER.SYS
# makes the loader go on; HDDriver then keeps C: as the boot drive.
#
# Build: tools/magxnvm/build.sh (the SVR4 m68k GNU as: & for immediates,
# # for comments; a TOS header put in front).
	.text
start:
	move.l	4(%sp),%a5		# basepage
	move.l	12(%a5),%d0		# p_tlen
	add.l	20(%a5),%d0		# p_dlen
	add.l	28(%a5),%d0		# p_blen
	add.l	&0x100,%d0
	move.l	%d0,-(%sp)
	move.l	%a5,-(%sp)
	clr.w	-(%sp)
	move.w	&0x4a,-(%sp)		# Mshrink: an AUTO program owns all memory until it does
	trap	&1
	lea	12(%sp),%sp

	pea	buf(%pc)
	move.w	&2,-(%sp)		# count
	clr.w	-(%sp)			# start: bytes 0-1 = boot preference
	clr.w	-(%sp)			# op: read
	move.w	&46,-(%sp)		# NVMaccess
	trap	&14
	lea	12(%sp),%sp
	tst.l	%d0			# 0 = read, else no NVRAM or bad checksum
	bne.s	quit
	cmp.b	&0x08,buf+1(%pc)	# MagiC?
	bne.s	quit

	clr.l	-(%sp)			# env: the parent's
	pea	cmd(%pc)
	pea	name(%pc)
	clr.w	-(%sp)			# mode 0: load and go
	move.w	&0x4b,-(%sp)		# Pexec
	trap	&1
	lea	16(%sp),%sp		# only back when MagiC did not start
quit:
	clr.w	-(%sp)
	trap	&1			# Pterm0
name:	.asciz	"\\MAGXBOOT.PRG"
cmd:	.byte	0,0
	.even
buf:	.space	4
