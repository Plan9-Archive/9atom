/* very primative draw allocator with no compaction */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

#define	compactmalloc(x)		malloc(x)
#define	compactfree(pool, x)	free(x)
#define Overhead	(sizeof(Memdata*)+sizeof(uintptr))

/* conflict for %P and %R; this is a gross fix. */
#ifdef notdef
int
Pfmt(Fmt *f)
{
	extern int fmtP(Fmt*);

	return fmtP(f);
}

int
Rfmt(Fmt *f)
{
	extern int fmtR(Fmt*);

	return fmtR(f);
}
#endif

void
memdrawallocinit(void)
{
}

static void
·memimagemove(void *from, void *to)
{
	Memdata *md;

	md = *(Memdata**)to;
	if(md->base != from){
		print("compacted data not right: #%p\n", md->base);
		abort();
	}
	md->base = to;

	/* if allocmemimage changes this must change too */
	md->bdata = (uchar*)md->base+Overhead;
}

Memimage*
allocmemimaged(Rectangle r, ulong chan, Memdata *md)
{
	int d;
	ulong l;
	Memimage *i;

	if(Dx(r) <= 0 || Dy(r) <= 0){
		werrstr("bad rectangle %R", r);
		return nil;
	}
	if((d = chantodepth(chan)) == 0) {
		werrstr("bad channel descriptor %.8lux", chan);
		return nil;
	}

	l = wordsperline(r, d);

	i = mallocz(sizeof(Memimage), 1);
	if(i == nil)
		return nil;

	i->data = md;
	i->zero = sizeof(ulong)*l*r.min.y;
	
	if(r.min.x >= 0)
		i->zero += (r.min.x*d)/8;
	else
		i->zero -= (-r.min.x*d+7)/8;
	i->zero = -i->zero;
	i->width = l;
	i->r = r;
	i->clipr = r;
	i->flags = 0;
	i->layer = nil;
	i->cmap = memdefcmap;
	if(memsetchan(i, chan) < 0){
		free(i);
		return nil;
	}
	return i;
}

Memimage*
allocmemimage(Rectangle r, ulong chan)
{
	int d;
	uchar *p;
	ulong l, nw;
	Memdata *md;
	Memimage *i;

	if((d = chantodepth(chan)) == 0) {
		werrstr("bad channel descriptor %.8lux", chan);
		return nil;
	}

	l = wordsperline(r, d);
	nw = l*Dy(r);
	md = malloc(sizeof(Memdata));
	if(md == nil)
		return nil;

	md->ref = 1;
	md->base = compactmalloc(Overhead+nw*sizeof(ulong));
	if(md->base == nil){
		free(md);
		return nil;
	}

	p = (uchar*)md->base;
	*(Memdata**)p = md;
	p += sizeof(Memdata*);

	*(uintptr*)p = getcallerpc(&r);
	p += sizeof(uintptr);

	/* if this changes, memimagemove must change too */
	md->bdata = p;
	md->allocd = 1;

	i = allocmemimaged(r, chan, md);
	if(i == nil){
		compactfree(imagmem, md->base);
		free(md);
		return nil;
	}
	md->imref = i;
	return i;
}

void
freememimage(Memimage *i)
{
	if(i == nil)
		return;
	if(i->data->ref-- == 1 && i->data->allocd){
		if(i->data->base)
			compactfree(imagmem, i->data->base);
		free(i->data);
	}
	free(i);
}
