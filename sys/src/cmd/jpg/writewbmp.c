#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <ctype.h>
#include <bio.h>
#include <flate.h>
#include "imagefile.h"

enum{
	Digits = 2*sizeof(ulong),
};

static int
Bputint(Biobufhdr *b, int i)
{
	char buf[Digits];
	int n;

	n = Digits;
	buf[--n] = i&127;
	i /= 127;
	for(; i; i /= 128)
		buf[--n] = i&127 | 0x80;
	return Bwrite(b, buf+n, Digits-n);

}

static Memimage*
mk1bit(Memimage *m)
{
	Memimage *n;

	n = allocmemimage(m->r, GREY1);
	memimagedraw(n, n->r, m, ZP, nil, ZP, SoverD);
	return n;
}

// we need to do this line-by-line because image scan lines are word aligned.
// wbmp scan lines are byte aligned.
static int
writeimgbits(Biobuf *b, Memimage *m)
{
	int r, w;

	w = (Dx(m->r)+7)/8;
	for(r = m->r.min.y; r < m->r.max.y; r++)
		if(Bwrite(b, byteaddr(m, Pt(0, r)), w) == -1)
			return -1;
	return 0;
}

char*
memwritewbmp(Biobuf *b, Memimage *m, ImageInfo *)
{
	Memimage *n;
	int i;

	Bputint(b, 0);
	Bputint(b, 0);
	Bputint(b, Dx(m->r));
	Bputint(b, Dy(m->r));
	n = mk1bit(m);
	i = writeimgbits(b, n);
	freememimage(n);
	if(i < 0)
		return "write error";
	return 0;
}
