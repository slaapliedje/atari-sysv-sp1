/* mixbench: what software mixing costs on this CPU. Mixes SECS seconds
 * of 25033 Hz audio several ways and prints the share of one second of
 * CPU each needs per second of sound. */
#include <stdio.h>
#include <sys/times.h>
#include <unistd.h>

#define HZ_OUT	25033L
#define SECS	4
#define FRAMES	512			/* one mix call */
#define NV	8

static signed char smp[NV][8192];	/* 8-bit samples, looped */
static unsigned char wave[4][256];	/* OpenUA-style wavetables */
static signed char voltab[65][256];	/* vol * sample / 64 */
static short wvol[65][256];		/* the same, as words */
static short acc[2][FRAMES];
static char out[FRAMES * 2];

struct voice { signed char *s; unsigned long pos, inc, len; int vol, l, r; };
static struct voice v[NV];

static double cpu(void)
{
	struct tms t;
	times(&t);
	return (double)(t.tms_utime + t.tms_stime) / sysconf(_SC_CLK_TCK);
}

/* A: today's OpenUA mixer shape, 4 wavetables, mono */
static void mix_a(char *d, long n)
{
	unsigned long p0 = v[0].pos, p1 = v[1].pos, p2 = v[2].pos, p3 = v[3].pos;
	unsigned long i0 = v[0].inc, i1 = v[1].inc, i2 = v[2].inc, i3 = v[3].inc;
	long i;
	for (i = 0; i < n; i++) {
		long u;
		p0 += i0; p1 += i1; p2 += i2; p3 += i3;
		u = (long)wave[0][(p0 >> 16) & 0xff] + wave[1][(p1 >> 16) & 0xff]
		  + wave[2][(p2 >> 16) & 0xff] + wave[3][(p3 >> 16) & 0xff];
		d[i] = (char)((u - 512) >> 2);
	}
	v[0].pos = p0; v[1].pos = p1; v[2].pos = p2; v[3].pos = p3;
}

/* B: 8 voices, volume by table, hard left/right, sample-major with a loop
 * check per voice per sample */
static void mix_b(char *d, long n)
{
	long i; int k;
	for (i = 0; i < n; i++) {
		long l = 0, r = 0;
		for (k = 0; k < NV; k++) {
			struct voice *w = &v[k];
			long s;
			w->pos += w->inc;
			if (w->pos >= w->len)
				w->pos -= w->len;
			s = voltab[w->vol][(unsigned char)w->s[w->pos >> 16]];
			if (k & 1) r += s; else l += s;
		}
		l >>= 2; r >>= 2;
		d[2 * i] = (char)(l > 127 ? 127 : l < -128 ? -128 : l);
		d[2 * i + 1] = (char)(r > 127 ? 127 : r < -128 ? -128 : r);
	}
}

/* C: as B with a pan per voice (two table reads per voice) */
static void mix_c(char *d, long n)
{
	long i; int k;
	for (i = 0; i < n; i++) {
		long l = 0, r = 0;
		for (k = 0; k < NV; k++) {
			struct voice *w = &v[k];
			unsigned char x;
			w->pos += w->inc;
			if (w->pos >= w->len)
				w->pos -= w->len;
			x = (unsigned char)w->s[w->pos >> 16];
			l += voltab[w->l][x];
			r += voltab[w->r][x];
		}
		l >>= 2; r >>= 2;
		d[2 * i] = (char)(l > 127 ? 127 : l < -128 ? -128 : l);
		d[2 * i + 1] = (char)(r > 127 ? 127 : r < -128 ? -128 : r);
	}
}

/* D: voice-major into 16-bit accumulators; the loop end is handled per
 * run, not per sample; then one pass to 8-bit interleaved */
static void mix_d(char *d, long n)
{
	int k; long i;
	for (i = 0; i < n; i++)
		acc[0][i] = acc[1][i] = 0;
	for (k = 0; k < NV; k++) {
		struct voice *w = &v[k];
		short *a = acc[k & 1];
		signed char *vt = voltab[w->vol];
		signed char *s = w->s;
		unsigned long pos = w->pos, inc = w->inc;
		long done = 0;
		while (done < n) {
			long run = (long)((w->len - pos + inc - 1) / inc);
			long j;
			if (run > n - done)
				run = n - done;
			for (j = 0; j < run; j++) {
				a[done + j] += vt[(unsigned char)s[pos >> 16]];
				pos += inc;
			}
			done += run;
			if (pos >= w->len)
				pos -= w->len;
		}
		w->pos = pos;
	}
	for (i = 0; i < n; i++) {
		long l = acc[0][i] >> 2, r = acc[1][i] >> 2;
		d[2 * i] = (char)(l > 127 ? 127 : l < -128 ? -128 : l);
		d[2 * i + 1] = (char)(r > 127 ? 127 : r < -128 ? -128 : r);
	}
}


/* E: as D with the inner loop in 68030 assembly: the 16.16 step as
 * add.w (fraction, sets X) + addx.l (whole part), no shifts; volume from
 * a word table; four instructions and a dbra per sample */
static void mix_e(char *d, long n)
{
	int k; long i;
	for (i = 0; i < n; i++)
		acc[0][i] = acc[1][i] = 0;
	for (k = 0; k < NV; k++) {
		struct voice *w = &v[k];
		short *a = acc[k & 1];
		long done = 0;
		unsigned long pos = w->pos;
		while (done < n) {
			long run = (long)((w->len - pos + w->inc - 1) / w->inc), cnt;
			unsigned long ipart = pos >> 16, fpart = pos & 0xffff;
			if (run > n - done)
				run = n - done;
			cnt = run - 1;
			{
				/* 0 sample, 4 volume table, 8 accumulator, 12 count-1,
				 * 16 whole part, 20 fraction, 24 step fraction, 28 step whole */
				long st[8];
				st[0] = (long)w->s; st[1] = (long)wvol[w->vol]; st[2] = (long)(a + done);
				st[3] = cnt; st[4] = (long)ipart; st[5] = (long)fpart;
				st[6] = (long)(w->inc & 0xffff); st[7] = (long)(w->inc >> 16);
				asm volatile(
				"	mov.l %0,%%a3\n"
				"	mov.l (%%a3),%%a0\n"
				"	mov.l 4(%%a3),%%a1\n"
				"	mov.l 8(%%a3),%%a2\n"
				"	mov.l 12(%%a3),%%d4\n"
				"	mov.l 16(%%a3),%%d0\n"
				"	mov.l 20(%%a3),%%d1\n"
				"	mov.l 24(%%a3),%%d2\n"
				"	mov.l 28(%%a3),%%d3\n"
				"	clr.l %%d5\n"
				"1:	mov.b (%%a0,%%d0.l),%%d5\n"
				"	mov.w (%%a1,%%d5.w*2),%%d6\n"
				"	add.w %%d6,(%%a2)+\n"
				"	add.w %%d2,%%d1\n"
				"	addx.l %%d3,%%d0\n"
				"	dbra %%d4,1b\n"
				"	mov.l %%d0,16(%%a3)\n"
				"	mov.l %%d1,20(%%a3)\n"
				: : "a"(st)
				: "d0", "d1", "d2", "d3", "d4", "d5", "d6", "a0", "a1", "a2", "a3", "cc", "memory");
				pos = ((unsigned long)st[4] << 16) | ((unsigned long)st[5] & 0xffff);
			}
			done += run;
			if (pos >= w->len)
				pos -= w->len;
		}
		w->pos = pos;
	}
	for (i = 0; i < n; i++) {
		long l = acc[0][i] >> 2, r = acc[1][i] >> 2;
		d[2 * i] = (char)(l > 127 ? 127 : l < -128 ? -128 : l);
		d[2 * i + 1] = (char)(r > 127 ? 127 : r < -128 ? -128 : r);
	}
}

static void run(const char *name, void (*f)(char *, long))
{
	long todo = HZ_OUT * SECS;
	double t0 = cpu(), t;
	while (todo > 0) {
		f(out, FRAMES);
		todo -= FRAMES;
	}
	t = cpu() - t0;
	printf("%-44s %5.1f%% of the CPU\n", name, 100.0 * t / SECS);
}

int main(void)
{
	int k, i, j;
	for (k = 0; k < NV; k++)
		for (i = 0; i < 8192; i++)
			smp[k][i] = (signed char)((i * (k + 3)) & 0xff);
	for (k = 0; k < 4; k++)
		for (i = 0; i < 256; i++)
			wave[k][i] = (unsigned char)(i ^ (k * 37));
	for (j = 0; j <= 64; j++)
		for (i = 0; i < 256; i++)
			voltab[j][i] = (signed char)(((signed char)i * j) >> 6),
			wvol[j][i] = voltab[j][i];
	for (k = 0; k < NV; k++) {
		v[k].s = smp[k]; v[k].pos = 0;
		v[k].inc = 0x8000 + k * 0x1234;		/* around 12-25 kHz samples */
		v[k].len = 8192UL << 16;
		v[k].vol = 48; v[k].l = 40; v[k].r = 20;
	}
	{	/* E must mix exactly what D mixes */
		static char od[FRAMES * 2]; int m, bad = 0; struct voice save[NV];
		for (m = 0; m < NV; m++) save[m] = v[m];
		mix_d(od, FRAMES); mix_d(od, FRAMES);
		for (m = 0; m < NV; m++) v[m] = save[m];
		mix_e(out, FRAMES); mix_e(out, FRAMES);
		for (m = 0; m < FRAMES * 2; m++) bad += od[m] != out[m];
		for (m = 0; m < NV; m++) v[m] = save[m];
		printf("E vs D: %d of %d bytes differ\n", bad, FRAMES * 2);
	}
	run("A  OpenUA today: 4 wavetables, mono", mix_a);
	run("B  8 voices, volume, hard L/R, per-sample", mix_b);
	run("C  8 voices, volume + pan, per-sample", mix_c);
	run("D  8 voices, volume, hard L/R, voice-major", mix_d);
	run("E  as D, 68030 assembly inner loop", mix_e);
	return 0;
}
