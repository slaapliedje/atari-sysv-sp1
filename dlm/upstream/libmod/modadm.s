| modadm(2): system call 68, carry set on error; retried on ERESTART.

	.text
	.globl	_modadm
	.weak	modadm
_modadm:
modadm:
	moveq	&68,%d0
	trap	&0
	bcc.b	Lmodadm
	cmpib	&91,%d0
	beq.b	_modadm
	movel	%d0,errno		| not _cerror: its PLT binder clobbers %d0
	moveq	&-1,%d0
	rts
Lmodadm:
	rts
