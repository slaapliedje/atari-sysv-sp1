/*
 * dlm_ld.c -- read, lay out and relocate one ELF relocatable module.
 *
 * The caller (dlm_core.c in the kernel, the relocation oracle on the
 * host) supplies a read callback and the tables to resolve against,
 * then calls, in order:
 *
 *	dlm_ld_hdr	ELF header, section headers, section layout
 *	dlm_ld_moddata	type-13 section; dlm_ld_dep() walks its names
 *	dlm_ld_syms	symbols resolved, commons sized -> ld_imgsz
 *	(caller sets ld_img, zeroed, and ld_base)
 *	dlm_ld_image	section contents
 *	dlm_ld_reloc	relocations
 *	dlm_ld_table	the module's global symbol table
 *	dlm_ld_free	loader buffers
 *
 * Image layout: allocated PROGBITS, NOBITS and type-13 sections in
 * section-header order, each aligned to max(sh_addralign, 4), then the
 * commons the module itself defines, each aligned to its own alignment.
 *
 * K&R C.
 */

#include "dlm.h"

#define	RCHUNK	64		/* relocations read at a time */

static int
rdx(ld, off, buf, len, eshort)
	struct dlm_ld *ld;
	unsigned long off;
	char *buf;
	long len;
	int eshort;
{
	int e;

	if (len == 0)
		return 0;
	e = (*ld->ld_read)(ld, off, buf, len);
	return e == DLM_ESHORT ? eshort : e;
}

#define	SH(ld, i)	((ld)->ld_sh + (i) * SHSZ)
#define	SHW(ld, i, f)	G32(SH(ld, i) + (f))

static int
placed(type)
	unsigned long type;
{
	return type == SHT_PROGBITS || type == SHT_NOBITS || type == SHT_DLMMOD;
}

/*
 * ELF header and section headers; check them and lay out the image.
 */
int
dlm_ld_hdr(ld)
	struct dlm_ld *ld;
{
	char eh[EHSZ];
	unsigned long shoff, t, f, al, off, sz;
	int e, i, nmod = 0, nsymtab = 0;

	if ((e = rdx(ld, 0L, eh, (long)EHSZ, EINVAL)) != 0)
		return e;
	if (eh[0] != 0x7f || eh[1] != 'E' || eh[2] != 'L' || eh[3] != 'F' ||
	    eh[4] != ELFCLASS32 || eh[5] != ELFDATA2MSB ||
	    G16(eh + 16) != ET_REL || G16(eh + 18) != EM_68K)
		return EINVAL;
	shoff = G32(eh + 32);
	ld->ld_shnum = G16(eh + 48);
	if (G16(eh + 46) != SHSZ || ld->ld_shnum < 2)
		return EINVAL;
	if ((unsigned long)ld->ld_shnum * SHSZ > (unsigned long)dlm_maximage)
		return ENOMEM;
	ld->ld_sh = dlm_zalloc((long)ld->ld_shnum * SHSZ);
	ld->ld_secoff = (unsigned long *)dlm_zalloc((long)ld->ld_shnum *
	    (long)sizeof (unsigned long));
	if ((e = rdx(ld, shoff, ld->ld_sh, (long)ld->ld_shnum * SHSZ, ERELOC)) != 0)
		return e;

	/* exactly one allocated type-13 section, at least one word */
	for (i = 1; i < ld->ld_shnum; i++)
		if (SHW(ld, i, SH_TYPE) == SHT_DLMMOD &&
		    (SHW(ld, i, SH_FLAGS) & SHF_ALLOC)) {
			nmod++;
			ld->ld_modsec = i;
		}
	if (nmod != 1 || SHW(ld, ld->ld_modsec, SH_SIZE) < 4)
		return EINVAL;

	ld->ld_secoff[0] = NOSEC;
	off = 0;
	for (i = 1; i < ld->ld_shnum; i++) {
		t = SHW(ld, i, SH_TYPE);
		f = SHW(ld, i, SH_FLAGS);
		sz = SHW(ld, i, SH_SIZE);
		ld->ld_secoff[i] = NOSEC;
		if (t == SHT_REL)
			return ERELOC;
		if (t == SHT_SYMTAB) {
			nsymtab++;
			ld->ld_symsec = i;
		}
		if (t == SHT_RELA && (SHW(ld, i, SH_LINK) >= ld->ld_shnum ||
		    SHW(ld, i, SH_INFO) >= ld->ld_shnum ||
		    SHW(ld, i, SH_ENTSIZE) != RELASZ || sz % RELASZ))
			return ERELOC;
		if (!(f & SHF_ALLOC))
			continue;
		if (!placed(t))
			return ERELOC;
		al = SHW(ld, i, SH_ALIGN);
		if (al < 4)
			al = 4;
		if (al & (al - 1))
			return ERELOC;
		if (sz > (unsigned long)dlm_maximage)
			return ENOMEM;
		off = (off + al - 1) & ~(al - 1);
		ld->ld_secoff[i] = off;
		off += sz;
		if (off > (unsigned long)dlm_maximage)
			return ENOMEM;
	}
	ld->ld_comoff = ld->ld_imgsz = off;
	if (nsymtab != 1)
		return ERELOC;
	i = ld->ld_symsec;
	if (SHW(ld, i, SH_ENTSIZE) != SYMSZ || SHW(ld, i, SH_SIZE) % SYMSZ ||
	    SHW(ld, i, SH_SIZE) < SYMSZ ||
	    SHW(ld, i, SH_SIZE) > (unsigned long)dlm_maximage ||
	    SHW(ld, i, SH_LINK) == 0 || SHW(ld, i, SH_LINK) >= ld->ld_shnum ||
	    SHW(ld, SHW(ld, i, SH_LINK), SH_TYPE) != SHT_STRTAB ||
	    SHW(ld, SHW(ld, i, SH_LINK), SH_SIZE) < 1 ||
	    SHW(ld, SHW(ld, i, SH_LINK), SH_SIZE) > (unsigned long)dlm_maximage)
		return ERELOC;
	for (i = 1; i < ld->ld_shnum; i++)
		if (SHW(ld, i, SH_TYPE) == SHT_RELA &&
		    SHW(ld, i, SH_LINK) != (unsigned long)ld->ld_symsec)
			return ERELOC;
	return 0;
}

/* The type-13 section's bytes, as in the file. */
int
dlm_ld_moddata(ld)
	struct dlm_ld *ld;
{
	ld->ld_modsz = SHW(ld, ld->ld_modsec, SH_SIZE);
	ld->ld_mod = dlm_zalloc(ld->ld_modsz);
	return rdx(ld, SHW(ld, ld->ld_modsec, SH_OFFSET), ld->ld_mod,
	    ld->ld_modsz, ERELOC) ? ERELOC : 0;
}

/*
 * Next dependency name after *pos (start at 4): 1 with the name copied,
 * 0 at the end, -1 if a name is longer than MODMAXNAMELEN - 1.
 */
int
dlm_ld_dep(ld, pos, name)
	struct dlm_ld *ld;
	long *pos;
	char *name;
{
	char *p = ld->ld_mod;
	long i = *pos, n;

#define	SEP(c)	((c) == 0 || (c) == ' ' || (c) == '\t')
	while (i < ld->ld_modsz && SEP(p[i]))
		i++;
	if (i >= ld->ld_modsz) {
		*pos = i;
		return 0;
	}
	for (n = 0; i < ld->ld_modsz && !SEP(p[i]); i++, n++) {
		if (n >= MODMAXNAMELEN - 1)
			return -1;
		name[n] = p[i];
	}
	name[n] = 0;
	*pos = i;
	return 1;
#undef	SEP
}

/* own-definition hash, used while resolving */
struct ownh {
	long	*b;		/* DLM_MODBUCKETS heads, then chain[nsym] */
};

static long
ownfind(ld, oh, name)
	struct dlm_ld *ld;
	struct ownh *oh;
	char *name;
{
	long i = oh->b[dlm_elfhash(name) % DLM_MODBUCKETS];

	while (i) {
		if (strcmp(ld->ld_str + G32(ld->ld_syms + i * SYMSZ + ST_NAME),
		    name) == 0)
			return i;
		i = oh->b[DLM_MODBUCKETS + i];
	}
	return 0;
}

/*
 * Read the symbol table and resolve every symbol: own definitions,
 * then direct dependencies (last listed first), then the static
 * kernel.  Commons bind to a dependency or kernel definition, else they
 * are allocated after the sections.
 */
int
dlm_ld_syms(ld)
	struct dlm_ld *ld;
{
	struct ownh oh;
	char *s, *name;
	struct dlm_lsym *ls;
	unsigned long shx, v, al, cur, lssz;
	long i, j, o, osz;
	int e, b, found, err = 0, info;
	long ssec = ld->ld_symsec, strsec;

	strsec = SHW(ld, ssec, SH_LINK);
	ld->ld_nsym = SHW(ld, ssec, SH_SIZE) / SYMSZ;
	ld->ld_strsz = SHW(ld, strsec, SH_SIZE);
	ld->ld_syms = dlm_zalloc(ld->ld_nsym * SYMSZ);
	ld->ld_str = dlm_zalloc(ld->ld_strsz);
	lssz = ld->ld_nsym * (long)sizeof (struct dlm_lsym);
	ld->ld_ls = (struct dlm_lsym *)dlm_zalloc((long)lssz);
	if ((e = rdx(ld, SHW(ld, ssec, SH_OFFSET), ld->ld_syms,
	    ld->ld_nsym * SYMSZ, ERELOC)) != 0)
		return ERELOC;
	if ((e = rdx(ld, SHW(ld, strsec, SH_OFFSET), ld->ld_str,
	    ld->ld_strsz, ERELOC)) != 0)
		return ERELOC;
	if (ld->ld_str[ld->ld_strsz - 1] != 0)
		return ERELOC;

	osz = (DLM_MODBUCKETS + ld->ld_nsym) * (long)sizeof (long);
	oh.b = (long *)dlm_zalloc(osz);

	/* definitions */
	for (i = 1; i < ld->ld_nsym; i++) {
		s = ld->ld_syms + i * SYMSZ;
		ls = &ld->ld_ls[i];
		shx = G16(s + ST_SHNDX);
		if (G32(s + ST_NAME) >= (unsigned long)ld->ld_strsz) {
			err = ERELOC;
			goto out;
		}
		if (shx == SHN_UNDEF || shx == SHN_COMMON)
			continue;
		if (shx == SHN_ABS) {
			ls->ls_kind = LS_ABS;
			ls->ls_val = G32(s + ST_VALUE);
		} else if (shx >= SHN_LORESERVE || shx >= (unsigned long)ld->ld_shnum) {
			err = ERELOC;
			goto out;
		} else if (ld->ld_secoff[shx] != NOSEC) {
			ls->ls_kind = LS_REL;
			ls->ls_val = ld->ld_secoff[shx] + G32(s + ST_VALUE);
		} else
			continue;	/* non-allocated: ERELOC if used */
		b = ST_BIND(s[ST_INFO]);
		if (b != STB_GLOBAL && b != STB_WEAK)
			continue;
		name = ld->ld_str + G32(s + ST_NAME);
		if ((j = ownfind(ld, &oh, name)) != 0) {
			/* the same name defined twice */
			if (b == STB_GLOBAL &&
			    ST_BIND(ld->ld_syms[j * SYMSZ + ST_INFO]) == STB_GLOBAL) {
				err = ERELOC;
				goto out;
			}
			if (b == STB_WEAK) {		/* keep the first */
				*ls = ld->ld_ls[j];
				ls->ls_tab = 0;
				continue;
			}
			ld->ld_ls[j].ls_tab = 0;	/* strong replaces weak */
			ld->ld_ls[j].ls_kind = ls->ls_kind;
			ld->ld_ls[j].ls_val = ls->ls_val;
		}
		ls->ls_tab = 1;
		o = dlm_elfhash(name) % DLM_MODBUCKETS;
		oh.b[DLM_MODBUCKETS + i] = oh.b[o];
		oh.b[o] = i;
	}

	/* undefined and common */
	cur = ld->ld_comoff;
	for (i = 1; i < ld->ld_nsym; i++) {
		s = ld->ld_syms + i * SYMSZ;
		ls = &ld->ld_ls[i];
		shx = G16(s + ST_SHNDX);
		if (shx != SHN_UNDEF && shx != SHN_COMMON)
			continue;
		name = ld->ld_str + G32(s + ST_NAME);
		b = ST_BIND(s[ST_INFO]);
		found = 0;
		if (shx == SHN_UNDEF && (j = ownfind(ld, &oh, name)) != 0) {
			ls->ls_kind = ld->ld_ls[j].ls_kind;
			ls->ls_val = ld->ld_ls[j].ls_val;
			continue;
		}
		for (j = ld->ld_ndep - 1; j >= 0 && !found; j--)
			found = dlm_blklookup(ld->ld_deptab[j], name, &v, &info);
		if (!found && ld->ld_ktab)
			found = dlm_blklookup(ld->ld_ktab, name, &v, &info);
		if (found) {
			ls->ls_kind = LS_ABS;
			ls->ls_val = v;
			continue;
		}
		if (shx == SHN_COMMON) {
			al = G32(s + ST_VALUE);
			if (al == 0)
				al = 1;
			if (al & (al - 1)) {
				err = ERELOC;
				goto out;
			}
			v = G32(s + ST_SIZE);
			if (v > (unsigned long)dlm_maximage) {
				err = ENOMEM;
				goto out;
			}
			o = -1;
			if (ld->ld_comhook)
				o = (*ld->ld_comhook)(ld, name, v, al);
			if (o < 0) {
				o = (cur + al - 1) & ~(al - 1);
				cur = o + v;
			} else if ((unsigned long)o + v > cur)
				cur = o + v;
			if (cur > (unsigned long)dlm_maximage) {
				err = ENOMEM;
				goto out;
			}
			ls->ls_kind = LS_REL;
			ls->ls_val = o;
			ls->ls_tab = 1;
			continue;
		}
		if (b == STB_WEAK) {
			ls->ls_kind = LS_ABS;
			ls->ls_val = 0;
			continue;
		}
		dlm_undef(ld, name);
		ld->ld_nundef++;
	}
	ld->ld_imgsz = cur;
	if (ld->ld_nundef)
		err = ERELOC;
out:
	dlm_free((char *)oh.b, osz);
	return err;
}

/* Section contents into ld_img (zeroed by the caller). */
int
dlm_ld_image(ld)
	struct dlm_ld *ld;
{
	int i;
	unsigned long t;

	for (i = 1; i < ld->ld_shnum; i++) {
		t = SHW(ld, i, SH_TYPE);
		if (ld->ld_secoff[i] == NOSEC || t == SHT_NOBITS)
			continue;
		if (rdx(ld, SHW(ld, i, SH_OFFSET), ld->ld_img + ld->ld_secoff[i],
		    (long)SHW(ld, i, SH_SIZE), ERELOC) != 0)
			return ERELOC;
	}
	return 0;
}

/* Apply one relocation; 0 or ERELOC. */
static int
reloc1(ld, tsec, r)
	struct dlm_ld *ld;
	int tsec;
	char *r;
{
	unsigned long off = G32(r), info = G32(r + 4), a = G32(r + 8);
	unsigned long symi = info >> 8, s, p, x, sz;
	int type = info & 0xff;
	struct dlm_lsym *ls;
	char *f;
	long v;

	switch (type) {
	case R_68K_NONE:
		return 0;
	case R_68K_32: case R_68K_PC32:
		sz = 4;
		break;
	case R_68K_16: case R_68K_PC16:
		sz = 2;
		break;
	case R_68K_8: case R_68K_PC8:
		sz = 1;
		break;
	default:
		return ERELOC;
	}
	if (off > SHW(ld, tsec, SH_SIZE) || sz > SHW(ld, tsec, SH_SIZE) - off)
		return ERELOC;
	if (symi >= (unsigned long)ld->ld_nsym)
		return ERELOC;
	s = 0;
	if (symi != 0) {
		ls = &ld->ld_ls[symi];
		if (ls->ls_kind == LS_NONE)
			return ERELOC;
		s = ls->ls_val;
		if (ls->ls_kind == LS_REL)
			s += ld->ld_base;
	}
	p = ld->ld_base + ld->ld_secoff[tsec] + off;
	x = M32(s + a);
	if (type == R_68K_PC32 || type == R_68K_PC16 || type == R_68K_PC8)
		x = M32(x - p);
	v = S32(x);
	f = ld->ld_img + ld->ld_secoff[tsec] + off;
	switch (type) {
	case R_68K_32: case R_68K_PC32:
		P32(f, x);
		break;
	case R_68K_16:
		if (v < -32768L || v > 65535L)
			return ERELOC;
		P16(f, x);
		break;
	case R_68K_PC16:
		if (v < -32768L || v > 32767L)
			return ERELOC;
		P16(f, x);
		break;
	case R_68K_8:
		if (v < -128L || v > 255L)
			return ERELOC;
		P8(f, x);
		break;
	case R_68K_PC8:
		if (v < -128L || v > 127L)
			return ERELOC;
		P8(f, x);
		break;
	}
	return 0;
}

int
dlm_ld_reloc(ld)
	struct dlm_ld *ld;
{
	char *buf;
	int i, t, e = 0;
	long n, k, c, j;

	/* in ld so that an interrupted read does not leak it */
	buf = ld->ld_rbuf = dlm_zalloc((long)RCHUNK * RELASZ);
	for (i = 1; i < ld->ld_shnum && e == 0; i++) {
		if (SHW(ld, i, SH_TYPE) != SHT_RELA)
			continue;
		t = SHW(ld, i, SH_INFO);
		if (ld->ld_secoff[t] == NOSEC)
			continue;		/* debugging info */
		if (SHW(ld, t, SH_TYPE) == SHT_NOBITS) {
			e = ERELOC;
			break;
		}
		n = SHW(ld, i, SH_SIZE) / RELASZ;
		for (k = 0; k < n && e == 0; k += c) {
			c = n - k < RCHUNK ? n - k : RCHUNK;
			if (rdx(ld, SHW(ld, i, SH_OFFSET) + k * RELASZ, buf,
			    c * RELASZ, ERELOC) != 0) {
				e = ERELOC;
				break;
			}
			for (j = 0; j < c && e == 0; j++)
				e = reloc1(ld, t, buf + j * RELASZ);
		}
	}
	dlm_free(buf, (long)RCHUNK * RELASZ);
	ld->ld_rbuf = 0;
	return e;
}

/*
 * The module's table: its global definitions (and the commons it
 * allocated) with run addresses, the whole string table, 101 buckets.
 */
int
dlm_ld_table(ld)
	struct dlm_ld *ld;
{
	long i, n = 1, k;
	char *s, *e;
	struct dlm_lsym *ls;

	for (i = 1; i < ld->ld_nsym; i++)
		if (ld->ld_ls[i].ls_tab)
			n++;
	ld->ld_tabsz = dlm_blksize(n, ld->ld_strsz, (long)DLM_MODBUCKETS);
	ld->ld_tab = dlm_zalloc(ld->ld_tabsz);
	dlm_blkinit(ld->ld_tab, n, ld->ld_strsz, (long)DLM_MODBUCKETS,
	    ld->ld_base, M32(ld->ld_base + ld->ld_imgsz));
	bcopy(ld->ld_str, ld->ld_tab + G32(ld->ld_tab + KH_STROFF), ld->ld_strsz);
	e = ld->ld_tab + G32(ld->ld_tab + KH_SYMOFF);
	for (i = 1, k = 1; i < ld->ld_nsym; i++) {
		ls = &ld->ld_ls[i];
		if (!ls->ls_tab)
			continue;
		s = ld->ld_syms + i * SYMSZ;
		P32(e + k * SYMSZ + ST_NAME, G32(s + ST_NAME));
		P32(e + k * SYMSZ + ST_VALUE, M32(ls->ls_val +
		    (ls->ls_kind == LS_REL ? ld->ld_base : 0)));
		P32(e + k * SYMSZ + ST_SIZE, G32(s + ST_SIZE));
		e[k * SYMSZ + ST_INFO] = s[ST_INFO];
		P16(e + k * SYMSZ + ST_SHNDX, SHN_ABS);
		k++;
	}
	dlm_blkhash(ld->ld_tab);
	return 0;
}

void
dlm_ld_free(ld)
	struct dlm_ld *ld;
{
	if (ld->ld_sh)
		dlm_free(ld->ld_sh, (long)ld->ld_shnum * SHSZ);
	if (ld->ld_secoff)
		dlm_free((char *)ld->ld_secoff, (long)ld->ld_shnum *
		    (long)sizeof (unsigned long));
	if (ld->ld_syms)
		dlm_free(ld->ld_syms, ld->ld_nsym * SYMSZ);
	if (ld->ld_str)
		dlm_free(ld->ld_str, ld->ld_strsz);
	if (ld->ld_ls)
		dlm_free((char *)ld->ld_ls, ld->ld_nsym *
		    (long)sizeof (struct dlm_lsym));
	if (ld->ld_mod)
		dlm_free(ld->ld_mod, ld->ld_modsz);
	if (ld->ld_tab)
		dlm_free(ld->ld_tab, ld->ld_tabsz);
	if (ld->ld_rbuf)
		dlm_free(ld->ld_rbuf, (long)RCHUNK * RELASZ);
	ld->ld_rbuf = 0;
	ld->ld_sh = ld->ld_syms = ld->ld_str = ld->ld_mod = ld->ld_tab = 0;
	ld->ld_secoff = 0;
	ld->ld_ls = 0;
}
