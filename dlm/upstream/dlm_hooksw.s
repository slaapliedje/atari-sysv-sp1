| dlm_hooksw.s -- an empty hook table, for a kernel without static
| shims; theirs replaces it.

	.data
	.weak	hooksw
hooksw:
	.long	0, 0, 0, 0
