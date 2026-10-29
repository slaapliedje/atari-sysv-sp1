| rall.s -- asm test module using every relocation type the loader
| supports (R_68K_NONE is made by editing a copy).  abs16, absneg,
| abs8, abs8u and absm8 come from the test's extra symbol table.

	.text
	.globl	rall_code
rall_code:
	jsr	printf			| R_68K_32, kernel
	jbsr	printf			| R_68K_PC32 (bsr.l)
	jbra	rall_far		| R_68K_PC32 into .text2
	lea	rall_d+8,%a0		| R_68K_32, own .data + addend
	lea	rall_near(%pc),%a1	| R_68K_PC16
	bsr.w	rall_near		| R_68K_PC16
	movew	&abs16,%d0		| R_68K_16
	movew	&absneg,%d1		| R_68K_16, negative
	bsr.b	rall_near8		| R_68K_PC8
	rts

	.section .text2,"ax"
	.globl	rall_near8
rall_near8:
	rts
	.globl	rall_near
rall_near:
	rts
	.globl	rall_far
rall_far:
	rts

	.data
rall_d:
	.long	rall_code		| R_68K_32, own .text
	.long	lbolt+4			| R_68K_32, kernel + addend
	.long	u			| R_68K_32, kernel absolute
	.word	abs16			| R_68K_16
	.word	absneg			| R_68K_16, negative
	.byte	abs8			| R_68K_8
	.byte	abs8u			| R_68K_8, 255
	.byte	absm8			| R_68K_8, -128
	.byte	0
	.globl	rall_wrapper
rall_wrapper:
	.long	1, 0, 0, 0, 0, 0	| MODREV, no routines, no linkages
