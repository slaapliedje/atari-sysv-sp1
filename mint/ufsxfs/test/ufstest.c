/* ufstest: list a UFS drive's root and read a file through ufs.xfs */
#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <mint/ostruct.h>
struct myxattr { unsigned short mode; long index; unsigned short dev, rdev, nlink, uid, gid; long size, blksize, nblocks;
	unsigned short mtime, mdate, atime, adate, ctime, cdate; short attr, res2; long res3[2]; };

static void ls(const char *path)
{
	long d = Dopendir(path, 0);
	char buf[300];
	struct myxattr xa;
	int n = 0;

	printf("ls %s: handle %lx\n", path, d);
	if (d < 0) return;
	{
		long xret = 0, r;
		while ((r = Dxreaddir(sizeof buf, d, buf, (void *)&xa, &xret)) == 0 && n < 40) {
			printf("  %6o %6ld %8ld %s (xret %ld)\n", xa.mode, xa.index, xa.size, buf + 4, xret);
			n++;
		}
		printf("  Dxreaddir -> %ld after %d entries\n", r, n);
		Drewinddir(d);
		r = Dreaddir(sizeof buf, d, buf);
		printf("  Dreaddir -> %ld: %s\n", r, r == 0 ? buf + 4 : "");
	}
	Dclosedir(d);
}

static void cat(const char *path, long max)
{
	long fd = Fopen(path, 0), n, total = 0;
	unsigned long sum = 0, i;
	char buf[1024];

	printf("cat %s: fd %ld\n", path, fd);
	if (fd < 0) return;
	while ((n = Fread(fd, sizeof buf, buf)) > 0) {
		if (total < max) {
			long k = n; if (total + k > max) k = max - total;
			fwrite(buf, 1, k, stdout);
		}
		for (i = 0; i < n; i++) sum = sum * 31 + (unsigned char)buf[i];
		total += n;
	}
	printf("\n[%ld bytes, last read %ld, sum %lx]\n", total, n, sum);
	Fclose(fd);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	long r, free[4];
	char lbuf[64], tgt[256];
	const char *drv = argc > 1 ? argv[1] : "V:";
	char p[300];

	printf("ufstest: drive %s\n", drv);
	r = Dfree(free, drv[0] - 'A' + 1);
	printf("Dfree %ld: free %ld total %ld bps %ld spc %ld\n", r, free[0], free[1], free[2], free[3]);
	sprintf(p, "%s\\", drv);
	r = Dreadlabel(p, lbuf, sizeof lbuf);
	printf("label %ld: %s\n", r, lbuf);
	ls(p);
	sprintf(p, "%s\\etc", drv); ls(p);
	sprintf(p, "%s\\etc\\passwd", drv); cat(p, 400);
	sprintf(p, "%s\\usr\\bin\\sh", drv); cat(p, 0);
	sprintf(p, "%s\\usr\\lib\\libc.so.1", drv); cat(p, 0);
	sprintf(p, "%s\\stand\\unix", drv); cat(p, 0);
	sprintf(p, "%s\\etc\\motd", drv); cat(p, 400);
	sprintf(p, "%s\\bin", drv); r = Freadlink(sizeof tgt, tgt, p);
	printf("readlink %s: %ld %s\n", p, r, r == 0 ? tgt : "");
	sprintf(p, "%s\\usr\\bin\\..\\lib", drv); ls(p);
	return 0;
}
