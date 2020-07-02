#include <u.h>
#include <libc.h>
#include "runetype.h"

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
tolowerrune(Rune c)
{
	Rune *p;

	p = bsearch(c, __tolower2, nelem(__tolower2)/3, 3);
	if(p && c >= p[0] && c <= p[1])
		return p[2] + (c - p[0]);
	p = bsearch(c, __tolower1, nelem(__tolower1)/2, 2);
	if(p && c == p[0] && p[2])
		return p[2] + (c - p[0]);
	return c;
}

Rune
toupperrune(Rune c)
{
	Rune *p;

	p = bsearch(c, __toupper2, nelem(__toupper2)/3, 3);
	if(p && c >= p[0] && c <= p[1])
		return p[2] + (c - p[0]);
	p = bsearch(c, __toupper1, nelem(__toupper1)/2, 2);
	if(p && c == p[0] && p[2])
		return p[2] + (c - p[0]);
	return c;
}

Rune
totitlerune(Rune c)
{
	Rune *p;

	p = bsearch(c, __totitle1, nelem(__totitle1)/2, 2);
	if(p && c == p[0])
		return p[2] + (c - p[0]);
	return c;
}

int
islowerrune(Rune c)
{
	Rune *p;

	p = bsearch(c, __toupper2, nelem(__toupper2)/3, 3);
	if(p && c >= p[0] && c <= p[1])
		return 1;
	p = bsearch(c, __toupper1, nelem(__toupper1)/2, 2);
	if(p && c == p[0])
		return 1;
	return 0;
}

int
isupperrune(Rune c)
{
	Rune *p;

	p = bsearch(c, __tolower2, nelem(__tolower2)/3, 3);
	if(p && c >= p[0] && c <= p[1])
		return 1;
	p = bsearch(c, __tolower1, nelem(__tolower1)/2, 2);
	if(p && c == p[0])
		return 1;
	return 0;
}

int
isalpharune(Rune c)
{
	Rune *p;

	if(isupperrune(c) || islowerrune(c))
		return 1;
	p = bsearch(c, __alpha2, nelem(__alpha2)/2, 2);
	if(p && c >= p[0] && c <= p[1])
		return 1;
	p = bsearch(c, __alpha1, nelem(__alpha1), 1);
	if(p && c == p[0])
		return 1;
	return 0;
}

int
istitlerune(Rune c)
{
	return isupperrune(c) && islowerrune(c);
}

int
isspacerune(Rune c)
{
	Rune *p;

	p = bsearch(c, __space2, nelem(__space2)/2, 2);
	if(p && c >= p[0] && c <= p[1])
		return 1;
	return 0;
}

int
isdigitrune(Rune c)
{
	Rune *p;

	p = bsearch(c, __digit2, nelem(__digit2)/2, 2);
	if(p && c >= p[0] && c <= p[1])
		return 1;
	return 0;
}

int
digitrunevalue(Rune c)
{
	Rune* p;

	p = bsearch(c, __digit2, nelem(__digit2)/2, 2);
	if (!p || c < p[0] || c>p[1])
		return -1;
	return c-p[0];
}
