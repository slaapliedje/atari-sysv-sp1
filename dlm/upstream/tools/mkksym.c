/*
 * mkksym -- fill the static kernel's symbol table (dlm_ksym) in a fully
 * linked kernel image.
 *
 *	mkksym [-l] image		fill dlm_ksym in place
 *	mkksym -c [-l] image		check a filled image
 *	mkksym -x image			print the exported names
 *	mkksym -o block [-l] image	write the block to a file only
 *
 *	-l	include local symbols (debug kernels; needs a larger
 *		KSYM_SPACE)
 *
 * The table holds every defined global and weak symbol, SHN_ABS
 * included, with its run address; kh_lo and kh_hi are the values of
 * stext and end.  The block format is <sys/ksym.h>; it is built by
 * dlm_sym.c, the same code the kernel reads it with.
 *
 * -c re-reads the image, checks the block the way dlm_init does, and
 * looks every selected symbol up through the hash; it fails unless all
 * resolve to their symbol-table values and the counts agree.
 *
 * Reads the ELF file by explicit byte offsets.  K&R C.
 */

#include "dlm.h"

char	*progname = "mkksym";
char	*img;
long	imglen;
int	lflag;

/* the ELF symbol table */
char	*sh, *syms, *strs;
long	shnum, nsyms, strsz;

void
die(s, a)
	char *s, *a;
{
	fprintf(stderr, "%s: ", progname);
	fprintf(stderr, s, a);
	fprintf(stderr, "\n");
	exit(1);
}

char *
dlm_zalloc(n)
	long n;
{
	char *p = calloc(1, (size_t)(n ? n : 1));

	if (p == 0)
		die("out of memory", "");
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
readimg(path)
	char *path;
{
	FILE *f;

	if ((f = fopen(path, "rb")) == 0)
		die("cannot open %s", path);
	fseek(f, 0L, 2);
	imglen = ftell(f);
	fseek(f, 0L, 0);
	img = dlm_zalloc(imglen);
	if (fread(img, 1, (size_t)imglen, f) != (size_t)imglen)
		die("cannot read %s", path);
	fclose(f);
}

void
parse()
{
	long shoff, i, l;

	if (imglen < EHSZ || img[0] != 0x7f || img[1] != 'E' || img[2] != 'L' ||
	    img[3] != 'F' || img[4] != ELFCLASS32 || img[5] != ELFDATA2MSB ||
	    G16(img + 18) != EM_68K)
		die("not a big-endian m68k ELF32 file", "");
	shoff = G32(img + 32);
	shnum = G16(img + 48);
	if (G16(img + 46) != SHSZ || shoff + shnum * SHSZ > imglen)
		die("bad section headers", "");
	sh = img + shoff;
	for (i = 1; i < shnum; i++)
		if (G32(sh + i * SHSZ + SH_TYPE) == SHT_SYMTAB)
			break;
	if (i == shnum)
		die("no symbol table", "");
	syms = img + G32(sh + i * SHSZ + SH_OFFSET);
	nsyms = G32(sh + i * SHSZ + SH_SIZE) / SYMSZ;
	l = G32(sh + i * SHSZ + SH_LINK);
	strs = img + G32(sh + l * SHSZ + SH_OFFSET);
	strsz = G32(sh + l * SHSZ + SH_SIZE);
}

char *
symname(i)
	long i;
{
	return strs + G32(syms + i * SYMSZ + ST_NAME);
}

/* the symbols that go into the table */
int
selected(i)
	long i;
{
	char *s = syms + i * SYMSZ;
	unsigned long shx = G16(s + ST_SHNDX);
	int b = ST_BIND(s[ST_INFO]), t = ST_TYPE(s[ST_INFO]);

	if (shx == SHN_UNDEF || shx == SHN_COMMON || *symname(i) == 0)
		return 0;
	if (b == STB_GLOBAL || b == STB_WEAK)
		return 1;
	return lflag && b == STB_LOCAL && t != STT_SECTION && t != STT_FILE;
}

/* a symbol's value by name; die if missing */
unsigned long
value(name)
	char *name;
{
	long i;

	for (i = 1; i < nsyms; i++)
		if (G16(syms + i * SYMSZ + ST_SHNDX) != SHN_UNDEF &&
		    strcmp(symname(i), name) == 0)
			return G32(syms + i * SYMSZ + ST_VALUE);
	die("no symbol %s", name);
	return 0;
}

/* file offset and size of dlm_ksym */
long
space(offp)
	long *offp;
{
	long i, x;
	char *s;

	for (i = 1; i < nsyms; i++)
		if (strcmp(symname(i), "dlm_ksym") == 0 &&
		    G16(syms + i * SYMSZ + ST_SHNDX) != SHN_UNDEF)
			break;
	if (i == nsyms)
		die("no dlm_ksym in the image", "");
	s = syms + i * SYMSZ;
	x = G16(s + ST_SHNDX);
	if (x >= shnum || G32(sh + x * SHSZ + SH_TYPE) != SHT_PROGBITS)
		die("dlm_ksym is not in a PROGBITS section", "");
	*offp = G32(sh + x * SHSZ + SH_OFFSET) + G32(s + ST_VALUE) -
	    G32(sh + x * SHSZ + SH_ADDR);
	if (*offp + (long)G32(s + ST_SIZE) > imglen)
		die("dlm_ksym lies outside the file", "");
	return G32(s + ST_SIZE);
}

char *
build(sizep)
	long *sizep;
{
	long n = 1, ss = 1, i, k, so;
	char *b, *str, *e;

	for (i = 1; i < nsyms; i++)
		if (selected(i)) {
			n++;
			ss += strlen(symname(i)) + 1;
		}
	*sizep = dlm_blksize(n, ss, (long)dlm_prime(n / 4));
	b = dlm_zalloc(*sizep);
	dlm_blkinit(b, n, ss, (long)dlm_prime(n / 4), value("stext"), value("end"));
	str = b + G32(b + KH_STROFF);
	e = b + G32(b + KH_SYMOFF);
	so = 1;
	for (i = 1, k = 1; i < nsyms; i++) {
		if (!selected(i))
			continue;
		P32(e + k * SYMSZ + ST_NAME, so);
		P32(e + k * SYMSZ + ST_VALUE, G32(syms + i * SYMSZ + ST_VALUE));
		P32(e + k * SYMSZ + ST_SIZE, G32(syms + i * SYMSZ + ST_SIZE));
		e[k * SYMSZ + ST_INFO] = syms[i * SYMSZ + ST_INFO];
		P16(e + k * SYMSZ + ST_SHNDX, SHN_ABS);
		strcpy(str + so, symname(i));
		so += strlen(symname(i)) + 1;
		k++;
	}
	dlm_blkhash(b);
	fprintf(stderr, "%s: %ld symbols, %ld string bytes, %ld buckets, %ld bytes\n",
	    progname, n - 1, ss, (long)dlm_prime(n / 4), *sizep);
	return b;
}

int
check(path)
	char *path;
{
	long off, sp, i, n = 0, bad = 0;
	char *b;
	unsigned long v;
	int info;

	sp = space(&off);
	b = img + off;
	if (dlm_blkcheck(b, sp) != 0)
		die("%s: block fails the kernel's check", path);
	if (G32(b + KH_LO) != value("stext") || G32(b + KH_HI) != value("end"))
		die("%s: kh_lo/kh_hi differ from stext/end", path);
	for (i = 1; i < nsyms; i++) {
		if (!selected(i))
			continue;
		n++;
		if (!dlm_blklookup(b, symname(i), &v, &info) ||
		    v != G32(syms + i * SYMSZ + ST_VALUE)) {
			if (!lflag || ST_BIND(syms[i * SYMSZ + ST_INFO]) != STB_LOCAL) {
				fprintf(stderr, "%s: %s does not resolve\n",
				    progname, symname(i));
				bad++;
			}
		}
	}
	if (G32(b + KH_NSYM) - 1 != (unsigned long)n)
		die("%s: symbol count differs from the image", path);
	printf("%s: %ld symbols resolve, block %ld of %ld bytes\n", path,
	    n - bad, (long)G32(b + KH_SIZE), sp);
	return bad != 0;
}

void
usage()
{
	fprintf(stderr, "usage: %s [-c|-x|-o block] [-l] image\n", progname);
	exit(2);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	int c = 0, x = 0, i;
	char *out = 0, *path = 0, *b;
	long size, off, sp;
	FILE *f;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0)
			c = 1;
		else if (strcmp(argv[i], "-x") == 0)
			x = 1;
		else if (strcmp(argv[i], "-l") == 0)
			lflag = 1;
		else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			out = argv[++i];
		else if (argv[i][0] == '-' || path)
			usage();
		else
			path = argv[i];
	}
	if (path == 0)
		usage();
	readimg(path);
	parse();
	if (c)
		return check(path);
	if (x) {
		for (i = 1; i < nsyms; i++)
			if (selected((long)i))
				printf("%s\n", symname((long)i));
		return 0;
	}
	b = build(&size);
	if (out) {
		if ((f = fopen(out, "wb")) == 0 ||
		    fwrite(b, 1, (size_t)size, f) != (size_t)size || fclose(f))
			die("cannot write %s", out);
		return 0;
	}
	sp = space(&off);
	if (size > sp) {
		fprintf(stderr, "%s: block is %ld bytes, dlm_ksym has %ld\n",
		    progname, size, sp);
		return 1;
	}
	if ((f = fopen(path, "r+b")) == 0 || fseek(f, off, 0) != 0 ||
	    fwrite(b, 1, (size_t)size, f) != (size_t)size)
		die("cannot write %s", path);
	free(b);
	b = dlm_zalloc(sp - size);
	if (fwrite(b, 1, (size_t)(sp - size), f) != (size_t)(sp - size) || fclose(f))
		die("cannot write %s", path);
	return 0;
}
