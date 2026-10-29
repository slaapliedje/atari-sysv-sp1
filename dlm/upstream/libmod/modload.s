| modload(2): system call 64, carry set on error; retried on ERESTART.

	.text
	.globl	_modload
	.weak	modload
_modload:
modload:
	moveq	&64,%d0
	trap	&0
	bcc.b	Lmodload
	cmpib	&91,%d0
	beq.b	_modload
	movel	%d0,errno		| not _cerror: its PLT binder clobbers %d0
	moveq	&-1,%d0
	rts
Lmodload:
	rts
