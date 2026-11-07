/*
 * regex.h - the POSIX calls JWM makes (regcomp, regexec, regfree; match.c,
 * for the patterns in Group, Include and the like) on SVR4.0's libgen
 * regcmp/regex, which is all Atari System V has. Extended expressions as
 * regcmp knows them (. [] * + {m,n} ^ $ and groups), "?" as {0,1}, and
 * "|" between whole alternatives (not inside a group). No match offsets.
 */
#ifndef ASV_REGEX_H
#define ASV_REGEX_H
#include <stddef.h>

typedef struct {
	int n;			/* alternatives */
	char **re;		/* each one compiled by regcmp */
} regex_t;
typedef struct {
	int rm_so, rm_eo;
} regmatch_t;

#define REG_EXTENDED	1
#define REG_ICASE	2
#define REG_NOSUB	4
#define REG_NEWLINE	8
#define REG_NOMATCH	1
#define REG_BADPAT	2
#define REG_ESPACE	12

int regcomp(regex_t *preg, const char *pattern, int cflags);
int regexec(const regex_t *preg, const char *string, size_t nmatch,
	    regmatch_t pmatch[], int eflags);
void regfree(regex_t *preg);
#endif
