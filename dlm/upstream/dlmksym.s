| dlmksym.s -- room for the static kernel's symbol table (<sys/ksym.h>).
| mkksym fills it in the final image; the size is fixed, so filling
| moves nothing.  In .data so that the relocatable image keeps its
| three sections.

	.data
	.balign	16
	.globl	dlm_ksym
	.type	dlm_ksym,@object
	.size	dlm_ksym,163840
dlm_ksym:
	.space	163840
