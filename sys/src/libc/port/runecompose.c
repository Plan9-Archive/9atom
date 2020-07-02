#include <u.h>
#include <libc.h>
#include "runecompose.h"

static uint*
bsearch32(uint c, uint *t, int n, int ne)
{
	uint *p;
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
	if(n && c == t[0])
		return t;
	return 0;
}

static uvlong*
bsearch64(uvlong c, uvlong *t, int n, int ne)
{
	uvlong *p;
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
	if(n && c == t[0])
		return t;
	return 0;
}

int
runecompose(Rune b, Rune c)
{
	uint *p;
	uvlong *q;

	if(c <= 0xffff){
		p = bsearch32(b<<16 | c, __combine2, nelem(__combine2)/2, 2);
		if(p)
			return p[1];
	}
	q = bsearch64((uvlong)b<<32 | c, __combine264, nelem(__combine264)/2, 2);
	if(q)
		return q[1];
	return -1;
}
