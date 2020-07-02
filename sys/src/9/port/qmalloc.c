/*
 * malloc
 *
 *	Uses Quickfit (see SIGPLAN Notices October 1988)
 *	with allocator from Kernighan & Ritchie
 *
 * This is a placeholder.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#define ALIGNED(p, a)	(((uintptr)(p) & ((a)-1))==0)

typedef double Align;
typedef union Header Header;
typedef struct Qlist Qlist;

union Header {
	struct {
		Header*	next;
		uint	size;
	} s;
	Align	al;
};

struct Qlist {
	Lock	lk;
	Header*	first;

	uint	nalloc;
};

enum {
	Unitsz		= sizeof(Header),	/* 16 bytes on amd64 */
};

#define	NUNITS(n)	(HOWMANY(n, Unitsz) + 1)
#define	NQUICK		((512/Unitsz)+1)	/* 33 on amd64 */

static	Qlist	quicklist[NQUICK+1];
static	Header	misclist;
static	Header	*rover;
static	unsigned tailsize;
static	unsigned tailnunits;
static	Header	*tailbase;
static	Header	*tailptr;
static	Header	checkval;
static	int	morecore(uint);

enum
{
	QSmalign = 0,
	QSmalignquick,
	QSmalignrover,
	QSmalignfront,
	QSmalignback,
	QSmaligntail,
	QSmalignnottail,
	QSmalloc,
	QSmallocrover,
	QSmalloctail,
	QSfree,
	QSfreetail,
	QSfreequick,
	QSfreenext,
	QSfreeprev,
	QSmax
};

static	void	qfreeinternal(void*);
static	int	qstats[QSmax];
static	char*	qstatstr[QSmax] = {
[QSmalign]		"malign",
[QSmalignquick]	"malignquick",
[QSmalignrover]	"malignrover",
[QSmalignfront]	"malignfront",
[QSmalignback]	"malignback",
[QSmaligntail]	"maligntail",
[QSmalignnottail]	"malignnottail",
[QSmalloc]		"malloc",
[QSmallocrover]	"mallocrover",
[QSmalloctail]	"malloctail",
[QSfree]		"free",
[QSfreetail]	"freetail",
[QSfreequick]	"freequick",
[QSfreenext]	"freenext",
[QSfreeprev]	"freeprev",
};

static	Lock		mainlock;

#define	MLOCK		ilock(&mainlock)
#define	MUNLOCK		iunlock(&mainlock)
#define QLOCK(l)	ilock(l)
#define QUNLOCK(l)	iunlock(l)

#define	tailalloc(p, n)	((p)=tailptr, tailsize -= (n), tailptr+=(n),\
			 (p)->s.size=(n), (p)->s.next = &checkval)

#define ISPOWEROF2(x)	(/*((x) != 0) && */((x) & ((x)-1)) == 0)
#define ALIGNHDR(h, a)	(Header*)((((uintptr)(h))+((a)-1)) & ~((a)-1))

static void*
qmalloc(usize nbytes)
{
	Qlist *qlist;
	Header *p, *q;
	uint nunits, n;

	qstats[QSmalloc]++;
	nunits = NUNITS(nbytes);
	if(nunits <= NQUICK){
		qlist = &quicklist[nunits];
		QLOCK(&qlist->lk);
		if((p = qlist->first) != nil){
			qlist->first = p->s.next;
			qlist->nalloc++;
			QUNLOCK(&qlist->lk);
			p->s.next = &checkval;
			return p+1;
		}
		QUNLOCK(&qlist->lk);
	}

	MLOCK;
	if(nunits > tailsize) {
		/* hard way */
		if((q = rover) != nil){
			do {
				p = q->s.next;
				if(p->s.size >= nunits) {
					if(p->s.size > nunits) {
						p->s.size -= nunits;
						p += p->s.size;
						p->s.size = nunits;
					} else
						q->s.next = p->s.next;
					p->s.next = &checkval;
					rover = q;
					qstats[QSmallocrover]++;
					MUNLOCK;
					return p+1;
				}
			} while((q = p) != rover);
		}
		if((n = morecore(nunits)) == 0){
			MUNLOCK;
			return nil;
		}
		tailsize += n;
	}
	qstats[QSmalloctail]++;
	tailalloc(p, nunits);
	MUNLOCK;

	return p+1;
}

static void*
qmallocalign(usize nbytes, uintptr align, long offset, usize span)
{
	Qlist *qlist;
	uintptr aligned;
	Header **pp, *p, *q, *r;
	uint naligned, nunits, n;

	if(nbytes == 0 || offset != 0 || span != 0)
		return nil;

	if(!ISPOWEROF2(align))
		panic("qmallocalign");

	if(align <= sizeof(Align))
		return qmalloc(nbytes);

	qstats[QSmalign]++;
	nunits = NUNITS(nbytes);
	if(nunits <= NQUICK){
		/*
		 * Look for a conveniently aligned block
		 * on one of the quicklists.
		 */
		qlist = &quicklist[nunits];
		QLOCK(&qlist->lk);
		pp = &qlist->first;
		for(p = *pp; p != nil; p = p->s.next){
			if(ALIGNED(p+1, align)){
				*pp = p->s.next;
				p->s.next = &checkval;
				QUNLOCK(&qlist->lk);
				qstats[QSmalignquick]++;
				return p+1;
			}
			pp = &p->s.next;
		}
		QUNLOCK(&qlist->lk);
	}

	MLOCK;
	if(nunits > tailsize) {
		/* hard way */
		if((q = rover) != nil){
			do {
				p = q->s.next;
				if(p->s.size < nunits)
					continue;
				aligned = ALIGNED(p+1, align);
				naligned = NUNITS(align)-1;
				if(!aligned && p->s.size < nunits+naligned)
					continue;

				/*
				 * This block is big enough, remove it
				 * from the list.
				 */
				q->s.next = p->s.next;
				rover = q;
				qstats[QSmalignrover]++;

				/*
				 * Free any runt in front of the alignment.
				 */
				if(!aligned){
					r = p;
					p = ALIGNHDR(p+1, align) - 1;
					n = p - r;
					p->s.size = r->s.size - n;

					r->s.size = n;
					r->s.next = &checkval;
					qfreeinternal(r+1);
					qstats[QSmalignfront]++;
				}

				/*
				 * Free any residue after the aligned block.
				 */
				if(p->s.size > nunits){
					r = p+nunits;
					r->s.size = p->s.size - nunits;
					r->s.next = &checkval;
					qstats[QSmalignback]++;
					qfreeinternal(r+1);

					p->s.size = nunits;
				}

				p->s.next = &checkval;
				MUNLOCK;
				return p+1;
			} while((q = p) != rover);
		}
		if((n = morecore(nunits)) == 0){
			MUNLOCK;
			return nil;
		}
		tailsize += n;
	}

	q = ALIGNHDR(tailptr+1, align);
	if(q == tailptr+1){
		tailalloc(p, nunits);
		qstats[QSmaligntail]++;
	}
	else{
		naligned = NUNITS(align)-1;
		if(tailsize < nunits+naligned){
			/*
			 * There are at least nunits,
			 * get enough for alignment.
			 */
			if((n = morecore(naligned)) == 0){
				MUNLOCK;
				return nil;
			}
			tailsize += n;
		}
		/*
		 * Save the residue before the aligned allocation
		 * and free it after the tail pointer has been bumped
		 * for the main allocation.
		 */
		n = q-tailptr - 1;
		tailalloc(r, n);
		tailalloc(p, nunits);
		qstats[QSmalignnottail]++;
		qfreeinternal(r+1);
	}
	MUNLOCK;

	return p+1;
}

/*
 * TODO: should not gain the MLOCK for
 * releasing memory into the quicklist.
 */
static void
qfreeinternal(void* ap)
{
	Qlist *qlist;
	Header *p, *q;
	uint nunits;

	if(ap == nil)
		return;
	qstats[QSfree]++;

	p = (Header*)ap - 1;
	if((nunits = p->s.size) == 0 || p->s.next != &checkval)
		panic("malloc: corrupt allocation arena");
	if(tailptr != nil && p+nunits == tailptr) {
		/* block before tail */
		tailptr = p;
		tailsize += nunits;
		qstats[QSfreetail]++;
		return;
	}
	if(nunits <= NQUICK) {
		qlist = &quicklist[nunits];
		QLOCK(&qlist->lk);
		p->s.next = qlist->first;
		qlist->first = p;
		QUNLOCK(&qlist->lk);
		qstats[QSfreequick]++;
		return;
	}
	if((q = rover) == nil) {
		q = &misclist;
		q->s.size = 0;
		q->s.next = q;
	}
	for(; !(p > q && p < q->s.next); q = q->s.next)
		if(q >= q->s.next && (p > q || p < q->s.next))
			break;
	if(p+p->s.size == q->s.next) {
		p->s.size += q->s.next->s.size;
		p->s.next = q->s.next->s.next;
		qstats[QSfreenext]++;
	} else
		p->s.next = q->s.next;
	if(q+q->s.size == p) {
		q->s.size += p->s.size;
		q->s.next = p->s.next;
		qstats[QSfreeprev]++;
	} else
		q->s.next = p;
	rover = q;
}

ulong
msize(void* ap)
{
	Header *p;
	uint nunits;

	if(ap == nil)
		return 0;

	p = (Header*)ap - 1;
	if((nunits = p->s.size) == 0 || p->s.next != &checkval)
		panic("malloc: corrupt allocation arena");

	return (nunits-1) * sizeof(Header);
}

static void
mallocreadfmt(char* s, char* e)
{
	char *p;
	Header *q;
	int i, n, t;
	Qlist *qlist;

	p = seprint(s, e,
		"%llud memory\n"
		"%d pagesize\n"
		"%llud kernel\n",
		(uvlong)conf.npage*BY2PG,
		BY2PG,
		(uvlong)conf.npage-conf.upages);

	t = 0;
	for(i = 0; i <= NQUICK; i++) {
		n = 0;
		qlist = &quicklist[i];
		QLOCK(&qlist->lk);
		for(q = qlist->first; q != nil; q = q->s.next){
//			if(q->s.size != i)
//				p = seprint(p, e, "q%d\t%#p\t%ud\n",
//					i, q, q->s.size);
			n++;
		}
		QUNLOCK(&qlist->lk);

//		if(n != 0)
//			p = seprint(p, e, "q%d %d\n", i, n);
		t += n * i*sizeof(Header);
	}
	p = seprint(p, e, "quick: %ud bytes total\n", t);

	MLOCK;
	if((q = rover) != nil){
		i = t = 0;
		do {
			t += q->s.size;
			i++;
//			p = seprint(p, e, "m%d\t%#p\n", q->s.size, q);
		} while((q = q->s.next) != rover);

		p = seprint(p, e, "rover: %d blocks %ud bytes total\n",
			i, t*sizeof(Header));
	}
	p = seprint(p, e, "total allocated %lud, %ud remaining\n",
		(tailptr-tailbase)*sizeof(Header), tailnunits*sizeof(Header));

	for(i = 0; i < nelem(qstats); i++){
		if(qstats[i] == 0)
			continue;
		p = seprint(p, e, "%s %ud\n", qstatstr[i], qstats[i]);
	}
	MUNLOCK;
}

long
mallocreadsummary(Chan*, void *a, long n, long offset)
{
	char *alloc;

	alloc = malloc(16*READSTR);
	if(waserror()){
		free(alloc);
		nexterror();
	}
	mallocreadfmt(alloc, alloc+16*READSTR);
	n = readstr(offset, a, n, alloc);
	poperror();
	free(alloc);

	return n;
}

void
mallocsummary(void)
{
	Header *q;
	int i, n, t;
	Qlist *qlist;

	t = 0;
	for(i = 0; i <= NQUICK; i++) {
		n = 0;
		qlist = &quicklist[i];
		QLOCK(&qlist->lk);
		for(q = qlist->first; q != nil; q = q->s.next){
			if(q->s.size != i)
				print("q%d\t%#p\t%ud\n", i, q, q->s.size);
			n++;
		}
		QUNLOCK(&qlist->lk);

		t += n * i*sizeof(Header);
	}
	print("quick: %ud bytes total\n", t);

	MLOCK;
	if((q = rover) != nil){
		i = t = 0;
		do {
			t += q->s.size;
			i++;
		} while((q = q->s.next) != rover);
	}
	MUNLOCK;

	if(i != 0){
		print("rover: %d blocks %ud bytes total\n",
			i, t*sizeof(Header));
	}
	print("total allocated %lud, %ud remaining\n",
		(tailptr-tailbase)*sizeof(Header), tailnunits*sizeof(Header));

	for(i = 0; i < nelem(qstats); i++){
		if(qstats[i] == 0)
			continue;
		print("%s %ud\n", qstatstr[i], qstats[i]);
	}
}

void
free(void* ap)
{
	MLOCK;
	qfreeinternal(ap);
	MUNLOCK;
}

void*
malloc(ulong size)
{
	void* v;

	if((v = qmalloc(size)) != nil)
		memset(v, 0, size);

	return v;
}

void*
mallocz(ulong size, int clr)
{
	void *v;

	if((v = qmalloc(size)) != nil && clr)
		memset(v, 0, size);

	return v;
}

void*
mallocalign(ulong nbytes, ulong align, long offset, ulong span)
{
	void *v;

	if(span != 0 && align <= span){
		if(nbytes > span)
			return nil;
		align = span;
		span = 0;
	}

	/*
	 * Should this memset or should it be left to the caller?
	 */
	if((v = qmallocalign(nbytes, align, offset, span)) != nil)
		memset(v, 0, nbytes);

	return v;
}

void*
smalloc(ulong size)
{
	void *v;

	while((v = malloc(size)) == nil)
		tsleep(&up->sleep, return0, 0, 100);
	memset(v, 0, size);

	return v;
}

void*
realloc(void* ap, ulong size)
{
	void *v;
	Header *p;
	ulong osize;
	uint nunits, ounits;
	int delta;

	/*
	 * Easy stuff:
	 * free and return nil if size is 0
	 * (implementation-defined behaviour);
	 * behave like malloc if ap is nil;
	 * check for arena corruption;
	 * do nothing if units are the same.
	 */
	if(size == 0){
		MLOCK;
		qfreeinternal(ap);
		MUNLOCK;

		return nil;
	}
	if(ap == nil)
		return qmalloc(size);

	p = (Header*)ap - 1;
	if((ounits = p->s.size) == 0 || p->s.next != &checkval)
		panic("realloc: corrupt allocation arena");

	if((nunits = NUNITS(size)) == ounits)
		return ap;

	/*
	 * Slightly harder:
	 * if this allocation abuts the tail, try to just
	 * adjust the tailptr.
	 */
	delta = nunits - ounits;
	MLOCK;
	if(tailptr != nil && p+ounits == tailptr &&
	   (delta < 0 || tailsize >= delta)){
		p->s.size = nunits;
		tailsize -= delta;
		tailptr += delta;
		MUNLOCK;
		return ap;
	}
	MUNLOCK;

	/*
	 * Worth doing if it's a small reduction?
	 * Do it anyway if <= NQUICK?
	if((ounits-nunits) < 2)
		return ap;
	 */

	/*
	 * Too hard (or can't be bothered):
	 * allocate, copy and free.
	 * What does the standard say for failure here?
	 */
	if((v = qmalloc(size)) != nil){
		osize = (ounits-1)*sizeof(Header);
		if(size < osize)
			osize = size;
		memmove(v, ap, osize);
		MLOCK;
		qfreeinternal(ap);
		MUNLOCK;
	}

	return v;
}

void
setmalloctag(void*, ulong)
{
}

void
setrealloctag(void*, ulong)
{
}

void
mallocinit(void)
{
	int i, upages, kpages, maxp;
	ulong n, maxpages;
	Confmem *m, *maxm;
	Pallocmem *pm;

	if(tailptr != nil)
		return;

	upages = conf.upages;
	kpages = conf.npage - upages;
	maxm = nil;
	maxp = 0;
	pm = palloc.mem;
	for(i=0; i<nelem(conf.mem); i++){
		m = &conf.mem[i];
		n = m->npage;
		if(n > kpages)
			n = kpages;
		/* don't try to use non-KADDR-able memory for kernel */
		maxpages = cankaddr(m->base)/BY2PG;
		if(n > maxpages)
			n = maxpages;
		/* first give to kernel */
		if(n != 0){
			m->kbase = (uintptr)KADDR(m->base);
			m->klimit = (uintptr)KADDR(m->base+n*BY2PG);
			if(maxm == nil || maxp < n){
				maxm = m;
				maxp = n;
			}
			kpages -= n;
		}
		/* if anything left over, give to user */
		if(n < m->npage){
			if(pm >= palloc.mem+nelem(palloc.mem)){	/* cannot happen */
				print("mallocinit: losing %lud pages\n", m->npage-n);
				continue;
			}
			pm->base = m->base+(uintmem)n*BY2PG;
			pm->limit = pm->base + (uintmem)(m->npage - n)*BY2PG;
			pm++;
		}
	}

	if(maxm == nil || maxp == 0)
		panic("mallocinit");
	tailbase = (Header*)maxm->kbase;
	tailptr = tailbase;
	tailnunits = NUNITS(maxm->klimit - maxm->kbase) - 1;
	print("base %#p ptr %#p nunits %ud\n", tailbase, tailptr, tailnunits);
}

static int
morecore(uint nunits)
{
	/*
	 * First (simple) cut.
	 * Pump it up when you don't really need it.
	 * Pump it up until you can feel it.
	 */
	if(nunits < NUNITS(128*1024))
		nunits = NUNITS(128*1024);
	if(nunits > tailnunits)
		return 0;
	tailnunits -= nunits;

	return nunits;
}

/* compatability with xalloc */
void
xinit(void)
{
	mallocinit();
}

void*
xalloc(usize n)
{
	return malloc(n);
}

void*
xallocz(usize n, int z)
{
	return mallocz(n, z);
}

void*
xspanalloc(usize nbytes, int align, usize span)
{
	return mallocalign(nbytes, align, 0, span);
}

void
xfree(void *p)
{
	free(p);
}

void
xsummary(void)
{
}

/*
 * faux compatability with pool-based allocation.
 * just return zeros; don't do anything.
 *
 * we can't compact, so qmalloc is not compatable with
 * devdraw.
 */
#include <pool.h>
static Pool pmainmem = {
	.name=	"Main",
};

static Pool pimagmem = {
	.name=	"Image",
};

Pool*	mainmem = &pmainmem;
Pool*	imagmem = &pimagmem;
