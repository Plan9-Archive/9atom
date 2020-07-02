#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "amd64.h"
#include "adr.h"

typedef	struct	Adrmap	Adrmap;
struct Adrmap {
	uintmem	base;
	uintmem	len;
	uint	type;
	uint	use;
	uint	memflags;
};

static struct {
	Lock;
	Adrmap	map[125];
	int	nmap;
	uvlong	pagecnt[Mlast];
} adr;

static char *tname[Alast+1] = {
	"none",
	"mem",
	"res",
	"reclaim",
	"nvs",
	"unusable",
	"disable",

	"apic",
	"pcibar",
	"mmio",
};

static char *uname[Mlast+1] = {
	"free",
	"ktext",
	"kpage",
	"upage",
	"vmap",
};

static int
fmta(Fmt *f)
{
	Adrmap *a;

	a = va_arg(f->args, Adrmap*);
	return fmtprint(f, "%.8s	%.8s	%#ux	%#.16P	%#.16P",
		uname[a->use], tname[a->type], a->memflags, a->base, a->base+a->len);
}
#pragma	varargck	type	"a"	Adrmap*

static long
adrread(Chan*, void *v, long n, vlong off)
{
	char *s, *p, *e;
	int i;
	Adrmap *a;

	s = smalloc(16*1024);
	e = s + 16*1024;
	p = s;
	for(i = 0; i < adr.nmap; i++){
		a = adr.map+i;
		p = seprint(p, e, "%a\n", a);
	}
	n = readstr(off, v, n, s);
	free(s);
	return n;
}

void
adrdump(void)
{
	int i;
	Adrmap *a;

	for(i = 0; i < adr.nmap; i++){
		a = adr.map+i;
		print("%a\n", a);
	}
}

static uvlong
npages(Adrmap *a)
{
	return (ROUNDDN(a->base+a->len, PGSZ) - ROUNDUP(a->base, PGSZ))/PGSZ;
}

void
insert(uintmem base, uintmem len, int type, int use, uint memflags)
{
	int i, n;
	Adrmap a;

	DBG("insert: %#P %.P %s %d\n", base, len, tname[type], use);
	if(adr.nmap == nelem(adr.map)){
		print("adr: map overflow\n");
		return;
	}

	lock(&adr);

	a = (Adrmap){base, len, type, use, memflags};
	n = npages(&a);
	if(0 && n <= 0){
		unlock(&adr);
		return;
	}

	for(i = adr.nmap; i >= 1 && adr.map[i-1].base >= a.base; i--)
		adr.map[i] = adr.map[i-1];
	adr.map[i] = a;
	adr.nmap++;

	adr.pagecnt[use] += n;

	unlock(&adr);
}

static Adrmap*
adrlook(uintmem pa, uintmem len)
{
	Adrmap *tab, *m;
	int n, i;
	
	tab = adr.map;
	n = adr.nmap;
	while(n > 0){
		i = n/2;
		m = tab+i;
		if(m->base <= pa){
			if(pa+len - m->base <= m->len)
				return m;
			tab += i+1;
			n -= i+1;
		}else
			n = i;
	}
	return nil;			
}

uint
adrmemflags(uintmem pa)
{
	int f;
	Adrmap *m;

	f = 0;
	lock(&adr);
	if((m = adrlook(pa, 8)) != nil)
		f = m->memflags;
	unlock(&adr);
	return f;
}

uintmem
adrmemtype(uintmem pa, uintmem *sz, int *type, int *use)
{
	Adrmap *m;
	uintmem r;

	r = 0;
	lock(&adr);
	if((m = adrlook(pa, 8)) != nil){
		if(sz != nil)
			*sz = m->len;
		if(type != nil)
			*type = m->type;
		if(use != nil)
			*use = m->use;
		r = m->base;
	}
	unlock(&adr);

	return r;
}

/*
 * Notes:
 * addrmapinit called from options.c:/^e820
 * subject to change.  system text map should be handled elsewhere.
 */
void
adrmapinit(uintmem base, uintmem len, int type, int use)
{
	switch(type){
	case Amemory:
		/*
		 * note: this should be moved into meminit/umeminit
		 *
		 * Adjust things for the peculiarities of this
		 * architecture.
		 * Sys->pmend is the largest physical memory address found,
		 * there may be gaps between it and sys->pmstart, the range
		 * and how much of it is occupied, might need to be known
		 * for setting up allocators later.
		 */
//		if(base < 1*MiB || base+len < sys->pmstart)
//			break;
//		if(base < sys->pmstart){
//			len -= sys->pmstart - base;
//			base = sys->pmstart;
//		}
		if(base >= 1*MiB && base+len < sys->pmstart)
			break;
		if(base >= 1*MiB && base < sys->pmstart){
			len -= sys->pmstart - base;
			base = sys->pmstart;
		}

		if(base+len > sys->pmend)
			sys->pmend = base+len;
	default:
		insert(base, len, type, use, 0);
		break;
	}
}

static int
trymerge(Adrmap *a, Adrmap *b)
{
	if(a->type == b->type)
	if(a->use == b->use)
	if(a->base+a->len == b->base){
		a->len += b->len;
		memmove(b, b+1, (adr.nmap-- - (b+1 - adr.map))*sizeof(Adrmap));
		return 1;
	}
	return 0;
}

static char*
tnam(int type)
{
	if(type == -1 || type >= nelem(tname))
		return "-1";
	return tname[type];
}

uintmem
adralloc(uintmem base, uintmem len, int align, int type, int use, uint flags)
{
	uintmem slop, adjlen, l;
	int i;
	Adrmap *a;

	DBG("adralloc: %#P:%#P, %s flags %#ux\n", base, len, tnam(type), flags);
	lock(&adr);
	for(i = 0; i < adr.nmap; i++){
		a = adr.map+i;
		if((type != -1 && a->type != type) || a->use != Mfree)
			continue;
		if(base != 0){
			if(base + len > a->base + a->len
			|| base < a->base)
				continue;
			/* add split */
			if(a->base < base){
				l = base - a->base;
				memmove(a+1, a, (adr.nmap++ - i)*sizeof(Adrmap));
				a->len = l;
				a[1].base += l;
				a[1].len -= l;
				continue;
			}
		}
		slop = align? ROUNDUP(a->base, align) - a->base: 0;
		adjlen = len + slop;
		if(a->len < adjlen)
			continue;
		base = a->base+slop;
		if(a->len > adjlen){
			memmove(a+1, a, (adr.nmap++ - i)*sizeof(Adrmap));
			a->len = adjlen;
			a[1].base += adjlen;
			a[1].len -= adjlen;
			if(0 && use == Mfree && i+1 < adr.nmap-1)
				trymerge(a+1, a+2);
		}
		a->use = use;
		a->memflags = flags;
		if(0 && use == Mfree && i>0)
			trymerge(a-1, a);
		unlock(&adr);
		return base;
	}
	unlock(&adr);

	print("adralloc: fail %#P len %#P type %s use %s align %d flags %#ux from %#p\n",
		base, len, tnam(type), uname[use], align, flags, getcallerpc(&base));
	adrdump();

	return 0;
}

void
adrfree(uintmem base, uintmem len)
{
	Adrmap *m;

	print("adrfree: %#P:%#P\n", base, len);
	lock(&adr);
	m = adrlook(base, len);
	if(m == nil || m->use == Mfree){
		unlock(&adr);
		print("should panic: bad adrfree %#P %#P", base, len);
	}
	m->use = Mfree;
	m->memflags = 0;
	if(m>adr.map && trymerge(m-1, m))
		m--;
	if(m <adr.map+adr.nmap && trymerge(m, m+1))
		m--;
	USED(m);
	unlock(&adr);
}

int
adrmatch(int i, int type, int use, uintmem *base, uintmem *sz)
{
	lock(&adr);
	
	for(; ++i < adr.nmap;){
		if(adr.map[i].type == type)
		if(adr.map[i].use == use){
			*base = adr.map[i].base;
			*sz = adr.map[i].len;
			iunlock(&adr);
			return i;
		}
	}

	iunlock(&adr);
	return -1;
}

/*
 * apics and i/o apics should be, but aren't always reserved memory.
 * insure that we've got an appropriate adr entry for such a beast.
 */
void
adrmapck(uintmem base, uintmem len, int type, int use)
{
	Adrmap *m;

	lock(&adr);
	m = adrlook(base, len);
	if(m == nil){
		unlock(&adr);
		insert(base, len, type, use, 0);
	}
	else{
		m->type = type;
		unlock(&adr);
		adralloc(base, len, 0, type, use, 0);
	}
}

/*
 * find enclosing address map.
 * only needed because externally, the map isn't visible.
 * if it were, then we'd want to export adrlook
 */
int
adrmapenc(uintmem *base, uintmem *len, int type, int use)
{
	Adrmap *m;

	lock(&adr);
	m = adrlook(*base, *len);
	if(m == nil){
		unlock(&adr);
		return -1;
	}
	else{
		m->type = type;
		m->use = use;
		*base = m->base;
		*len = m->len;
		unlock(&adr);
		return 0;
	}
}

void
adrinit(void)
{
	fmtinstall('a', fmta);
//	sys->pmstart = ROUNDUP(PADDR(end), BIGPGSZ);
	sys->pmstart = 0+INIMAP;
	sys->pmend = sys->pmstart;
	insert(1*MiB, sys->pmstart - 1*MiB, Amemory, Mktext, 0);	/* botch; hardcoded */
	addarchfile("adr", 0444, adrread, nil);
}

/* no malloc yet, so grab memory directly */
static uintmem
walkalloc(usize size)
{
	uintmem pa, w;

	assert(size == PTSZ && sys->vmunused+size <= sys->vmunmapped);
	if(!ALIGNED(sys->vmunused, PTSZ)){
		w = ROUNDUP(sys->vmunused, PTSZ);
		print("adr: walkalloc: %P wasted\n", w - sys->vmunused);
		sys->vmunused = w;
	}
	if((pa = mmuphysaddr(sys->vmunused)) != ~0)
		sys->vmunused += size;
	return pa;
}

void
meminit(void)
{
	int i, j, l, npg[4];
	Adrmap *a;
	PTE *pte, *pml4;
	uintptr va;
	uintmem hi, lo, mem, nextmem, pa;

//	adralloc(MiB, sys->pmstart, 0, Amemory, Mktext, 0);
	assert(!((sys->vmunmapped|sys->vmend) & m->pgszmask[1]));

	/* can pick any pa; expect INIMAP */
	pa = adralloc(0, sys->vmend - sys->vmunmapped, 0, Amemory, Mkpage, 0);
	if(pa == 0)
		panic("adr: insufficient contiguous memory for kernel %#P", pa);
	print("pa %#P (%#P %#P)\n", pa, sys->vmend, sys->vmunmapped);

	/* assume already 2MiB aligned*/
	assert(ALIGNED(sys->vmunmapped, 2*MiB));
	pml4 = UINT2PTR(m->pml4->va);
	while(sys->vmunmapped < sys->vmend){
		l = mmuwalk(pml4, sys->vmunmapped, 1, &pte, walkalloc);
		USED(l);
		DBG("%#p l %d\n", sys->vmunmapped, l);
		*pte = pa|PtePS|PteRW|PteP|PteG;
		sys->vmunmapped += 2*MiB;
		pa += 2*MiB;
		sys->kpages += 2*MiB/PGSZ;
		sys->npages += 2*MiB/PGSZ;
	}
	sys->ialloc = (sys->vmend - sys->vmstart - INIMAP)/4;

	/* everything else mapped at kseg2 s.t. pa = va - KSEG2 */
	memset(npg, 0, sizeof npg);
	for(j = 0; j < adr.nmap; j++){
		a = adr.map+j;
		if(a->type != Amemory || a->use != 0)
			continue;
		va = KSEG2+a->base;

		lo = a->base;
		hi = a->base+a->len;
		print("adr: mem %#P %#P size %P\n", lo, hi, a->len);

		/* Convert a range into pages */
		for(mem = lo; mem < hi; mem = nextmem){
			nextmem = (mem + PGLSZ(0)) & ~m->pgszmask[0];

			/* Try large pages first */
			for(i = m->npgsz - 1; i >= 0; i--){
				if((mem & m->pgszmask[i]) != 0)
					continue;
				if(mem + PGLSZ(i) > hi)
					continue;
				/* This page fits entirely within the range. */
				/* Mark it a usable */
				if((l = mmuwalk(pml4, va, i, &pte, walkalloc)) < 0)
					panic("adr: meminit3");

				*pte = mem|PteRW|PteP|PteG;
				if(l > 0)
					*pte |= PtePS;

				nextmem = mem + PGLSZ(i);
				va += PGLSZ(i);
				npg[i]++;
				break;
			}
		}
	}
	sys->meminit = 1;
	print("%d %d %d\n", npg[0], npg[1], npg[2]);
}

void
umeminit(void)
{
	int i;
	Adrmap *a;

	for(i = 0; i < adr.nmap; i++){
		a = adr.map+i;
		if(a->type != Amemory || a->use != 0)
			continue;
		adr.pagecnt[a->use] -= npages(a);
		a->use = Mupage;
		adr.pagecnt[Mupage] += npages(a);
		physinit(a->base, a->len);
	}
	physallocdump();
}
