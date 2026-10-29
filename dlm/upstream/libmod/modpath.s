| modpath(2): system call 66, carry set on error; retried on ERESTART.

	.text
	.globl	_modpath
	.weak	modpath
_modpath:
modpath:
	moveq	&66,%d0
	trap	&0
	bcc.b	Lmodpath
	cmpib	&91,%d0
	beq.b	_modpath
	movel	%d0,errno		| not _cerror: its PLT binder clobbers %d0
	moveq	&-1,%d0
	rts
Lmodpath:
	rts
