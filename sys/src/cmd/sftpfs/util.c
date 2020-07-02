/* Copyright © 2008 Fazlul Shahriar*/

#include "fxp.h"

char*
estrstrdup(char *s, char *t)
{
	char *u;

	u = emalloc9p(strlen(s)+strlen(t)+1);
	strcpy(u, s);
	strcat(u, t);
	return u;
}

char*
estr3dup(char *s, char *t, char *r)
{
	char *u;

	u = emalloc9p(strlen(s)+strlen(t)+strlen(r)+1);
	strcpy(u, s);
	strcat(u, t);
	strcat(u, r);
	return u;
}

char*
eappend(char *s, char *sep, char *t)
{
	char *u;

	if(t == nil)
		u = estrstrdup(s, sep);
	else{
		u = emalloc9p(strlen(s)+strlen(sep)+strlen(t)+1);
		strcpy(u, s);
		strcat(u, sep);
		strcat(u, t);
	}
	free(s);
	return u;
}

char*
egrow(char *s, char *sep, char *t)
{
	s = eappend(s, sep, t);
	free(t);
	return s;
}

void
hexdump(char *cmt, uchar *buf, int len)
{
	Biobuf *bo;
	int i;
	
	bo = emalloc9p(sizeof *bo);
	Binit(bo, 1, OWRITE);
	Bprint(bo, "%s", cmt);
	for(i = 0; i < len; i++)
		Bprint(bo, "%02X ", buf[i]);
	Bprint(bo, "\n");
	Bterm(bo);
	free(bo);
}
