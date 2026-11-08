/*
 * regex.c - regcomp/regexec/regfree on libgen's regcmp/regex (see regex.h).
 */
#include <stdlib.h>
#include <string.h>
#include "regex.h"

extern char *regcmp(const char *, ...);
extern char *regex(const char *, const char *, ...);

/* the "]" that ends the bracket expression at p ("[...]"), or NULL: a "]"
 * right after "[" or "[^" is one of its characters, and so is a "\\" */
static const char *bracket_end(const char *p, const char *end)
{
	p++;
	if (p < end && *p == '^')
		p++;
	if (p < end && *p == ']')
		p++;
	while (p < end && *p != ']')
		p++;
	return p < end ? p : NULL;
}

/* one alternative in regcmp's terms: "?" becomes {0,1}, but not in a
 * bracket expression */
static char *compile(const char *p, size_t len)
{
	char *s = malloc(len * 5 + 1), *o = s, *re;
	const char *e;
	size_t i;

	if (s == NULL)
		return NULL;
	for (i = 0; i < len; i++) {
		if (p[i] == '[' && (e = bracket_end(p + i, p + len)) != NULL) {
			memcpy(o, p + i, e - (p + i) + 1);
			o += e - (p + i) + 1;
			i = e - p;
		} else if (p[i] == '\\' && i + 1 < len) {
			*o++ = p[i++];
			*o++ = p[i];
		} else if (p[i] == '?') {
			memcpy(o, "{0,1}", 5);
			o += 5;
		} else
			*o++ = p[i];
	}
	*o = '\0';
	re = regcmp(s, (char *)0);
	free(s);
	return re;
}

int regcomp(regex_t *preg, const char *pattern, int cflags)
{
	const char *p, *start = pattern, *end = pattern + strlen(pattern);
	int depth = 0, n = 1;

	(void)cflags;
	for (p = pattern; *p; p++)
		if (*p == '|')
			n++;
	preg->n = 0;
	if ((preg->re = malloc(n * sizeof(char *))) == NULL)
		return REG_ESPACE;
	/* split at the "|"s outside groups and brackets */
	for (p = pattern;; p++) {
		if (*p == '[') {
			if ((p = bracket_end(p, end)) == NULL)
				break;
			continue;
		}
		if (*p == '\\' && p[1]) {
			p++;
			continue;
		}
		if (*p == '(')
			depth++;
		else if (*p == ')')
			depth--;
		else if ((*p == '|' && depth == 0) || *p == '\0') {
			char *re = compile(start, p - start);

			if (re == NULL) {
				regfree(preg);
				return REG_BADPAT;
			}
			preg->re[preg->n++] = re;
			if (*p == '\0')
				return 0;
			start = p + 1;
		}
	}
	regfree(preg);
	return REG_BADPAT;
}

int regexec(const regex_t *preg, const char *string, size_t nmatch,
	    regmatch_t pmatch[], int eflags)
{
	int i;

	(void)nmatch;
	(void)pmatch;
	(void)eflags;
	for (i = 0; i < preg->n; i++)
		if (regex(preg->re[i], string, (char *)0) != NULL)
			return 0;
	return REG_NOMATCH;
}

void regfree(regex_t *preg)
{
	int i;

	for (i = 0; i < preg->n; i++)
		free(preg->re[i]);
	free(preg->re);
	preg->n = 0;
	preg->re = NULL;
}
