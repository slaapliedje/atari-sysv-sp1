/*
 * asvhost.h - forced into every iserver source (-include) by build.sh:
 * what the Atari System V / AMIX build needs that the INMOS sources
 * only provide for other compilers.
 */
/* what SP.GETENV answers when the variable is not in the environment
 * (the Atari build's fallbacks; defined in lnktlk.c) */
extern char *ibsize;		/* IBOARDSIZE: the root transputer's memory */
extern char *isearch;		/* ISEARCH: where the tools' libraries are */
extern char *iccarg;		/* ICCARG_TODO */
