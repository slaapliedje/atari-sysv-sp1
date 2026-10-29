| libmod.s -- the six module system calls for Atari System V programs.
| ASV makes system calls with trap #10, the number in %d0 and the
| arguments in %a0, %d1, %a1 (its libc stubs do the same); carry set on
| error, retried on ERESTART (91).  Upstream libmod/*.s is the AMIX form
| (trap #0, arguments on the stack).  errno is set directly, as upstream
| does, rather than through _cerror.

	.text
	.globl	_modload
	.weak	modload
_modload:
modload:
	moveq	&64,%d0
	movel	4(%sp),%a0
	trap	&10
	bcc.b	Lmodload
	cmpib	&91,%d0
	beq.b	_modload
	movel	%d0,errno
	moveq	&-1,%d0
	rts
Lmodload:
	rts

	.text
	.globl	_moduload
	.weak	moduload
_moduload:
moduload:
	moveq	&65,%d0
	movel	4(%sp),%a0
	trap	&10
	bcc.b	Lmoduload
	cmpib	&91,%d0
	beq.b	_moduload
	movel	%d0,errno
	moveq	&-1,%d0
	rts
Lmoduload:
	rts

	.text
	.globl	_modpath
	.weak	modpath
_modpath:
modpath:
	moveq	&66,%d0
	movel	4(%sp),%a0
	trap	&10
	bcc.b	Lmodpath
	cmpib	&91,%d0
	beq.b	_modpath
	movel	%d0,errno
	moveq	&-1,%d0
	rts
Lmodpath:
	rts

	.text
	.globl	_modstat
	.weak	modstat
_modstat:
modstat:
	moveq	&67,%d0
	movel	4(%sp),%a0
	movel	8(%sp),%d1
	movel	12(%sp),%a1
	trap	&10
	bcc.b	Lmodstat
	cmpib	&91,%d0
	beq.b	_modstat
	movel	%d0,errno
	moveq	&-1,%d0
	rts
Lmodstat:
	rts

	.text
	.globl	_modadm
	.weak	modadm
_modadm:
modadm:
	moveq	&68,%d0
	movel	4(%sp),%a0
	movel	8(%sp),%d1
	movel	12(%sp),%a1
	trap	&10
	bcc.b	Lmodadm
	cmpib	&91,%d0
	beq.b	_modadm
	movel	%d0,errno
	moveq	&-1,%d0
	rts
Lmodadm:
	rts

	.text
	.globl	_getksym
	.weak	getksym
_getksym:
getksym:
	moveq	&69,%d0
	movel	4(%sp),%a0
	movel	8(%sp),%d1
	movel	12(%sp),%a1
	trap	&10
	bcc.b	Lgetksym
	cmpib	&91,%d0
	beq.b	_getksym
	movel	%d0,errno
	moveq	&-1,%d0
	rts
Lgetksym:
	rts
