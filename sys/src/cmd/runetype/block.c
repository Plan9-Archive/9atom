#include <u.h>
#include <libc.h>
#include "block.h"

static Ublock*
bsearch(Rune c, Ublock *t, int n)
{
	Ublock *p;
	int m;

	while(n > 1) {
		m = n/2;
		p = t + m;
		if(c >= p->r[0]) {
			t = p;
			n = n-m;
		} else
			n = m;
	}
	if(n == 1 && c <= t->r[1])
		return t;
	return nil;
}

void
usage(void)
{
	fprint(2, "usage: rune/block rune ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i;
	Rune r;
	Ublock *t;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	for(i = 0; i < argc; i++){
		if(chartorune(&r, argv[i]) > 0)
			t = bsearch(r, ucblock, nelem(ucblock));
		else{
			r = strtoul(argv[i], nil, 0);
			t = bsearch(r, ucblock, nelem(ucblock));
		}
		if(t != nil)
			print("%s\n", t->name);
	}
}
