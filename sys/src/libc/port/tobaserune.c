#include <u.h>
#include <libc.h>
#include "tobaserune.h"

static Rune*
bsearch(Rune c, Rune *t, int n, int ne)
{
	Rune *p;
	int m;

	while(n > 1) {
		m = n/2;
		p = t + m*ne;
		if(c >= p[0]) {
			t = p;
			n = n-m;
		} else
			n = m;
	}
	if(n && c >= t[0])
		return t;
	return 0;
}

Rune
tobaserune(Rune c)
{
	Rune *p;

	p = bsearch(c, __base2, nelem(__base2)/2, 2);
	if(p && *p == c)
		c = p[1];
	return c;
}

Rune
isbaserune(Rune c)
{
	return tobaserune(c) == c;
}
