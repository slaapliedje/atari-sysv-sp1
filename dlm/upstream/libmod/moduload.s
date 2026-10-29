| moduload(2): system call 65, carry set on error; retried on ERESTART.

	.text
	.globl	_moduload
	.weak	moduload
_moduload:
moduload:
	moveq	&65,%d0
	trap	&0
	bcc.b	Lmoduload
	cmpib	&91,%d0
	beq.b	_moduload
	movel	%d0,errno		| not _cerror: its PLT binder clobbers %d0
	moveq	&-1,%d0
	rts
Lmoduload:
	rts
