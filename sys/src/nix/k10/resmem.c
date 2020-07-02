#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"../port/error.h"
#include	"amd64.h"
#include	"adr.h"

typedef	struct	Rseg	Rseg;
struct	Rseg {
	uintmem	base;
	uintmem	len;
	uchar	*u;
};

Rseg	rsegtab[] = {
	{0,		PGSZ,		nil, },
	{PGSZ,		MiB-PGSZ,	nil },
	{0xfff00000,	1,		nil, },
};

Rseg	wrentab[] = {
	{0xa0000, 0xc0000-0xa0000, nil, },		/* mda/vga range */
};

struct {
	Lock;
	int	once;
	int	fail;
} rseg;

static void
rseginit(void)
{
	int i;
	Rseg *r;

	lock(&rseg);
	if(rseg.once == 1){
		unlock(&rseg);
		return;
	}
	rseg.once = 1;
	for(i = 0; i < nelem(rsegtab); i++){
		r = rsegtab + i;
		if(r->base < MiB || adrmapenc(&r->base, &r->len, Mvmap, Mfree) == 0){
			if(r->base == 0)
				r->u = KADDR(0);
			else
				r->u = vmap(r->base, r->len);
			if(r->u == nil)
				rseg.fail++;
		}
	}
	unlock(&rseg);
}

static int
resmemwchk(vlong o, usize n)
{
	Rseg *w;

	for(w = wrentab; w < wrentab + nelem(wrentab); w++)
		if(o >= w->base)
		if(o + n <= w->base + w->len)
			return 0;
	return -1;
}

static long
resmemrw(int isr, void *va, long n, vlong off)
{
	uchar *a;
	long chunk, b;
	Rseg *r, *e;

	a = va;
	if(off < 0 || n < 0)
		error("bad offset/count");
	rseginit();
	if(rseg.fail)
		error("can't vmap");

	r = rsegtab;
	e = r + nelem(rsegtab);
	while(n > 0){
		if(r == e || off < r->base)
			error("unmapped");
		if(off >= r->base + r->len){
			r++;
			continue;
		}

		chunk = n;
		if(chunk > r->len)
			chunk = r->len;
		b = off - r->base;
		if(b + chunk > r->len)
			chunk = r->len - b;
		if(isr)
			memmove(a, r->u + b, chunk);
		else if(resmemwchk(off, chunk) == 0)
			memmove(r->u + b, a, chunk);
		else
			error(Eperm);
		a += chunk;
		off += chunk;
		n -= chunk;
		if(!isr)
			break;
	}
	return a - (uchar*)va;
}

static long
resmemread(Chan*, void *a, long n, vlong off)
{
	return resmemrw(1, a, n, off);
}

static long
resmemwrite(Chan*, void *a, long n, vlong off)
{
	return resmemrw(0, a, n, off);
}

void
resmemlink(void)
{
	addarchfile("realmodemem", 0660, resmemread, resmemwrite);		/* legacy crap */
	addarchfile("resmem", 0660, resmemread, resmemwrite);
}
