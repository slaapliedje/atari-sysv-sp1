/*
 * dlm_sym.c -- symbol table blocks (<sys/ksym.h> layout).
 *
 * Shared by the kernel (static table and module tables), the host test
 * harnesses and mkksym, so that the tool that writes the static table
 * and the kernel that reads it use the same code.
 *
 * K&R C.
 */

#include "dlm.h"

unsigned long
dlm_elfhash(name)
	char *name;
{
	unsigned long h = 0, g;

	while (*name) {
		h = (h << 4) + (*name++ & 0xff);
		if ((g = h & 0xf0000000L) != 0)
			h ^= g >> 24;
		h &= ~g;
		h = M32(h);
	}
	return h;
}

/* largest prime <= n, at least 67 */
int
dlm_prime(n)
	long n;
{
	long d;

	if (n < 67)
		return 67;
	for (;; n--) {
		for (d = 2; d * d <= n; d++)
			if (n % d == 0)
				break;
		if (d * d > n)
			return (int)n;
	}
}

static long
hashoff(nsym, strsize)
	long nsym, strsize;
{
	return (KSYMHDRSZ + nsym * SYMSZ + strsize + 3) & ~3L;
}

long
dlm_blksize(nsym, strsize, nbucket)
	long nsym, strsize, nbucket;
{
	return hashoff(nsym, strsize) + 4 * (2 + nbucket + nsym);
}

/*
 * Lay out an empty, zeroed block: header, the null symbol, room for
 * nsym - 1 symbols and strsize bytes of strings.
 */
void
dlm_blkinit(b, nsym, strsize, nbucket, lo, hi)
	char *b;
	long nsym, strsize, nbucket;
	unsigned long lo, hi;
{
	long h = hashoff(nsym, strsize);

	P32(b + KH_MAGIC, KSYM_MAGIC);
	P32(b + KH_VERSION, KSYM_VERSION);
	P32(b + KH_NSYM, nsym);
	P32(b + KH_SYMOFF, KSYMHDRSZ);
	P32(b + KH_STROFF, KSYMHDRSZ + nsym * SYMSZ);
	P32(b + KH_STRSIZE, strsize);
	P32(b + KH_HASHOFF, h);
	P32(b + KH_LO, lo);
	P32(b + KH_HI, hi);
	P32(b + KH_SIZE, dlm_blksize(nsym, strsize, nbucket));
	P32(b + h, nbucket);
	P32(b + h + 4, nsym);
}

/* Fill the hash from the names of symbols 1 .. nsym-1. */
void
dlm_blkhash(b)
	char *b;
{
	long nsym = G32(b + KH_NSYM), i;
	char *sym = b + G32(b + KH_SYMOFF);
	char *str = b + G32(b + KH_STROFF);
	char *h = b + G32(b + KH_HASHOFF);
	long nb = G32(h);
	char *bucket = h + 8, *chain = h + 8 + 4 * nb;
	unsigned long k;

	for (i = 1; i < nsym; i++) {
		k = dlm_elfhash(str + G32(sym + i * SYMSZ + ST_NAME)) % nb;
		P32(chain + 4 * i, G32(bucket + 4 * k));
		P32(bucket + 4 * k, i);
	}
}

/*
 * Check a block before trusting it: header, every offset and size
 * inside space, the strings NUL-terminated, every name and every
 * hash index in range.  0 if usable.
 */
int
dlm_blkcheck(b, space)
	char *b;
	long space;
{
	long nsym, so, stro, strsz, ho, nb, i, size;
	char *h;

	if (space < KSYMHDRSZ)
		return -1;
	if (G32(b + KH_MAGIC) != KSYM_MAGIC || G32(b + KH_VERSION) != KSYM_VERSION)
		return -1;
	nsym = G32(b + KH_NSYM);
	so = G32(b + KH_SYMOFF);
	stro = G32(b + KH_STROFF);
	strsz = G32(b + KH_STRSIZE);
	ho = G32(b + KH_HASHOFF);
	size = G32(b + KH_SIZE);
	if (size > space || nsym < 1 || nsym > space / SYMSZ || strsz < 1 ||
	    strsz > space || so < KSYMHDRSZ || so > space - nsym * SYMSZ ||
	    stro < so + nsym * SYMSZ || stro > space - strsz ||
	    ho < stro + strsz || (ho & 3) || ho > space - 8)
		return -1;
	if (b[stro + strsz - 1] != 0)
		return -1;
	h = b + ho;
	nb = G32(h);
	if (nb < 1 || G32(h + 4) != nsym || nb > (space - ho) / 4 ||
	    ho + 4 * (2 + nb + nsym) > size)
		return -1;
	for (i = 0; i < nsym; i++)
		if (G32(b + so + i * SYMSZ + ST_NAME) >= strsz)
			return -1;
	for (i = 0; i < nb + nsym; i++)
		if (G32(h + 8 + 4 * i) >= nsym)
			return -1;
	return 0;
}

/* Look a name up; 1 and *val, *info if found. */
int
dlm_blklookup(b, name, val, info)
	char *b, *name;
	unsigned long *val;
	int *info;
{
	char *sym = b + G32(b + KH_SYMOFF);
	char *str = b + G32(b + KH_STROFF);
	char *h = b + G32(b + KH_HASHOFF);
	long nb = G32(h), i, n = 0, nsym = G32(b + KH_NSYM);
	char *e;

	i = G32(h + 8 + 4 * (dlm_elfhash(name) % nb));
	while (i != 0 && n++ < nsym) {
		e = sym + i * SYMSZ;
		if (strcmp(str + G32(e + ST_NAME), name) == 0) {
			*val = G32(e + ST_VALUE);
			if (info)
				*info = e[ST_INFO] & 0xff;
			return 1;
		}
		i = G32(h + 8 + 4 * (nb + i));
	}
	return 0;
}

/*
 * The symbol with the greatest value <= addr (sections and file names
 * excluded).  1 and *name, *off if there is one.
 */
int
dlm_blkaddr(b, addr, name, off)
	char *b;
	unsigned long addr;
	char **name;
	unsigned long *off;
{
	char *sym = b + G32(b + KH_SYMOFF);
	char *str = b + G32(b + KH_STROFF);
	long nsym = G32(b + KH_NSYM), i, best = 0;
	unsigned long v, bv = 0;
	int t;

	for (i = 1; i < nsym; i++) {
		t = ST_TYPE(sym[i * SYMSZ + ST_INFO]);
		if (t == STT_SECTION || t == STT_FILE)
			continue;
		v = G32(sym + i * SYMSZ + ST_VALUE);
		if (v <= addr && (best == 0 || v > bv)) {
			best = i;
			bv = v;
		}
	}
	if (best == 0)
		return 0;
	*name = str + G32(sym + best * SYMSZ + ST_NAME);
	*off = addr - bv;
	return 1;
}
