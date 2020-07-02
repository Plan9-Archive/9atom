#include <u.h>
#include <goo.h>
#include "snap.h"

static char *oom = "out of memory";

void*
emalloc(ulong n)
{
	void *v;

	v = malloc(n);
	if(!v)
		sysfatal(oom);
	memset(v, 0, n);
	return v;
}

void*
erealloc(void *v, ulong n)
{
	v = realloc(v, n);
	if(!v)
		sysfatal(oom);
	return v;
}

char*
estrdup(char *s)
{
	char *t;
	ulong l;

	l = strlen(s);
	t = emalloc(l+1);
	memmove(t, s, l);
	t[l] = 0;
	return t;
}
