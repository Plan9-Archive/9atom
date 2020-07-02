#include "compose.h"

static void
compose(Memimage *screen, Memimage *im, ulong op)
{
	memdraw(screen, screen->r, im, ZP, nil, ZP, op);
}

void
composer(C *c, int n, ulong chan, ulong op)
{
	int i;
	Memimage *im;

	memimageinit();
	im = pageimg(c + 0, chan);
	for(i = 1; i < n; i++)
		compose(im, pageimg(c + i, 0), op);
	writememimage(1, im);
}
