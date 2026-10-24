/*
 * ufs.xfs - Atari System V's UFS file systems for FreeMiNT, read-only.
 *
 * The Unix side of a dual-boot TT disk is a UNX entry in the Atari root
 * sector; inside it, after the Unix boot block and the pdsector, the
 * SVR4 VTOC names the slices in absolute disk sectors.  The root, /var
 * and /home slices are UFS: the 4.3BSD fast file system as SVR4 shipped
 * it, big-endian like the 68030 this runs on, so the structures are
 * used as they lie on the disk.  HDDriver gives a UNX partition no
 * drive letter, but XHDI's XHReadWrite addresses the whole device by
 * SCSI id with absolute sectors, which is what the VTOC uses.
 *
 * At init every XHDI device is looked at; each UFS slice found becomes
 * a drive, V: onwards in slice order (UFS_FIRST_DRIVE), skipping letters
 * the BIOS already has.  The blocks go through the kernel's buffer cache
 * (bio) with a device descriptor of our own; reads only: the drive is
 * write-protected and every writing call answers EROFS.
 *
 * Written from the FFS on-disk format (McKusick et al., 1984) and the
 * structure layouts as System V's headers state them; no AT&T code.
 * The MiNT side follows FreeMiNT's ext2.xfs and skeleton.
 *
 * Copyright 2026 the Atari System V service pack 1 authors.
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 */

# include "mint/mint.h"
# include "mint/dcntl.h"
# include "mint/emu_tos.h"
# include "mint/file.h"
# include "mint/ioctl.h"
# include "mint/pathconf.h"
# include "mint/proc.h"
# include "mint/stat.h"
# include "mint/block_IO.h"
# include "libkern/libkern.h"

# define VER_MAJOR	0
# define VER_MINOR	1

# define UFS_FIRST_DRIVE	21		/* V: */
# define UFS_MAX_DRIVES		8
# define XHDI_MAX_MAJOR		32		/* ACSI 0-7, SCSI 8-15, IDE 16-23, ... */

struct kerinfo *KERNEL;

# define ALERT(x)	KERNEL_ALERT x
# define DEBUG(x)	KERNEL_DEBUG x

/* --- the disk ----------------------------------------------------------- */

/* the Atari root sector: four entries, then the UNX one */
struct atari_part {
	uchar	flag;			/* bit 0: in use */
	char	id[3];			/* "GEM", "BGM", "UNX", "XGM" */
	ulong	start, size;		/* sectors */
};
# define ATARI_PART_OFF	0x1c6

/* the SVR4 VTOC, sector 2 of the UNX partition */
# define VTOC_SANE	0x600DDEEEUL
# define V_NUMPAR	16
# define V_SWAP		3
# define V_BACKUP	7
# define V_UNMNT	0x01
struct vtoc_part {
	ushort	tag, flag;
	long	start, size;		/* absolute sectors */
};
struct vtoc {
	ulong	bootinfo[3];
	ulong	sanity, version;
	char	volume[8];
	ushort	sectorsz, nparts;
	ulong	reserved[10];
	struct vtoc_part part[V_NUMPAR];
};

/* the 4.3BSD superblock, 8 KB into the slice, as far as fs_magic */
# define SBOFF		8192L
# define UFS_MAGIC	0x011954L
# define UFS_ROOTINO	2L
# define NDADDR		12
# define NIADDR		3
struct ufs_sb {
	long	link, rlink;
	long	sblkno, cblkno, iblkno, dblkno;
	long	cgoffset, cgmask;
	long	time, size, dsize, ncg;
	long	bsize, fsize, frag;
	long	minfree, rotdelay, rps;
	long	bmask, fmask, bshift, fshift;
	long	maxcontig, maxbpg;
	long	fragshift, fsbtodb, sbsize, csmask, csshift;
	long	nindir, inopb, nspf;
	long	optim, npsect, interleave, trackskew, headswitch, trkseek;
	long	csaddr, cssize, cgsize;
	long	ntrak, nsect, spc, ncyl, cpg, ipg, fpg;
	long	cs_ndir, cs_nbfree, cs_nifree, cs_nffree;
	char	fmod, clean, ronly, flags;
	char	fsmnt[512];
	long	cgrotor;
	long	csp[32];
	long	cpc;
	short	postbl[32][8];
	long	magic;			/* 1372 */
};

/* the inode, 128 bytes */
struct ufs_dinode {
	ushort	smode, nlink, suid, sgid;	/* 0 */
	long	size_hi, size;			/* 8 */
	long	atime, atspare, mtime, mtspare, ctime, ctspare;	/* 16 */
	long	db[NDADDR];			/* 40 */
	long	ib[NIADDR];			/* 88 */
	long	flags, blocks, gen;		/* 100 */
	long	mode, uid, gid;			/* 112: the EFT (32-bit) ones */
	ulong	eftflag;			/* 124: set when those are in use */
};
# define UFS_IFMT	0170000
# define UFS_IFIFO	0010000
# define UFS_IFCHR	0020000
# define UFS_IFDIR	0040000
# define UFS_IFBLK	0060000
# define UFS_IFREG	0100000
# define UFS_IFLNK	0120000
# define UFS_IFSOCK	0140000

/* a directory entry: 8 bytes, then the name padded to 4 */
struct ufs_direct {
	ulong	ino;
	ushort	reclen, namlen;
	char	name[4];
};
# define MAXNAMLEN	255

/* --- a mounted slice ---------------------------------------------------- */

typedef struct ufs_fs {
	ushort	drv;			/* the drive letter it got */
	ushort	major, minor;		/* XHDI device */
	ulong	start, size;		/* the slice, sectors */
	ushort	tag;			/* the VTOC tag: root 2, var 5, home 8, ... */
	ushort	slice;
	DI	*di;			/* bio's device descriptor */
	long	bsize, fsize, frag;	/* block, fragment, frags per block */
	long	bshift, fshift;
	long	nindir, inopb, ipg, fpg;
	long	iblkno, cgoffset, cgmask;
	long	dsize, nbfree, nffree, nifree, ncg;
	struct ufs_dinode root;		/* the root inode, read once */
	char	volume[9];
} UFS;

static UFS *ufs[NUM_DRIVES];
static int nufs;
static int root_drv = -1;		/* the drive of the root slice, for absolute symlinks */

static long (*xhdi)(ushort opcode, ...);

# define XHGetVersion()			xhdi(0)
# define XHInqTarget2(ma, mi, bs, fl, nm, ln)	xhdi(11, (ushort)(ma), (ushort)(mi), (ulong *)(bs), (ulong *)(fl), (char *)(nm), (ushort)(ln))
# define XHReadWrite(ma, mi, rw, rec, n, b)	xhdi(10, (ushort)(ma), (ushort)(mi), (ushort)(rw), (ulong)(rec), (ushort)(n), (void *)(b))

/* --- the file system ---------------------------------------------------- */

static long	_cdecl ufs_root		(int drv, fcookie *fc);
static long	_cdecl ufs_lookup	(fcookie *dir, const char *name, fcookie *fc);
static DEVDRV *	_cdecl ufs_getdev	(fcookie *fc, long *devsp);
static long	_cdecl ufs_getxattr	(fcookie *fc, XATTR *xattr);
static long	_cdecl ufs_stat64	(fcookie *fc, STAT *stat);
static long	_cdecl ufs_chattr	(fcookie *fc, int attrib);
static long	_cdecl ufs_chown	(fcookie *fc, int uid, int gid);
static long	_cdecl ufs_chmode	(fcookie *fc, unsigned mode);
static long	_cdecl ufs_mkdir	(fcookie *dir, const char *name, unsigned mode);
static long	_cdecl ufs_rmdir	(fcookie *dir, const char *name);
static long	_cdecl ufs_creat	(fcookie *dir, const char *name, unsigned mode, int attrib, fcookie *fc);
static long	_cdecl ufs_remove	(fcookie *dir, const char *name);
static long	_cdecl ufs_getname	(fcookie *root, fcookie *dir, char *pathname, int size);
static long	_cdecl ufs_rename	(fcookie *olddir, char *oldname, fcookie *newdir, const char *newname);
static long	_cdecl ufs_opendir	(DIR *dirh, int flags);
static long	_cdecl ufs_readdir	(DIR *dirh, char *nm, int nmlen, fcookie *fc);
static long	_cdecl ufs_rewinddir	(DIR *dirh);
static long	_cdecl ufs_closedir	(DIR *dirh);
static long	_cdecl ufs_pathconf	(fcookie *dir, int which);
static long	_cdecl ufs_dfree	(fcookie *dir, long *buf);
static long	_cdecl ufs_writelabel	(fcookie *dir, const char *name);
static long	_cdecl ufs_readlabel	(fcookie *dir, char *name, int namelen);
static long	_cdecl ufs_symlink	(fcookie *dir, const char *name, const char *to);
static long	_cdecl ufs_readlink	(fcookie *file, char *buf, int len);
static long	_cdecl ufs_hardlink	(fcookie *fromdir, const char *fromname, fcookie *todir, const char *toname);
static long	_cdecl ufs_fscntl	(fcookie *dir, const char *name, int cmd, long arg);
static long	_cdecl ufs_dskchng	(int drv, int mode);
static long	_cdecl ufs_release	(fcookie *fc);
static long	_cdecl ufs_dupcookie	(fcookie *dst, fcookie *src);
static long	_cdecl ufs_sync		(void);

static FILESYS ftab =
{
	NULL,
	FS_CASESENSITIVE | FS_LONGPATH | FS_NO_C_CACHE | FS_OWN_MEDIACHANGE |
	FS_EXT_2 | FS_EXT_3,
	ufs_root,
	ufs_lookup, ufs_creat, ufs_getdev, ufs_getxattr,
	ufs_chattr, ufs_chown, ufs_chmode,
	ufs_mkdir, ufs_rmdir, ufs_remove, ufs_getname, ufs_rename,
	ufs_opendir, ufs_readdir, ufs_rewinddir, ufs_closedir,
	ufs_pathconf, ufs_dfree, ufs_writelabel, ufs_readlabel,
	ufs_symlink, ufs_readlink, ufs_hardlink, ufs_fscntl, ufs_dskchng,
	ufs_release, ufs_dupcookie,
	ufs_sync,
	NULL, NULL,			/* FS_EXT_1: mknod, unmount */
	ufs_stat64,			/* FS_EXT_3 */
	0, 0, 0, 0, 0,
	NULL, NULL
};

static long	_cdecl ufs_open		(FILEPTR *f);
static long	_cdecl ufs_write	(FILEPTR *f, const char *buf, long bytes);
static long	_cdecl ufs_read		(FILEPTR *f, char *buf, long bytes);
static long	_cdecl ufs_lseek	(FILEPTR *f, long where, int whence);
static long	_cdecl ufs_ioctl	(FILEPTR *f, int mode, void *buf);
static long	_cdecl ufs_datime	(FILEPTR *f, ushort *time, int flag);
static long	_cdecl ufs_close	(FILEPTR *f, int pid);
static long	_cdecl ufs_select	(FILEPTR *f, long p, int mode);
static void	_cdecl ufs_unselect	(FILEPTR *f, long p, int mode);

static DEVDRV devtab =
{
	ufs_open,
	ufs_write, ufs_read, ufs_lseek, ufs_ioctl, ufs_datime,
	ufs_close,
	ufs_select, ufs_unselect
};

/* --- block access ------------------------------------------------------- */

/* bio's I/O vector for our descriptors: the record number counts
 * fragments (lshift), the sectors are absolute on the device */
static long _cdecl
ufs_rwabs (DI *di, ushort rw, void *buf, ulong size, ulong rec)
{
	ulong n = size >> di->pshift;
	ulong recno = rec << di->lshift;

	if (rw & 1)
		return EROFS;
	if (!n || recno + n > di->size)
		return ESECTOR;
	return XHReadWrite (di->major, di->minor, 0, recno + di->start, n, buf);
}

static long _cdecl
ufs_nochange (DI *di)
{
	return 0;
}

/* a block of the slice: bytes at fragment address frag, cached.  The
 * unit is good until the next bio call, so callers copy at once. */
static UNIT *
ufs_bread (UFS *s, ulong frag, long bytes)
{
	return bio.read (s->di, frag, bytes);
}

/* --- inodes ------------------------------------------------------------- */

static long
ufs_iget (UFS *s, ulong ino, struct ufs_dinode *ip)
{
	ulong cg, frag;
	UNIT *u;

	if (ino == UFS_ROOTINO) {
		*ip = s->root;
		return E_OK;
	}
	if (ino < UFS_ROOTINO || ino >= (ulong)s->ipg * s->ncg)
		return EBADARG;
	cg = ino / s->ipg;
	frag = s->fpg * cg + s->cgoffset * (cg & ~s->cgmask) + s->iblkno +
	       ((ino % s->ipg) / s->inopb) * s->frag;
	u = ufs_bread (s, frag, s->bsize);
	if (!u)
		return EREAD;
	memcpy (ip, u->data + (ino % s->inopb) * sizeof (*ip), sizeof (*ip));
	return E_OK;
}

static ushort
ufs_mode (const struct ufs_dinode *ip)
{
	ushort m = ip->smode;

	switch (m & UFS_IFMT) {
		case UFS_IFLNK:  m = (m & ~UFS_IFMT) | S_IFLNK;  break;
		case UFS_IFIFO:  m = (m & ~UFS_IFMT) | S_IFIFO;  break;
		case UFS_IFSOCK: m = (m & ~UFS_IFMT) | S_IFSOCK; break;
	}
	return m;
}

static long
ufs_uid (const struct ufs_dinode *ip)
{
	return ip->eftflag ? ip->uid : ip->suid;
}

static long
ufs_gid (const struct ufs_dinode *ip)
{
	return ip->eftflag ? ip->gid : ip->sgid;
}

/* the fragment address of a file's block n, 0 for a hole */
static ulong
ufs_bmap (UFS *s, const struct ufs_dinode *ip, ulong n)
{
	ulong addr, idx, per;
	long level;
	UNIT *u;

	if (n < NDADDR)
		return ip->db[n];
	n -= NDADDR;
	per = 1;
	for (level = 0; level < NIADDR; level++) {
		if (n < per * s->nindir)
			break;
		n -= per * s->nindir;
		per *= s->nindir;
	}
	if (level == NIADDR)
		return 0;
	addr = ip->ib[level];
	while (addr && level >= 0) {
		u = ufs_bread (s, addr, s->bsize);
		if (!u)
			return 0;
		idx = n / per;
		n %= per;
		addr = ((ulong *)u->data)[idx];
		per /= s->nindir;
		level--;
	}
	return addr;
}

/* bytes [pos, pos + len) of a file into buf; returns the bytes read */
static long
ufs_readfile (UFS *s, const struct ufs_dinode *ip, long pos, char *buf, long len)
{
	long done = 0, size = ip->size;

	if (pos >= size)
		return 0;
	if (len > size - pos)
		len = size - pos;
	while (len > 0) {
		ulong blk = pos >> s->bshift, frag;
		long off = pos & (s->bsize - 1), n = s->bsize - off, have;
		UNIT *u;

		if (n > len)
			n = len;
		/* the file's last block may be fragments only */
		have = size - (blk << s->bshift);
		if (have > s->bsize)
			have = s->bsize;
		have = (have + s->fsize - 1) & ~(s->fsize - 1);
		frag = ufs_bmap (s, ip, blk);
		if (!frag)
			memset (buf, 0, n);
		else {
			u = ufs_bread (s, frag, have);
			if (!u)
				break;
			memcpy (buf, u->data + off, n);
		}
		buf += n;
		pos += n;
		len -= n;
		done += n;
	}
	return done;
}

/* --- directories -------------------------------------------------------- */

/* the entry called name in dir: its inode number, or 0 */
static ulong
ufs_dirlookup (UFS *s, const struct ufs_dinode *dir, const char *name, long namlen)
{
	long pos = 0, size = dir->size;
	char *blk;

	blk = kmalloc (s->bsize);
	if (!blk)
		return 0;
	while (pos < size) {
		long n = ufs_readfile (s, dir, pos, blk, s->bsize), off = 0;

		if (n <= 0)
			break;
		while (off + 8 <= n) {
			struct ufs_direct *d = (struct ufs_direct *)(blk + off);

			if (d->reclen < 8)
				goto out;
			if (d->ino && d->namlen == namlen && !memcmp (d->name, name, namlen)) {
				ulong ino = d->ino;

				kfree (blk);
				return ino;
			}
			off += d->reclen;
		}
		pos += n;
	}
out:
	kfree (blk);
	return 0;
}

/* the name of inode ino in dir, for getname */
static long
ufs_dirname (UFS *s, const struct ufs_dinode *dir, ulong ino, char *name, long max)
{
	long pos = 0, size = dir->size, r = ENOENT;
	char *blk;

	blk = kmalloc (s->bsize);
	if (!blk)
		return ENOMEM;
	while (pos < size && r == ENOENT) {
		long n = ufs_readfile (s, dir, pos, blk, s->bsize), off = 0;

		if (n <= 0)
			break;
		while (off + 8 <= n) {
			struct ufs_direct *d = (struct ufs_direct *)(blk + off);

			if (d->reclen < 8)
				goto out;
			if (d->ino == ino) {
				if (d->namlen >= max) {
					r = EBADARG;
					break;
				}
				memcpy (name, d->name, d->namlen);
				name[d->namlen] = '\0';
				r = E_OK;
				break;
			}
			off += d->reclen;
		}
		pos += n;
	}
out:
	kfree (blk);
	return r;
}

/* --- mounting ----------------------------------------------------------- */

static int
ufs_free_drive (int from)
{
	ulong bits = *(ulong *)0x4c2L;
	int d;

	for (d = from; d < NUM_DRIVES; d++)
		if (d != 20 && !(bits & (1UL << d)) && !ufs[d])
			return d;
	return -1;
}

/* a slice with a UFS superblock becomes a drive */
static int next_drive = UFS_FIRST_DRIVE;

static long
ufs_mount_slice (ushort major, ushort minor, const struct vtoc_part *p, int slice, const char *volume)
{
	struct ufs_sb *sb;
	UFS *s;
	DI *di;
	UNIT *u;
	int drv;
	long r;

	sb = kmalloc (SBOFF);
	if (!sb)
		return ENOMEM;
	r = XHReadWrite (major, minor, 0, p->start + SBOFF / 512, SBOFF / 512, sb);
	if (r || sb->magic != UFS_MAGIC || sb->bsize < 4096 || sb->bsize > 8192 ||
	    sb->fsize < 512 || sb->fsize > sb->bsize || sb->frag != sb->bsize / sb->fsize ||
	    sb->nindir != sb->bsize / 4 || sb->inopb != sb->bsize / 128 ||
	    sb->ipg <= 0 || sb->fpg <= 0 || sb->ncg <= 0) {
		kfree (sb);
		return r ? r : EMEDIUMTYPE;
	}
	drv = ufs_free_drive (next_drive);
	if (drv < 0 || nufs >= UFS_MAX_DRIVES) {
		kfree (sb);
		return ENOMEM;
	}
	s = kmalloc (sizeof (*s));
	if (!s) {
		kfree (sb);
		return ENOMEM;
	}
	memset (s, 0, sizeof (*s));
	s->drv = drv;
	s->major = major;
	s->minor = minor;
	s->start = p->start;
	s->size = p->size;
	s->tag = p->tag;
	s->slice = slice;
	s->bsize = sb->bsize;
	s->fsize = sb->fsize;
	s->frag = sb->frag;
	s->bshift = sb->bshift;
	s->fshift = sb->fshift;
	s->nindir = sb->nindir;
	s->inopb = sb->inopb;
	s->ipg = sb->ipg;
	s->fpg = sb->fpg;
	s->iblkno = sb->iblkno;
	s->cgoffset = sb->cgoffset;
	s->cgmask = sb->cgmask;
	s->dsize = sb->dsize;
	s->nbfree = sb->cs_nbfree;
	s->nffree = sb->cs_nffree;
	s->nifree = sb->cs_nifree;
	s->ncg = sb->ncg;
	kfree (sb);
	memcpy (s->volume, volume, 8);
	s->volume[8] = '\0';
	if (!s->volume[0]) {
		const char *n = s->tag == 2 ? "root" : s->tag == 5 ? "var" :
				s->tag == 8 ? "home" : s->tag == 4 ? "usr" : "ufs";

		strcpy (s->volume, n);
	}

	di = bio.res_di (drv);
	if (!di) {
		kfree (s);
		return ENOMEM;
	}
	di->major = major;
	di->minor = minor;
	di->start = s->start;
	di->size = s->size;
	di->mode |= BIO_LRECNO | BIO_WP_HARD | BIO_WP_MODE;
	*di->rrwabs = ufs_rwabs;
	*di->rdskchng = ufs_nochange;
	bio.set_pshift (di, 512);
	bio.set_lshift (di, s->fsize);
	s->di = di;

	/* the root inode, kept: the inode table's first block */
	{
		ulong frag = s->iblkno;

		u = ufs_bread (s, frag, s->bsize);
		if (!u) {
			bio.free_di (di);
			kfree (s);
			return EREAD;
		}
		memcpy (&s->root, u->data + UFS_ROOTINO * sizeof (s->root), sizeof (s->root));
	}
	if ((s->root.smode & UFS_IFMT) != UFS_IFDIR) {
		bio.free_di (di);
		kfree (s);
		return EMEDIUMTYPE;
	}

	ufs[drv] = s;
	nufs++;
	next_drive = drv + 1;
	*(ulong *)0x4c2L |= 1UL << drv;
	if (s->tag == 2 && root_drv < 0)
		root_drv = drv;
	return E_OK;
}

/* the UNX partition of one XHDI device: its VTOC's slices */
static long
ufs_scan_device (ushort major, ushort minor)
{
	char *sec;
	ulong bs = 0, flags = 0;
	char name[64];
	long r, i, found = 0;
	ulong unx = 0;

	r = XHInqTarget2 (major, minor, &bs, &flags, name, sizeof name);
	if (r || bs != 512)
		return r ? r : EMEDIUMTYPE;
	sec = kmalloc (512);
	if (!sec)
		return ENOMEM;
	r = XHReadWrite (major, minor, 0, 0UL, 1, sec);
	if (r)
		goto out;
	for (i = 0; i < 4; i++) {
		struct atari_part *ap = (struct atari_part *)(sec + ATARI_PART_OFF + 12 * i);

		if ((ap->flag & 1) && ap->id[0] == 'U' && ap->id[1] == 'N' && ap->id[2] == 'X') {
			unx = ap->start;
			break;
		}
	}
	if (i == 4) {
		r = EMEDIUMTYPE;
		goto out;
	}
	r = XHReadWrite (major, minor, 0, unx + 2, 1, sec);
	if (r)
		goto out;
	{
		struct vtoc *v = (struct vtoc *)sec;
		struct vtoc_part parts[V_NUMPAR];
		char volume[8];
		long n = v->nparts;

		if (v->sanity != VTOC_SANE || n < 1 || n > V_NUMPAR) {
			r = EMEDIUMTYPE;
			goto out;
		}
		memcpy (parts, v->part, sizeof parts);
		memcpy (volume, v->volume, 8);
		for (i = 0; i < n; i++) {
			struct vtoc_part *p = &parts[i];

			if (p->size <= 0 || p->start <= 0 || p->tag == V_SWAP ||
			    p->tag == V_BACKUP || (p->flag & V_UNMNT))
				continue;
			if (ufs_mount_slice (major, minor, p, (int)i, volume) == E_OK)
				found++;
		}
	}
	r = found ? E_OK : EMEDIUMTYPE;
out:
	kfree (sec);
	return r;
}

/* --- init --------------------------------------------------------------- */

# define MSG_BOOT	"\033p UFS (Atari System V) file system driver " \
			"0.1, read-only \033q\r\n"
# define MSG_OLDMINT	"\033pMiNT too old, ufs.xfs needs FreeMiNT 1.16\033q\r\n"
# define MSG_NOXHDI	"\033pno XHDI 1.10: ufs.xfs not installed\033q\r\n"
# define MSG_NONE	"ufs: no UFS slice on any XHDI device\r\n"

FILESYS * _cdecl
init_xfs (struct kerinfo *k)
{
	ulong t = 0;
	ushort major;
	char line[16];
	int d;

	KERNEL = k;
	_c_conws (MSG_BOOT);
	if (MINT_MAJOR < 1 || (MINT_MAJOR == 1 && MINT_MINOR < 16) ||
	    MINT_KVERSION == 0 || !KERNEL->xtime || bio.version != 3) {
		_c_conws (MSG_OLDMINT);
		return NULL;
	}
	if (get_toscookie (0x58484449UL /* XHDI */, &t) || !t ||
	    ((long *)t)[-1] != 0x27011992L) {
		_c_conws (MSG_NOXHDI);
		return NULL;
	}
	xhdi = (long (*)(ushort, ...))t;
	if (XHGetVersion () < 0x110) {
		_c_conws (MSG_NOXHDI);
		return NULL;
	}
	for (major = 0; major < XHDI_MAX_MAJOR; major++)
		ufs_scan_device (major, 0);
	if (!nufs) {
		_c_conws (MSG_NONE);
		return NULL;
	}
	_c_conws ("ufs:");
	for (d = 0; d < NUM_DRIVES; d++) {
		if (!ufs[d])
			continue;
		line[0] = ' ';
		line[1] = DriveToLetter (d);
		line[2] = ':';
		line[3] = ' ';
		strcpy (line + 4, ufs[d]->volume);
		_c_conws (line);
	}
	_c_conws ("\r\n");
	return &ftab;
}

/* --- the FILESYS calls -------------------------------------------------- */

static long _cdecl
ufs_root (int drv, fcookie *fc)
{
	if (drv < 0 || drv >= NUM_DRIVES || !ufs[drv])
		return EMEDIUMTYPE;
	fc->fs = &ftab;
	fc->dev = drv;
	fc->aux = 0;
	fc->index = UFS_ROOTINO;
	return E_OK;
}

static long _cdecl
ufs_lookup (fcookie *dir, const char *name, fcookie *fc)
{
	UFS *s = ufs[dir->dev];
	struct ufs_dinode in;
	long r, len;
	ulong ino;

	if (!s)
		return EMEDIUMTYPE;
	if (!*name || (name[0] == '.' && name[1] == '\0')) {
		*fc = *dir;
		return E_OK;
	}
	if (name[0] == '.' && name[1] == '.' && name[2] == '\0' && dir->index == UFS_ROOTINO) {
		*fc = *dir;
		return EMOUNT;
	}
	r = ufs_iget (s, dir->index, &in);
	if (r)
		return r;
	if ((in.smode & UFS_IFMT) != UFS_IFDIR)
		return ENOTDIR;
	len = strlen (name);
	if (len > MAXNAMLEN)
		return ENAMETOOLONG;
	ino = ufs_dirlookup (s, &in, name, len);
	if (!ino)
		return ENOENT;
	fc->fs = &ftab;
	fc->dev = dir->dev;
	fc->aux = 0;
	fc->index = ino;
	return E_OK;
}

static DEVDRV * _cdecl
ufs_getdev (fcookie *fc, long *devsp)
{
	return fc->fs == &ftab ? &devtab : NULL;
}

static long _cdecl
ufs_getxattr (fcookie *fc, XATTR *xattr)
{
	UFS *s = ufs[fc->dev];
	struct ufs_dinode in;
	long r;

	if (!s)
		return EMEDIUMTYPE;
	r = ufs_iget (s, fc->index, &in);
	if (r)
		return r;
	xattr->mode = ufs_mode (&in);
	xattr->index = fc->index;
	xattr->dev = fc->dev;
	xattr->rdev = 0;
	if (S_ISCHR (xattr->mode) || S_ISBLK (xattr->mode))
		xattr->rdev = (ushort)in.db[0];
	xattr->nlink = in.nlink;
	xattr->uid = (ushort)ufs_uid (&in);
	xattr->gid = (ushort)ufs_gid (&in);
	xattr->size = in.size;
	xattr->blksize = s->bsize;
	xattr->nblocks = (in.blocks + (s->bsize >> 9) - 1) / (s->bsize >> 9);
	/* UTC seconds; the kernel makes local DOS time of them */
	xattr->mtime = (ushort)((ulong)in.mtime >> 16);
	xattr->mdate = (ushort)in.mtime;
	xattr->atime = (ushort)((ulong)in.atime >> 16);
	xattr->adate = (ushort)in.atime;
	xattr->ctime = (ushort)((ulong)in.ctime >> 16);
	xattr->cdate = (ushort)in.ctime;
	xattr->attr = S_ISDIR (xattr->mode) ? FA_DIR : FA_RDONLY;
	xattr->reserved2 = 0;
	xattr->reserved3[0] = xattr->reserved3[1] = 0;
	return E_OK;
}

static long _cdecl
ufs_stat64 (fcookie *fc, STAT *st)
{
	UFS *s = ufs[fc->dev];
	struct ufs_dinode in;
	long r;

	if (!s)
		return EMEDIUMTYPE;
	r = ufs_iget (s, fc->index, &in);
	if (r)
		return r;
	memset (st, 0, sizeof (*st));
	st->dev = fc->dev;
	st->ino = fc->index;
	st->mode = ufs_mode (&in);
	st->nlink = in.nlink;
	st->uid = ufs_uid (&in);
	st->gid = ufs_gid (&in);
	if (S_ISCHR (st->mode) || S_ISBLK (st->mode))
		st->rdev = in.db[0];
	st->atime.time = in.atime;
	st->mtime.time = in.mtime;
	st->ctime.time = in.ctime;
	st->size = in.size;
	st->blocks = in.blocks;
	st->blksize = s->bsize;
	st->gen = in.gen;
	return E_OK;
}

static long _cdecl ufs_chattr (fcookie *fc, int attrib) { return EROFS; }
static long _cdecl ufs_chown (fcookie *fc, int uid, int gid) { return EROFS; }
static long _cdecl ufs_chmode (fcookie *fc, unsigned mode) { return EROFS; }
static long _cdecl ufs_mkdir (fcookie *dir, const char *name, unsigned mode) { return EROFS; }
static long _cdecl ufs_rmdir (fcookie *dir, const char *name) { return EROFS; }
static long _cdecl ufs_creat (fcookie *dir, const char *name, unsigned mode, int attrib, fcookie *fc) { return EROFS; }
static long _cdecl ufs_remove (fcookie *dir, const char *name) { return EROFS; }
static long _cdecl ufs_rename (fcookie *olddir, char *oldname, fcookie *newdir, const char *newname) { return EROFS; }
static long _cdecl ufs_writelabel (fcookie *dir, const char *name) { return EROFS; }
static long _cdecl ufs_symlink (fcookie *dir, const char *name, const char *to) { return EROFS; }
static long _cdecl ufs_hardlink (fcookie *fromdir, const char *fromname, fcookie *todir, const char *toname) { return EROFS; }
static long _cdecl ufs_sync (void) { return E_OK; }
static long _cdecl ufs_dskchng (int drv, int mode) { return 0; }
static long _cdecl ufs_release (fcookie *fc) { return E_OK; }
static long _cdecl ufs_dupcookie (fcookie *dst, fcookie *src) { *dst = *src; return E_OK; }

/* the path of dir below root, "/" separated, by walking up through ".." */
static long _cdecl
ufs_getname (fcookie *root, fcookie *dir, char *pathname, int size)
{
	UFS *s = ufs[dir->dev];
	ulong ino = dir->index;
	long len = 0, r;
	char name[MAXNAMLEN + 1];

	if (!s)
		return EMEDIUMTYPE;
	*pathname = '\0';
	size--;
	while (ino != root->index) {
		struct ufs_dinode in;
		ulong parent;
		long i;

		r = ufs_iget (s, ino, &in);
		if (r)
			return r;
		parent = ufs_dirlookup (s, &in, "..", 2);
		if (!parent)
			return ENOENT;
		r = ufs_iget (s, parent, &in);
		if (r)
			return r;
		r = ufs_dirname (s, &in, ino, name, sizeof name);
		if (r)
			return r;
		i = strlen (name);
		if (len + i + 1 > size)
			return EBADARG;
		/* the pieces come bottom up: each is reversed, the whole
		 * reversed at the end */
		while (i--)
			pathname[len++] = name[i];
		pathname[len++] = '/';
		pathname[len] = '\0';
		ino = parent;
	}
	strrev (pathname);
	return E_OK;
}

struct ufs_dirinfo {
	long	pos;
	long	size;
};

static long _cdecl
ufs_opendir (DIR *dirh, int flags)
{
	UFS *s = ufs[dirh->fc.dev];
	struct ufs_dirinfo *di = (struct ufs_dirinfo *)dirh->fsstuff;
	struct ufs_dinode in;
	long r;

	if (!s)
		return EMEDIUMTYPE;
	r = ufs_iget (s, dirh->fc.index, &in);
	if (r)
		return r;
	if ((in.smode & UFS_IFMT) != UFS_IFDIR)
		return ENOTDIR;
	di->pos = 0;
	di->size = in.size;
	return E_OK;
}

static long _cdecl
ufs_readdir (DIR *dirh, char *nm, int nmlen, fcookie *fc)
{
	UFS *s = ufs[dirh->fc.dev];
	struct ufs_dirinfo *di = (struct ufs_dirinfo *)dirh->fsstuff;
	struct ufs_dinode in;
	long r;

	if (!s)
		return EMEDIUMTYPE;
	r = ufs_iget (s, dirh->fc.index, &in);
	if (r)
		return r;
	while (di->pos < di->size) {
		struct ufs_direct d;
		char name[MAXNAMLEN + 1];
		long n;

		/* the header, then the name: the entry never crosses a
		 * 512-byte boundary, nor does an 8 KB block */
		n = ufs_readfile (s, &in, di->pos, (char *)&d, 8);
		if (n < 8 || d.reclen < 8)
			return ENMFILES;
		if (d.ino && d.namlen <= MAXNAMLEN) {
			n = ufs_readfile (s, &in, di->pos + 8, name, d.namlen);
			di->pos += d.reclen;
			if (n < d.namlen)
				return ENMFILES;
			name[d.namlen] = '\0';
			if (!(dirh->flags & TOS_SEARCH)) {
				unaligned_putl (nm, (long)d.ino);
				nm += 4;
				nmlen -= 4;
			}
			if (nmlen <= d.namlen)
				return EBADARG;
			strcpy (nm, name);
			fc->fs = &ftab;
			fc->dev = dirh->fc.dev;
			fc->aux = 0;
			fc->index = d.ino;
			return E_OK;
		}
		di->pos += d.reclen;
	}
	return ENMFILES;
}

static long _cdecl
ufs_rewinddir (DIR *dirh)
{
	struct ufs_dirinfo *di = (struct ufs_dirinfo *)dirh->fsstuff;

	di->pos = 0;
	return E_OK;
}

static long _cdecl
ufs_closedir (DIR *dirh)
{
	return E_OK;
}

static long _cdecl
ufs_pathconf (fcookie *dir, int which)
{
	switch (which) {
		case DP_INQUIRE:	return DP_VOLNAMEMAX;
		case DP_IOPEN:		return UNLIMITED;
		case DP_MAXLINKS:	return 32767;
		case DP_PATHMAX:	return UNLIMITED;
		case DP_NAMEMAX:	return MAXNAMLEN;
		case DP_ATOMIC:		return 8192;
		case DP_TRUNC:		return DP_NOTRUNC;
		case DP_CASE:		return DP_CASESENS;
		case DP_MODEATTR:	return DP_ATTRBITS | DP_MODEBITS | DP_FT_DIR |
					       DP_FT_CHR | DP_FT_BLK | DP_FT_REG |
					       DP_FT_LNK | DP_FT_SOCK | DP_FT_FIFO;
		case DP_XATTRFIELDS:	return DP_INDEX | DP_DEV | DP_RDEV | DP_NLINK |
					       DP_UID | DP_GID | DP_BLKSIZE | DP_SIZE |
					       DP_NBLOCKS | DP_ATIME | DP_CTIME | DP_MTIME;
		case DP_VOLNAMEMAX:	return 8;
	}
	return ENOSYS;
}

static long _cdecl
ufs_dfree (fcookie *dir, long *buf)
{
	UFS *s = ufs[dir->dev];

	if (!s)
		return EMEDIUMTYPE;
	buf[0] = s->nbfree * s->frag + s->nffree;	/* free fragments */
	buf[1] = s->dsize;				/* fragments */
	buf[2] = 512;
	buf[3] = s->fsize / 512;
	return E_OK;
}

static long _cdecl
ufs_readlabel (fcookie *dir, char *name, int namelen)
{
	UFS *s = ufs[dir->dev];

	if (!s)
		return EMEDIUMTYPE;
	if (namelen <= (int)strlen (s->volume))
		return EBADARG;
	strcpy (name, s->volume);
	return E_OK;
}

/* the link's target; an absolute one is put on the root slice's drive,
 * so that /usr/bin/x resolves where System V would resolve it */
static long _cdecl
ufs_readlink (fcookie *file, char *buf, int len)
{
	UFS *s = ufs[file->dev];
	struct ufs_dinode in;
	char target[1024];
	long r, n, off = 0;

	if (!s)
		return EMEDIUMTYPE;
	r = ufs_iget (s, file->index, &in);
	if (r)
		return r;
	if ((in.smode & UFS_IFMT) != UFS_IFLNK)
		return EINVAL;
	n = in.size;
	if (n <= 0 || n >= (long)sizeof target)
		return EBADARG;
	if (in.blocks == 0)	/* a 4.4-style short link, kept in the inode */
		memcpy (target, in.db, n);
	else if (ufs_readfile (s, &in, 0, target, n) != n)
		return EREAD;
	target[n] = '\0';
	if (target[0] == '/' && root_drv >= 0) {
		if (len < 3)
			return EBADARG;
		buf[0] = DriveToLetter (root_drv);
		buf[1] = ':';
		off = 2;
	}
	if (n + off >= len)
		return EBADARG;
	strcpy (buf + off, target);
	return E_OK;
}

static long _cdecl
ufs_fscntl (fcookie *dir, const char *name, int cmd, long arg)
{
	UFS *s = ufs[dir->dev];

	switch (cmd) {
		case MX_KER_XFSNAME:
			strcpy ((char *)arg, "ufs");
			return E_OK;
		case FS_INFO: {
			struct fs_info *info = (struct fs_info *)arg;

			if (info) {
				strcpy (info->name, "ufs-xfs");
				info->version = ((long)VER_MAJOR << 16) | VER_MINOR;
				info->type = 0;
				strcpy (info->type_asc, "ufs");
			}
			return E_OK;
		}
		case FS_USAGE: {
			struct fs_usage *usage = (struct fs_usage *)arg;

			if (usage && s) {
				usage->blocksize = s->fsize;
				usage->blocks = s->dsize;
				usage->free_blocks = s->nbfree * s->frag + s->nffree;
				usage->inodes = (long)s->ipg * s->ncg;
				usage->free_inodes = s->nifree;
			}
			return E_OK;
		}
	}
	return ENOSYS;
}

/* --- the device driver: open files -------------------------------------- */

static long _cdecl
ufs_open (FILEPTR *f)
{
	UFS *s = ufs[f->fc.dev];
	struct ufs_dinode in;
	long r;

	if (!s)
		return EMEDIUMTYPE;
	if ((f->flags & O_RWMODE) != O_RDONLY)
		return EROFS;
	r = ufs_iget (s, f->fc.index, &in);
	if (r)
		return r;
	if ((in.smode & UFS_IFMT) != UFS_IFREG)
		return EACCES;
	f->pos = 0;
	f->devinfo = in.size;
	return E_OK;
}

static long _cdecl
ufs_write (FILEPTR *f, const char *buf, long bytes)
{
	return EROFS;
}

static long _cdecl
ufs_read (FILEPTR *f, char *buf, long bytes)
{
	UFS *s = ufs[f->fc.dev];
	struct ufs_dinode in;
	long r;

	if (!s)
		return EMEDIUMTYPE;
	if (bytes <= 0)
		return 0;
	r = ufs_iget (s, f->fc.index, &in);
	if (r)
		return r;
	r = ufs_readfile (s, &in, f->pos, buf, bytes);
	if (r > 0)
		f->pos += r;
	return r;
}

static long _cdecl
ufs_lseek (FILEPTR *f, long where, int whence)
{
	switch (whence) {
		case SEEK_SET:	break;
		case SEEK_CUR:	where += f->pos;	break;
		case SEEK_END:	where += f->devinfo;	break;
		default:	return EINVAL;
	}
	if (where < 0)
		return EBADARG;
	f->pos = where;
	return where;
}

static long _cdecl
ufs_ioctl (FILEPTR *f, int mode, void *buf)
{
	switch (mode) {
		case FIONREAD:
			*(long *)buf = f->devinfo - f->pos;
			return E_OK;
		case FIONWRITE:
			*(long *)buf = 0;
			return E_OK;
		case FIOEXCEPT:
			*(long *)buf = 0;
			return E_OK;
	}
	return ENOSYS;
}

static long _cdecl
ufs_datime (FILEPTR *f, ushort *time, int flag)
{
	UFS *s = ufs[f->fc.dev];
	struct ufs_dinode in;
	long r;

	if (flag)
		return EROFS;
	if (!s)
		return EMEDIUMTYPE;
	r = ufs_iget (s, f->fc.index, &in);
	if (r)
		return r;
	time[0] = (ushort)((ulong)in.mtime >> 16);
	time[1] = (ushort)in.mtime;
	return E_OK;
}

static long _cdecl
ufs_close (FILEPTR *f, int pid)
{
	return E_OK;
}

static long _cdecl
ufs_select (FILEPTR *f, long p, int mode)
{
	return mode == O_RDONLY || mode == O_WRONLY ? 1 : 0;
}

static void _cdecl
ufs_unselect (FILEPTR *f, long p, int mode)
{
}
