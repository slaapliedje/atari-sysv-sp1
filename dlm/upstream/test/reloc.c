/*
 * reloc -- host driver for dlm_ld.c: load one module at a chosen run
 * address the way the kernel does, and write the image, so that it can
 * be compared with m68k-elf-ld linking the same file at the same
 * address (the relocation oracle).
 *
 *	reloc -k ksym.bin [-S extra.sym] [-D dep@base]... [-C commons]
 *	      [-o image] [-L layout] [-T table] -b base module
 *
 *	-k	static kernel table (mkksym -o, or a filled image's block)
 *	-S	extra absolute symbols, "name value" lines; searched like a
 *		dependency
 *	-D	load a dependency first at base; its table is searched
 *	-C	"name address" lines: place these commons at these run
 *		addresses (as the reference link placed them)
 *	-L	write "section address size" lines and "COMMON address size"
 *	-T	write the module's symbol table, "name value" lines
 *
 * Exit 0 on success; otherwise prints "ERR n" (the errno the kernel
 * would return) and exits 3.
 *
 * K&R C.
 */

#include "dlm.h"

long	dlm_maximage = 2 * 1024 * 1024;
int	dlm_verbose = 1;
int	dlm_def_unload_delay = 60;
int	dlm_unload_wake = 60;
char	*dlm_static[] = { 0 };

char	*deptab[32];
int	ndep;
char	*ktab;
char	*comname[4096];
unsigned long comaddr[4096];
int	ncom;
unsigned long curbase;

char *
dlm_zalloc(n)
	long n;
{
	char *p = calloc(1, (size_t)(n ? n : 1));

	if (!p) {
		fprintf(stderr, "out of memory\n");
		exit(2);
	}
	return p;
}

void
dlm_free(p, n)
	char *p;
	long n;
{
	free(p);
}

void
dlm_undef(ld, name)
	struct dlm_ld *ld;
	char *name;
{
	fprintf(stderr, "%s: undefined symbol %s\n", ld->ld_name, name);
}

static int
fread_cb(ld, off, buf, len)
	struct dlm_ld *ld;
	unsigned long off;
	char *buf;
	long len;
{
	FILE *f = (FILE *)ld->ld_rh;

	if (fseek(f, (long)off, 0) != 0)
		return DLM_ESHORT;
	if (fread(buf, 1, (size_t)len, f) != (size_t)len)
		return DLM_ESHORT;
	return 0;
}

static long
comhook(ld, name, size, al)
	struct dlm_ld *ld;
	char *name;
	unsigned long size, al;
{
	int i;

	for (i = 0; i < ncom; i++)
		if (strcmp(comname[i], name) == 0)
			return (long)(comaddr[i] - curbase);
	return -1;
}

char *
readfile(path, lenp)
	char *path;
	long *lenp;
{
	FILE *f = fopen(path, "rb");
	char *b;

	if (!f) {
		fprintf(stderr, "cannot open %s\n", path);
		exit(2);
	}
	fseek(f, 0L, 2);
	*lenp = ftell(f);
	fseek(f, 0L, 0);
	b = dlm_zalloc(*lenp);
	if (fread(b, 1, (size_t)*lenp, f) != (size_t)*lenp)
		exit(2);
	fclose(f);
	return b;
}

/* a table of absolute symbols from "name value" lines */
char *
symfile(path)
	char *path;
{
	FILE *f = fopen(path, "r");
	char name[256], *b, *e, *str;
	unsigned long v[1024];
	char *nm[1024];
	long n = 1, ss = 1, i, so = 1;

	if (!f) {
		fprintf(stderr, "cannot open %s\n", path);
		exit(2);
	}
	while (n < 1024 && fscanf(f, "%255s %lx", name, &v[n]) == 2) {
		nm[n] = strdup(name);
		ss += strlen(name) + 1;
		n++;
	}
	fclose(f);
	b = dlm_zalloc(dlm_blksize(n, ss, 67L));
	dlm_blkinit(b, n, ss, 67L, 0L, 0L);
	str = b + G32(b + KH_STROFF);
	e = b + G32(b + KH_SYMOFF);
	for (i = 1; i < n; i++) {
		P32(e + i * SYMSZ + ST_NAME, so);
		P32(e + i * SYMSZ + ST_VALUE, v[i]);
		e[i * SYMSZ + ST_INFO] = (STB_GLOBAL << 4) | STT_NOTYPE;
		P16(e + i * SYMSZ + ST_SHNDX, SHN_ABS);
		strcpy(str + so, nm[i]);
		so += strlen(nm[i]) + 1;
	}
	dlm_blkhash(b);
	return b;
}

/* load path at base: 0 and *ldp, or the errno */
int
load(path, base, ldp)
	char *path;
	unsigned long base;
	struct dlm_ld **ldp;
{
	struct dlm_ld *ld = (struct dlm_ld *)dlm_zalloc((long)sizeof *ld);
	FILE *f = fopen(path, "rb");
	int e;

	if (!f) {
		fprintf(stderr, "cannot open %s\n", path);
		exit(2);
	}
	ld->ld_read = fread_cb;
	ld->ld_rh = (char *)f;
	ld->ld_name = path;
	ld->ld_deptab = deptab;
	ld->ld_ndep = ndep;
	ld->ld_ktab = ktab;
	ld->ld_comhook = comhook;
	curbase = base;
	*ldp = ld;
	if ((e = dlm_ld_hdr(ld)) || (e = dlm_ld_moddata(ld)) ||
	    (e = dlm_ld_syms(ld)))
		return e;
	if (ld->ld_imgsz > (unsigned long)dlm_maximage)
		return ENOMEM;
	ld->ld_img = dlm_zalloc((long)ld->ld_imgsz);
	ld->ld_base = base;
	if ((e = dlm_ld_image(ld)) || (e = dlm_ld_reloc(ld)))
		return e;
	dlm_ld_table(ld);
	fclose(f);
	return 0;
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	struct dlm_ld *ld, *dl;
	char *out = 0, *lay = 0, *tabf = 0, *path = 0, *p, *nm;
	unsigned long base = 0, b;
	char name[256];
	long len, i, n;
	int e;
	FILE *f;

	for (i = 1; i < argc; i++) {
		p = argv[i];
		if (strcmp(p, "-k") == 0 && i + 1 < argc) {
			ktab = readfile(argv[++i], &len);
			if (dlm_blkcheck(ktab, len) != 0) {
				fprintf(stderr, "bad kernel table\n");
				return 2;
			}
		} else if (strcmp(p, "-S") == 0 && i + 1 < argc)
			deptab[ndep++] = symfile(argv[++i]);
		else if (strcmp(p, "-D") == 0 && i + 1 < argc) {
			p = argv[++i];
			nm = strchr(p, '@');
			if (!nm)
				return 2;
			*nm++ = 0;
			if ((e = load(p, strtoul(nm, 0, 0), &dl)) != 0) {
				printf("ERR %d (dependency %s)\n", e, p);
				return 3;
			}
			deptab[ndep++] = dl->ld_tab;
		} else if (strcmp(p, "-C") == 0 && i + 1 < argc) {
			if (!(f = fopen(argv[++i], "r")))
				return 2;
			while (ncom < 4096 && fscanf(f, "%255s %lx", name,
			    &comaddr[ncom]) == 2)
				comname[ncom++] = strdup(name);
			fclose(f);
		} else if (strcmp(p, "-o") == 0 && i + 1 < argc)
			out = argv[++i];
		else if (strcmp(p, "-L") == 0 && i + 1 < argc)
			lay = argv[++i];
		else if (strcmp(p, "-T") == 0 && i + 1 < argc)
			tabf = argv[++i];
		else if (strcmp(p, "-b") == 0 && i + 1 < argc)
			base = strtoul(argv[++i], 0, 0);
		else
			path = p;
	}
	if (!path) {
		fprintf(stderr, "usage: reloc -k ksym [-S sym] [-D dep@base] [-C com] [-o img] [-L lay] [-T tab] -b base module\n");
		return 2;
	}
	if ((e = load(path, base, &ld)) != 0) {
		printf("ERR %d\n", e);
		return 3;
	}
	if (out) {
		f = fopen(out, "wb");
		fwrite(ld->ld_img, 1, (size_t)ld->ld_imgsz, f);
		fclose(f);
	}
	if (lay) {
		f = fopen(lay, "w");
		for (i = 1; i < ld->ld_shnum; i++)
			if (ld->ld_secoff[i] != NOSEC)
				fprintf(f, "%ld 0x%lx 0x%lx\n", i,
				    base + ld->ld_secoff[i],
				    G32(ld->ld_sh + i * SHSZ + SH_SIZE));
		fprintf(f, "COMMON 0x%lx 0x%lx\n", base + ld->ld_comoff,
		    ld->ld_imgsz - ld->ld_comoff);
		fclose(f);
	}
	if (tabf) {
		f = fopen(tabf, "w");
		n = G32(ld->ld_tab + KH_NSYM);
		for (i = 1; i < n; i++) {
			p = ld->ld_tab + G32(ld->ld_tab + KH_SYMOFF) + i * SYMSZ;
			b = G32(p + ST_VALUE);
			fprintf(f, "%s 0x%lx\n", ld->ld_tab +
			    G32(ld->ld_tab + KH_STROFF) + G32(p + ST_NAME), b);
		}
		fclose(f);
	}
	printf("OK size 0x%lx\n", ld->ld_imgsz);
	return 0;
}
