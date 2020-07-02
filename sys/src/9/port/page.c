#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#define	pghash(daddr)	palloc.hash[(daddr>>PGSHIFT)&(PGHSIZE-1)]

Palloc palloc;

/*
 * 1st phase: allocate out of Pallocmem, and don't coalesce
 * 2nd phase: use buddy allocator to allocate physical memory
 * note no need to free Page structures: could re-use them
 */

static void
pageblanks(usize n, int lgsize)
{
	Pallocpg *pg;
	Page *p, *pages;
	int j, color;

	pages = xalloc(n*sizeof(Page));
	if(pages == 0)
		panic("pageblanks");

	pg = &palloc.avail[lgsize];
	color = 0;
	p = pages;
	for(j=0; j<n; j++){
		p->prev = nil;
		p->next = nil;
		p->pa = 0;
		p->va = 0;
		p->daddr = 0;
		p->color = color;
		p->lgsize = lgsize;
		color = (color+1)%NCOLOR;
		p->next = pg->blank;
		pg->blank = p;
		p++;
	}
}

static Page*
blankpage(uint lg)
{
	Pallocpg *pg;
	Page *p;

	pg = &palloc.avail[lg];
	while((p = pg->blank) == nil)
		pageblanks(256, lg);
	pg->blank = p->next;
	p->next = nil;
	p->pa = 0;
	p->va = 0;
	p->lgsize = lg;
	return p;
}

static void
preallocate(uintmem base, usize np, int lgsize)
{
	Pallocpg *pg;
	Page *p;
	int j;

	print("preallocate %lud x %ud KB %#P-%#P\n", np, 1<<lgsize, base, base+((uintmem)np<<lgsize));
	pg = &palloc.avail[lgsize];
	lock(pg);
	for(j=0; j<np; j++){
		p = blankpage(lgsize);
		p->pa = base;
		p->va = 0;
		p->lgsize = lgsize;
		pagechaintail(p);
		base += 1<<lgsize;
	}
	pg->count += np;
	unlock(pg);
}

void
pageinit(void)
{
	int i;
	Pallocmem *pm;
	uintmem m, /*vkb,*/ pkb, top;
	uint np, k;

	np = 0;
	top = 0;
	for(i=0; i<nelem(palloc.mem); i++){
		pm = &palloc.mem[i];
		k = (pm->limit - pm->base) >> PGSHIFT;
		np += k;
		if(pm->limit > top)
			top = pm->limit;
	}
	palloc.user = np;

	physinit(top);
	if(np > (64*MB)/BY2PG)
		np = (64*MB)/BY2PG;

	for(i=0; i<nelem(palloc.mem) && np != 0; i++){
		pm = &palloc.mem[i];
		k = (pm->limit - pm->base) >> PGSHIFT;
		if(k > np)
			k = np;
		preallocate(pm->base, k, PGSHIFT);
		pm->base += (uintmem)k << PGSHIFT;
		np -= k;
	}

	for(i=0; i<nelem(palloc.mem); i++){
		pm = &palloc.mem[i];
		if(pm->limit != pm->base)
			physinitfree(pm->base, pm->limit);
	}
	physdump();

	pkb = (uintmem)palloc.user*BY2PG/1024;
//	vkb = pkb + ((uintmem)conf.nswap*BY2PG)/1024;

	/* Paging numbers */
	swapalloc.highwater = (palloc.user*5)/100;
	swapalloc.headroom = swapalloc.highwater + (swapalloc.highwater/4);

	m = 0;
	for(i=0; i<nelem(conf.mem); i++)
		if(conf.mem[i].npage)
			m += (uintmem)conf.mem[i].npage*BY2PG;
	k = PGROUND(end - (char*)KTZERO);
	print("%PM memory: ", (m+k+1024*1024-1)/(1024*1024));
	print("%PM kernel data, ", (m+k-pkb*1024+1024*1024-1)/(1024*1024));
	print("%PM user, ", pkb/1024);
//	print("%PM swap\n", vkb/1024);
	print("0M swap\n");
}

static void
pageunchain(Page *p)
{
	Pallocpg *pg;

assert(1<<p->lgsize & segpgsizes);
	pg = &palloc.avail[p->lgsize];
	if(canlock(pg))
		panic("pageunchain (palloc %#p %d)", pg, p->lgsize);
	if(p->prev)
		p->prev->next = p->next;
	else
		pg->head = p->next;
	if(p->next)
		p->next->prev = p->prev;
	else
		pg->tail = p->prev;
	p->prev = p->next = nil;
	pg->freecount--;
}

void
pagechaintail(Page *p)
{
	Pallocpg *pg;

assert(1<<p->lgsize & segpgsizes);
	pg = &palloc.avail[p->lgsize];
	if(canlock(pg))
		panic("pagechaintail");
	if(pg->tail) {
		p->prev = pg->tail;
		pg->tail->next = p;
	}
	else {
		pg->head = p;
		p->prev = nil;
	}
	pg->tail = p;
	p->next = nil;
	pg->freecount++;
}

void
pagechainhead(Page *p)
{
	Pallocpg *pg;

assert(1<<p->lgsize & segpgsizes);
	pg = &palloc.avail[p->lgsize];
	if(canlock(pg))
		panic("pagechainhead");
	if(pg->head) {
		p->next = pg->head;
		pg->head->prev = p;
	}
	else {
		pg->tail = p;
		p->next = nil;
	}
	pg->head = p;
	p->prev = nil;
	pg->freecount++;
}

Page*
newpage(int clear, Segment **s, ulong va, uint lgsize)
{
	Page *p;
	KMap *k;
	uchar ct;
	Pallocpg *pg;
	int i, hw, dontalloc, color;
	uintmem pa;

if(lgsize != PGSHIFT)print("newpage %ud from %#p\n", lgsize, getcallerpc(&clear));
	pg = &palloc.avail[lgsize];
	lock(pg);
	color = getpgcolor(va);
	hw = swapalloc.highwater;
	if(lgsize != PGSHIFT || up == nil || up->kp)
		hw = 0;
	for(;;) {

		if(pg->freecount > hw)
			break;

		/* try allocating a suitable page */
		pa = physalloc(1<<lgsize);
		if(pa != 0){
			p = pg->blank;
			if(p != nil){
				pg->blank = p->next;
				p->next = nil;
			}else
				p = blankpage(lgsize);
			p->pa = pa;
			p->ref = 1;
			p->va = va;
			pg->count++;
			unlock(pg);
			goto Clear;
		}

		unlock(pg);
		dontalloc = 0;
		if(s && *s) {
			qunlock(&((*s)->lk));
			*s = 0;
			dontalloc = 1;
		}
		qlock(&pg->pwait);	/* Hold memory requesters here */

		while(waserror())	/* Ignore interrupts */
			;

		kickpager();
		tsleep(&pg->r, ispages, pg, 1000);

		poperror();

		qunlock(&pg->pwait);

		/*
		 * If called from fault and we lost the segment from
		 * underneath don't waste time allocating and freeing
		 * a page. Fault will call newpage again when it has
		 * reacquired the segment locks
		 */
		if(dontalloc)
			return 0;

		lock(pg);
	}

	/* First try for our colour */
	for(p = pg->head; p != nil; p = p->next)
		if(p->color == color)
			break;

	ct = PG_NOFLUSH;
	if(p == nil) {
		p = pg->head;
		p->color = color;
		ct = PG_NEWCOL;
	}

	pageunchain(p);

	lock(p);
	if(p->ref != 0)
		panic("newpage: p->ref %d != 0", p->ref);

	uncachepage(p);
	p->ref++;
	p->va = va;
	p->modref = 0;
	for(i = 0; i < MAXMACH; i++)
		p->cachectl[i] = ct;
	unlock(p);
	unlock(pg);

Clear:
	if(clear) {
		k = kmap(p);
		memset((void*)VA(k), 0, 1<<p->lgsize);
		kunmap(k);
	}

	return p;
}

int
ispages(void *a)
{
	return ((Pallocpg*)a)->freecount >= swapalloc.highwater;
}

void
putpage(Page *p)
{
	Pallocpg *pg;

	if(onswap(p)) {
		putswap(p);
		return;
	}

	pg = &palloc.avail[p->lgsize];
	lock(pg);
	lock(p);

	if(p->ref == 0)
		panic("putpage");

	if(--p->ref > 0) {
		unlock(p);
		unlock(pg);
		return;
	}

	if(p->image && p->image != &swapimage)
		pagechaintail(p);
	else 
		pagechainhead(p);

	if(pg->r.p != nil)
		wakeup(&pg->r);

	unlock(p);
	unlock(pg);
}

/*
 * always BY2PG
 */
Page*
auxpage(void)
{
	Page *p;
	Pallocpg *pg;

	pg = &palloc.avail[PGSHIFT];

	lock(pg);
	if(pg->freecount < swapalloc.highwater) {
		unlock(pg);
		return 0;
	}
	p = pg->head;
	pageunchain(p);

	lock(p);
	if(p->ref != 0)
		panic("auxpage");
	p->ref++;
	uncachepage(p);
	unlock(p);
	unlock(pg);

	return p;
}

static int dupretries = 15000;

int
duppage(Page *p)				/* Always call with p locked */
{
	Pallocpg *pg;
	Page *np;
	int color;
	int retries;

	retries = 0;
retry:
	/* don't dup shared page */
	if(p->ref != 1){
		print("%lud %s: %s: duppage: p->ref %d != 1\n", up->pid, up->text, up->user, p->ref);
		return 0;
	}

	if(retries++ > dupretries){
		print("duppage %d, up %#p\n", retries, up);
		dupretries += 100;
		if(dupretries > 100000)
			panic("duppage");
		uncachepage(p);
		return 1;
	}

	/* don't dup pages with no image */
	if(p->ref == 0 || p->image == nil || p->image->notext)
		return 0;

	/* don't dup large pages */
	if(p->lgsize != PGSHIFT){
		uncachepage(p);
		return 1;
	}

	pg = &palloc.avail[p->lgsize];

	/*
	 *  normal lock ordering is to call
	 *  lock(pg) before lock(p).
	 *  To avoid deadlock, we have to drop
	 *  our locks and try again.
	 */
	if(!canlock(pg)){
		unlock(p);
		if(up)
			sched();
		lock(p);
		goto retry;
	}

	/* No freelist cache when memory is very low */
	if(pg->freecount < swapalloc.highwater) {
		unlock(pg);
		uncachepage(p);
		return 1;
	}

	color = getpgcolor(p->va);
	for(np = pg->head; np != nil; np = np->next)
		if(np->color == color)
			break;

	/* No page of the correct color */
	if(np == nil) {
		unlock(pg);
		uncachepage(p);
		return 1;
	}

	pageunchain(np);
	pagechaintail(np);
/*
* XXX - here's a bug? - np is on the freelist but it's not really free.
* when we unlock palloc someone else can come in, decide to
* use np, and then try to lock it.  they succeed after we've 
* run copypage and cachepage and unlock(np).  then what?
* they call pageunchain before locking(np), so it's removed
* from the freelist, but still in the cache because of
* cachepage below.  if someone else looks in the cache
* before they remove it, the page will have a nonzero ref
* once they finally lock(np).
*/
	lock(np);
	unlock(pg);

	/* Cache the new version */
	uncachepage(np);
	np->va = p->va;
	np->daddr = p->daddr;
	copypage(p, np);
	cachepage(np, p->image);
	unlock(np);
	uncachepage(p);

	return 0;
}

void
copypage(Page *f, Page *t)
{
	KMap *ks, *kd;

assert(1<<f->lgsize & segpgsizes);
	if(f->lgsize != t->lgsize)
		panic("copypage");
	ks = kmap(f);
	kd = kmap(t);
	memmove((void*)VA(kd), (void*)VA(ks), 1<<t->lgsize);
	kunmap(ks);
	kunmap(kd);
}

void
uncachepage(Page *p)			/* Always called with a locked page */
{
	Page **l, *f;

	if(p->image == 0)
		return;

	lock(&palloc.hashlock);
	l = &pghash(p->daddr);
	for(f = *l; f; f = f->hash) {
		if(f == p) {
			*l = p->hash;
			break;
		}
		l = &f->hash;
	}
	unlock(&palloc.hashlock);
	putimage(p->image);
	p->image = 0;
	p->daddr = 0;
}

void
cachepage(Page *p, Image *i)
{
	Page **l;

	/* If this ever happens it should be fixed by calling
	 * uncachepage instead of panic. I think there is a race
	 * with pio in which this can happen. Calling uncachepage is
	 * correct - I just wanted to see if we got here.
	 */
	if(p->image)
		panic("cachepage");

	incref(i);
	lock(&palloc.hashlock);
	p->image = i;
	l = &pghash(p->daddr);
	p->hash = *l;
	*l = p;
	unlock(&palloc.hashlock);
}

void
cachedel(Image *i, ulong daddr)
{
	Page *f, **l;

	lock(&palloc.hashlock);
	l = &pghash(daddr);
	for(f = *l; f; f = f->hash) {
		if(f->image == i && f->daddr == daddr) {
			lock(f);
			if(f->image == i && f->daddr == daddr){
				*l = f->hash;
				putimage(f->image);
				f->image = 0;
				f->daddr = 0;
			}
			unlock(f);
			break;
		}
		l = &f->hash;
	}
	unlock(&palloc.hashlock);
}

Page *
lookpage(Image *i, ulong daddr)
{
	Page *f;
	Pallocpg *pg;

	lock(&palloc.hashlock);
	for(f = pghash(daddr); f; f = f->hash) {
		if(f->image == i && f->daddr == daddr) {
			unlock(&palloc.hashlock);

			pg = &palloc.avail[f->lgsize];
			lock(pg);
			lock(f);
			if(f->image != i || f->daddr != daddr) {
				unlock(f);
				unlock(pg);
				return 0;
			}
			if(++f->ref == 1)
				pageunchain(f);
			unlock(f);
			unlock(pg);

			return f;
		}
	}
	unlock(&palloc.hashlock);

	return 0;
}

Pte*
ptecpy(Pte *old)
{
	Pte *new;
	Page **src, **dst;

	new = ptealloc();
	dst = &new->pages[old->first-old->pages];
	new->first = dst;
	for(src = old->first; src <= old->last; src++, dst++)
		if(*src) {
			if(onswap(*src))
				dupswap(*src);
			else {
				lock(*src);
				(*src)->ref++;
				unlock(*src);
			}
			new->last = dst;
			*dst = *src;
		}

	return new;
}

Pte*
ptealloc(void)
{
	Pte *new;

	new = smalloc(sizeof(Pte));
	new->first = &new->pages[PTEPERTAB];
	new->last = new->pages;
	return new;
}

void
freepte(Segment *s, Pte *p)
{
	int ref;
	void (*fn)(Page*);
	Page *pt, **pg, **ptop;

	switch(s->type&SG_TYPE) {
	case SG_PHYSICAL:
		fn = s->pseg->pgfree;
		ptop = &p->pages[PTEPERTAB];
		if(fn) {
			for(pg = p->pages; pg < ptop; pg++) {
				if(*pg == 0)
					continue;
				(*fn)(*pg);
				*pg = 0;
			}
			break;
		}
		for(pg = p->pages; pg < ptop; pg++) {
			pt = *pg;
			if(pt == 0)
				continue;
			lock(pt);
			ref = --pt->ref;
			unlock(pt);
			if(ref == 0)
				free(pt);
		}
		break;
	default:
		for(pg = p->first; pg <= p->last; pg++)
			if(*pg) {
				putpage(*pg);
				*pg = 0;
			}
	}
	free(p);
}

uint
pagenumber(Page *p)
{
	USED(p);
	return 0;	/* was p-palloc.pages */
}

uint
pagesmade(void)
{
	int i;
	ulong np;

	np = 0;
	for(i=0; i<nelem(palloc.avail); i++)
		np += palloc.avail[i].count<<(i-PGSHIFT);
	return np;
}

uint
pagesfree(void)
{
	int i;
	uint np;

	np = 0;
	for(i=0; i<nelem(palloc.avail); i++)
		np += palloc.avail[i].freecount<<(i-PGSHIFT);
	return np;
}

void
pagewake(void)
{
	Pallocpg *pg;
	int i;

	for(i=0; i<nelem(palloc.avail); i++){
		pg = &palloc.avail[i];
		if(pg->r.p != nil)
			wakeup(&pg->r);
	}
}

void
checkpagerefs(void)
{
#ifdef not
	int s;
	ulong i, np, nwrong;
	ulong *ref;
	
	np = palloc.user;
	ref = malloc(np*sizeof ref[0]);
	if(ref == nil){
		print("checkpagerefs: out of memory\n");
		return;
	}
	
	/*
	 * This may not be exact if there are other processes
	 * holding refs to pages on their stacks.  The hope is
	 * that if you run it on a quiescent system it will still
	 * be useful.
	 */
	s = splhi();
	lock(&palloc);
	countpagerefs(ref, 0);
	portcountpagerefs(ref, 0);
	nwrong = 0;
	for(i=0; i<np; i++){
		if(palloc.pages[i].ref != ref[i]){
			iprint("page %#P ref %d actual %lud\n", 
				palloc.pages[i].pa, palloc.pages[i].ref, ref[i]);
			ref[i] = 1;
			nwrong++;
		}else
			ref[i] = 0;
	}
	countpagerefs(ref, 1);
	portcountpagerefs(ref, 1);
	iprint("%lud mistakes found\n", nwrong);
	unlock(&palloc);
	splx(s);
#endif
}

void
portcountpagerefs(ulong *ref, int print)
{
	USED(ref, print);
#ifdef not
	ulong i, j, k, ns, n;
	Page **pg, *entry;
	Proc *p;
	Pte *pte;
	Segment *s;

	/*
	 * Pages in segments.  s->mark avoids double-counting.
	 */
	n = 0;
	ns = 0;
	for(i=0; i<conf.nproc; i++){
		p = proctab(i);
		for(j=0; j<NSEG; j++){
			s = p->seg[j];
			if(s)
				s->mark = 0;
		}
	}
	for(i=0; i<conf.nproc; i++){
		p = proctab(i);
		for(j=0; j<NSEG; j++){
			s = p->seg[j];
			if(s == nil || s->mark++)
				continue;
			ns++;
			for(k=0; k<s->mapsize; k++){
				pte = s->map[k];
				if(pte == nil)
					continue;
				for(pg = pte->first; pg <= pte->last; pg++){
					entry = *pg;
					if(pagedout(entry))
						continue;
					if(print){
						if(ref[pagenumber(entry)])
							iprint("page %#P in segment %#p\n", entry->pa, s);
						continue;
					}
					if(ref[pagenumber(entry)]++ == 0)
						n++;
				}
			}
		}
	}
	if(!print){
		iprint("%lud pages in %lud segments\n", n, ns);
		for(i=0; i<conf.nproc; i++){
			p = proctab(i);
			for(j=0; j<NSEG; j++){
				s = p->seg[j];
				if(s == nil)
					continue;
				if(s->ref != s->mark){
					iprint("segment %#p (used by proc %lud pid %lud) has bad ref count %lud actual %lud\n",
						s, i, p->pid, s->ref, s->mark);
				}
			}
		}
	}
#endif
}

