#include <u.h>
#include <libc.h>
#include "runeclass.h"

static Rune*
bsearchs(Rune c, Rune **t, int n, int ne)
{
	Rune **p;
	int m;

	while(n > 1) {
		m = n/2;
		p = t + m*ne;
		if(c >= p[0][0]) {
			t = p;
			n = n-m;
		} else
			n = m;
	}
	if(n && c >= t[0][0])
		return t[0];
	return 0;
}

Rune*
runeclass(Rune c)
{
	Rune *p;

	p = bsearchs(c, __unfoldbase, nelem(__unfoldbase), 1);
	if(p && *p == c)
		return p;
	return nil;
}
