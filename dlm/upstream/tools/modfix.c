/*
 * modfix -- last step of mkmod: check a linked module and give its
 * .moddata section ELF type 13 (binutils 2.8.1 cannot emit it).
 *
 *	modfix [-n] [-e exports]... [-d depmodule]... -p prefix -m name file
 *
 *	-e	file of kernel symbol names, one per line (mkksym -x)
 *	-d	a dependency module; its defined globals count as exported
 *	-n	check only, leave the type alone
 *
 * Fails, leaving the file unchanged, unless:
 *   - name is 1..14 characters;
 *   - <prefix>_wrapper is defined exactly once, globally;
 *   - there is exactly one .moddata (or type-13) section, allocated,
 *     at least 4 bytes, whose word 0 has an R_68K_32 relocation against
 *     <prefix>_wrapper with addend 0;
 *   - there is no SHT_REL section and every RELA type is 0..6;
 *   - with -e: every undefined, non-weak global is in the export list
 *     or defined by a -d module.
 *
 * Reads the ELF file by explicit byte offsets.  K&R C.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define	G8(p)	((unsigned long)((unsigned char *)(p))[0])
#define	G16(p)	(G8(p) << 8 | G8((char *)(p) + 1))
#define	G32(p)	(G16(p) << 16 | G16((char *)(p) + 2))

#define	SHSZ	40
#define	SYMSZ	16
#define	RELASZ	12

char	*progname = "modfix";
int	bad;

struct elf {
	char	*img;
	long	len;
	char	*sh, *shstr, *syms, *strs;
	long	shnum, nsyms;
	long	symsec;
};

void
die(s, a)
	char *s, *a;
{
	fprintf(stderr, "%s: ", progname);
	fprintf(stderr, s, a);
	fprintf(stderr, "\n");
	exit(1);
}

void
complain(s, a)
	char *s, *a;
{
	fprintf(stderr, "%s: ", progname);
	fprintf(stderr, s, a);
	fprintf(stderr, "\n");
	bad++;
}

void
load(e, path)
	struct elf *e;
	char *path;
{
	FILE *f;
	long i, l;

	if ((f = fopen(path, "rb")) == 0)
		die("cannot open %s", path);
	fseek(f, 0L, 2);
	e->len = ftell(f);
	fseek(f, 0L, 0);
	if ((e->img = malloc((size_t)e->len + 1)) == 0 ||
	    fread(e->img, 1, (size_t)e->len, f) != (size_t)e->len)
		die("cannot read %s", path);
	fclose(f);
	if (e->len < 52 || e->img[0] != 0x7f || e->img[1] != 'E' ||
	    e->img[4] != 1 || e->img[5] != 2 || G16(e->img + 16) != 1 ||
	    G16(e->img + 18) != 4 || G16(e->img + 46) != SHSZ)
		die("%s: not an m68k ELF32 relocatable", path);
	e->shnum = G16(e->img + 48);
	e->sh = e->img + G32(e->img + 32);
	if (G32(e->img + 32) + e->shnum * SHSZ > e->len)
		die("%s: bad section headers", path);
	e->shstr = e->img + G32(e->sh + G16(e->img + 50) * SHSZ + 16);
	e->symsec = 0;
	for (i = 1; i < e->shnum; i++)
		if (G32(e->sh + i * SHSZ + 4) == 2) {
			if (e->symsec)
				die("%s: more than one symbol table", path);
			e->symsec = i;
		}
	if (e->symsec == 0)
		die("%s: no symbol table", path);
	e->syms = e->img + G32(e->sh + e->symsec * SHSZ + 16);
	e->nsyms = G32(e->sh + e->symsec * SHSZ + 20) / SYMSZ;
	l = G32(e->sh + e->symsec * SHSZ + 24);
	e->strs = e->img + G32(e->sh + l * SHSZ + 16);
}

char *
secname(e, i)
	struct elf *e;
	long i;
{
	return e->shstr + G32(e->sh + i * SHSZ);
}

char *
symname(e, i)
	struct elf *e;
	long i;
{
	return e->strs + G32(e->syms + i * SYMSZ);
}

#define	SHNDX(e, i)	G16((e)->syms + (i) * SYMSZ + 14)
#define	BIND(e, i)	(G8((e)->syms + (i) * SYMSZ + 12) >> 4)

/* defined global or weak symbol by name */
int
defined(e, name)
	struct elf *e;
	char *name;
{
	long i;

	for (i = 1; i < e->nsyms; i++)
		if (SHNDX(e, i) != 0 && SHNDX(e, i) != 0xfff2 &&
		    (BIND(e, i) == 1 || BIND(e, i) == 2) &&
		    strcmp(symname(e, i), name) == 0)
			return 1;
	return 0;
}

char	**exports;
long	nexp, maxexp;

void
readexports(path)
	char *path;
{
	FILE *f;
	char line[512];
	long n;

	if ((f = fopen(path, "r")) == 0)
		die("cannot open %s", path);
	while (fgets(line, sizeof line, f)) {
		n = strlen(line);
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (n == 0)
			continue;
		if (nexp == maxexp) {
			maxexp = maxexp ? 2 * maxexp : 4096;
			if ((exports = realloc(exports, maxexp * sizeof (char *))) == 0)
				die("out of memory", "");
		}
		if ((exports[nexp] = malloc((size_t)n + 1)) == 0)
			die("out of memory", "");
		strcpy(exports[nexp++], line);
	}
	fclose(f);
}

int
exported(name)
	char *name;
{
	long i;

	for (i = 0; i < nexp; i++)
		if (strcmp(exports[i], name) == 0)
			return 1;
	return 0;
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	struct elf m, d[16];
	int nd = 0, nflag = 0, eflag = 0, i;
	char *prefix = 0, *name = 0, *path = 0, wrapper[256];
	long k, j, ms = 0, nwrap = 0, wsym = 0, r, n, off;
	unsigned long info;
	FILE *f;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-n") == 0)
			nflag = 1;
		else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
			readexports(argv[++i]);
			eflag = 1;
		} else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc && nd < 16)
			load(&d[nd++], argv[++i]);
		else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
			prefix = argv[++i];
		else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
			name = argv[++i];
		else if (argv[i][0] == '-' || path)
			die("usage: modfix [-n] [-e exports] [-d dep] -p prefix -m name file", "");
		else
			path = argv[i];
	}
	if (!prefix || !name || !path)
		die("usage: modfix [-n] [-e exports] [-d dep] -p prefix -m name file", "");
	if (strlen(name) < 1 || strlen(name) > 14 || strchr(name, '/'))
		complain("module name '%s' must be 1..14 characters", name);
	if (strlen(prefix) > 200)
		die("prefix too long", "");
	sprintf(wrapper, "%s_wrapper", prefix);
	load(&m, path);

	for (k = 1; k < m.nsyms; k++)
		if (strcmp(symname(&m, k), wrapper) == 0 && SHNDX(&m, k) != 0 &&
		    SHNDX(&m, k) < 0xff00) {
			nwrap++;
			wsym = k;
			if (BIND(&m, k) != 1)
				complain("%s is not global", wrapper);
		}
	if (nwrap != 1)
		complain("%s must be defined exactly once", wrapper);

	/* the type-13 section and its wrapper word */
	for (k = 1; k < m.shnum; k++) {
		if (strcmp(secname(&m, k), ".moddata") != 0 &&
		    G32(m.sh + k * SHSZ + 4) != 13)
			continue;
		if (ms)
			complain("more than one .moddata section", "");
		ms = k;
	}
	if (ms == 0)
		complain("no .moddata section", "");
	else {
		if (!(G32(m.sh + ms * SHSZ + 8) & 2))
			complain(".moddata is not allocated", "");
		if (G32(m.sh + ms * SHSZ + 20) < 4)
			complain(".moddata is shorter than 4 bytes", "");
	}

	/* relocations */
	n = 0;
	for (k = 1; k < m.shnum; k++) {
		if (G32(m.sh + k * SHSZ + 4) == 9)
			complain("SHT_REL section %s", secname(&m, k));
		if (G32(m.sh + k * SHSZ + 4) != 4)
			continue;
		off = G32(m.sh + k * SHSZ + 16);
		for (j = 0; j < (long)G32(m.sh + k * SHSZ + 20) / RELASZ; j++) {
			r = off + j * RELASZ;
			info = G32(m.img + r + 4);
			if ((info & 0xff) > 6) {
				fprintf(stderr, "%s: %s: relocation type %lu\n",
				    progname, secname(&m, k), info & 0xff);
				bad++;
			}
			if (ms && G32(m.sh + k * SHSZ + 28) == (unsigned long)ms &&
			    G32(m.img + r) == 0 && (info & 0xff) == 1 &&
			    (long)(info >> 8) == wsym && wsym &&
			    G32(m.img + r + 8) == 0)
				n++;
		}
	}
	if (ms && n != 1)
		complain(".moddata word 0 is not relocated against %s", wrapper);

	/* undefined symbols */
	if (eflag)
		for (k = 1; k < m.nsyms; k++) {
			if (SHNDX(&m, k) != 0 || BIND(&m, k) != 1 ||
			    *symname(&m, k) == 0)
				continue;
			if (exported(symname(&m, k)))
				continue;
			for (i = 0; i < nd; i++)
				if (defined(&d[i], symname(&m, k)))
					break;
			if (i == nd)
				complain("undefined: %s", symname(&m, k));
		}

	if (bad)
		return 1;
	if (nflag || G32(m.sh + ms * SHSZ + 4) == 13)
		return 0;
	/* sh_type := 13 */
	off = G32(m.img + 32) + ms * SHSZ + 4;
	if ((f = fopen(path, "r+b")) == 0 || fseek(f, off, 0) != 0 ||
	    fwrite("\0\0\0\15", 1, 4, f) != 4 || fclose(f))
		die("cannot write %s", path);
	return 0;
}
