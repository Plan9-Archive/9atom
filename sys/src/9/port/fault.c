#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

int
fault(ulong addr, int read)
{
	int tries;
	Segment *s;
	char *sps;

	if(up == nil)
		panic("fault: nil up");
	if(up->nlocks.ref){
		print("fault: %#p %s: %s: nlocks %ld\n", addr, up->text, up->user, up->nlocks.ref);
		dumpstack();
	}

	sps = up->psstate;
	up->psstate = "Fault";
	spllo();

	m->pfault++;
	for(tries = 200; tries > 0; tries--) {	/* TODO: reset to 20 */
		s = seg(up, addr, 1);		/* leaves s->lk qlocked if seg != nil */
		if(s == 0) {
			up->psstate = sps;
			return -1;
		}

		if(!read && (s->type&SG_RONLY)) {
			qunlock(&s->lk);
			up->psstate = sps;
			return -1;
		}

		if(fixfault(s, addr, read, 1) == 0)	/* qunlocks s->lk */
			break;
	}
	/*
	 * if we loop more than a few times, we're probably stuck on
	 * an unfixable address, almost certainly due to a bug
	 * elsewhere (e.g., bad page tables) so there's no point
	 * in spinning forever.
	 */
	if (tries <= 0)
		panic("fault: fault stuck on va %#8.8p read %d", addr, read);

	up->psstate = sps;
	return 0;
}

static void
faulterror(char *s, Chan *c, int freemem)
{
	char buf[ERRMAX];

	if(c && c->path){
		snprint(buf, sizeof buf, "%s accessing %s: %s", s, c->path->s, up->errstr);
		s = buf;
	}
	if(up->nerrlab) {
		postnote(up, 1, s, NDebug);
		error(s);
	}
	pexit(s, freemem);
}

void	(*checkaddr)(ulong, Segment *, Page *);
ulong	addr2check;

int
fixfault(Segment *s, ulong addr, int read, int doputmmu)
{
	int type;
	int ref;
	uint pgsize;
	Pte **p, *etp;
	ulong soff;
	uintmem mmuphys;
	Page **pg, *lkp, *new;
	Page *(*fn)(Segment*, ulong);

	pgsize = 1<<s->lgpgsize;
	addr &= ~(pgsize-1);
	soff = addr-s->base;
 if(1 && pgsize != BY2PG)print("fault addr %#p pgsize %d soff %#p offset %lud/%d\n", addr, pgsize, soff, soff/s->ptemapmem, s->mapsize);
	p = &s->map[soff/s->ptemapmem];
	if(*p == 0)
		*p = ptealloc();

	etp = *p;
	pg = &etp->pages[(soff&(s->ptemapmem-1))>>s->lgpgsize];
	type = s->type&SG_TYPE;

	if(pg < etp->first)
		etp->first = pg;
	if(pg > etp->last)
		etp->last = pg;

	mmuphys = 0;
	switch(type) {
	default:
		panic("fault: unknown segtype %ux", type);
		break;

	case SG_TEXT: 			/* Demand load */
		if(pagedout(*pg))
			pio(s, addr, soff, pg);

		mmuphys = PPN((*pg)->pa) | PTERONLY|PTEVALID;
		(*pg)->modref = PG_REF;
		break;

	case SG_BSS:
	case SG_SHARED:			/* Zero fill on demand */
	case SG_STACK:
		if(*pg == 0) {
			new = newpage(1, &s, addr, s->lgpgsize);
			if(s == 0)
				return -1;

			*pg = new;
		}
		goto common;

	case SG_DATA:
	common:			/* Demand load/pagein/copy on write */
		if(pagedout(*pg))
			pio(s, addr, soff, pg);

		/*
		 *  It's only possible to copy on write if
		 *  we're the only user of the segment.
		 */
		if(read && conf.copymode == 0 && s->ref == 1) {
			mmuphys = PPN((*pg)->pa)|PTERONLY|PTEVALID;
			(*pg)->modref |= PG_REF;
			break;
		}

		lkp = *pg;
		lock(lkp);
		if(lkp->ref <= 0)
			panic("fault: page->ref %d <= 0", lkp->ref);

		if(lkp->image == &swapimage)
			ref = lkp->ref + swapcount(lkp->daddr);
		else
			ref = lkp->ref;
		if(ref == 1 && lkp->image){
			duppage(lkp);
			ref = lkp->ref;
		}
		unlock(lkp);
		if(ref > 1){
			new = newpage(0, &s, addr, s->lgpgsize);
			if(s == 0)
				return -1;
			*pg = new;
			copypage(lkp, *pg);
			putpage(lkp);
		}
		mmuphys = PPN((*pg)->pa) | PTEWRITE | PTEVALID;
		(*pg)->modref = PG_MOD|PG_REF;
		break;

	case SG_PHYSICAL:
		if(*pg == 0) {
			fn = s->pseg->pgalloc;
			if(fn)
				*pg = (*fn)(s, addr);
			else {
				new = smalloc(sizeof(Page));
				new->va = addr;
				new->pa = s->pseg->pa+(addr-s->base);
				new->ref = 1;
				new->lgsize = s->pseg->lgpgsize;
				*pg = new;
			}
		}

		if (checkaddr && addr == addr2check)
			(*checkaddr)(addr, s, *pg);
		mmuphys = PPN((*pg)->pa) |PTEVALID;
		if((s->pseg->attr & SG_RONLY) == 0)
			mmuphys |= PTEWRITE;
		if((s->pseg->attr & SG_CACHED) == 0)
			mmuphys |= PTEUNCACHED;
		(*pg)->modref = PG_MOD|PG_REF;
		break;
	}
	qunlock(&s->lk);

	if(doputmmu)
		putmmu(addr, mmuphys, *pg);

	return 0;
}

void
pio(Segment *s, ulong addr, ulong soff, Page **p)
{
	Page *new;
	KMap *k;
	Chan *c;
	int n, ask;
	uint pgsize;
	char *kaddr;
	ulong daddr;
	Page *loadrec;

	pgsize = 1<<s->lgpgsize;
retry:
	loadrec = *p;
	if(loadrec == 0) {	/* from a text/data image */
		daddr = s->fstart+soff;
		new = lookpage(s->image, daddr);
		if(new != nil) {
			*p = new;
			return;
		}

		c = s->image->c;
		ask = s->flen-soff;
		if(ask > pgsize)
			ask = pgsize;
	}
	else {			/* from a swap image */
		daddr = swapaddr(loadrec);
		new = lookpage(&swapimage, daddr);
		if(new != nil) {
			putswap(loadrec);
			*p = new;
			return;
		}

		c = swapimage.c;
		ask = pgsize;
	}
	qunlock(&s->lk);

	new = newpage(0, 0, addr, s->lgpgsize);
	k = kmap(new);
	kaddr = (char*)VA(k);

	while(waserror()) {
		if(strcmp(up->errstr, Eintr) == 0)
			continue;
		kunmap(k);
		putpage(new);
		faulterror(Eioload, c, 0);
	}

	n = devtab[c->type]->read(c, kaddr, ask, daddr);
	if(n != ask)
		faulterror(Eioload, c, 0);
	if(ask < pgsize)
		memset(kaddr+ask, 0, pgsize-ask);

	poperror();
	kunmap(k);
	qlock(&s->lk);
	if(loadrec == 0) {	/* This is demand load */
		/*
		 *  race, another proc may have gotten here first while
		 *  s->lk was unlocked
		 */
		if(*p == 0) { 
			new->daddr = daddr;
			cachepage(new, s->image);
			*p = new;
		}
		else
			putpage(new);
	}
	else {			/* This is paged out */
		/*
		 *  race, another proc may have gotten here first
		 *  (and the pager may have run on that page) while
		 *  s->lk was unlocked
		 */
		if(*p != loadrec){
			if(!pagedout(*p)){
				/* another process did it for me */
				putpage(new);
				goto done;
			} else {
				/* another process and the pager got in */
				putpage(new);
				goto retry;
			}
		}

		new->daddr = daddr;
		cachepage(new, &swapimage);
		*p = new;
		putswap(loadrec);
	}

done:
	if(s->flushme)
		memset((*p)->cachectl, PG_TXTFLUSH, sizeof((*p)->cachectl));
}

/*
 * Called only in a system call
 */
int
okaddr(ulong addr, ulong len, int write)
{
	Segment *s;

	if((long)len >= 0) {
		for(;;) {
			s = seg(up, addr, 0);
			if(s == 0 || (write && (s->type&SG_RONLY)))
				break;

			if(addr+len > s->top) {
				len -= s->top - addr;
				addr = s->top;
				continue;
			}
			return 1;
		}
	}
	pprint("suicide: invalid address %#lux/%lud in sys call pc=%#lux\n", addr, len, userpc());
	return 0;
}

void
validaddr(ulong addr, ulong len, int write)
{
	if(!okaddr(addr, len, write)){
		postnote(up, 1, "sys: bad address in syscall", NDebug);
		error(Ebadarg);
	}
}

/*
 * &s[0] is known to be a valid address.
 */
void*
vmemchr(void *s, int c, int n)
{
	int m;
	ulong a;
	void *t;

	a = (ulong)s;
	while(PGROUND(a) != PGROUND(a+n-1)){
		/* spans pages; handle this page */
		m = BY2PG - (a & (BY2PG-1));
		t = memchr((void*)a, c, m);
		if(t)
			return t;
		a += m;
		n -= m;
		if(a < KZERO)
			validaddr(a, 1, 0);
	}

	/* fits in one page */
	return memchr((void*)a, c, n);
}

Segment*
seg(Proc *p, uintptr addr, int dolock)
{
	Segment **s, **et, *n;

	et = &p->seg[NSEG];
	for(s = p->seg; s < et; s++) {
		n = *s;
		if(n == 0)
			continue;
		if(addr >= n->base && addr < n->top) {
			if(dolock == 0)
				return n;

			qlock(&n->lk);
			if(addr >= n->base && addr < n->top)
				return n;
			qunlock(&n->lk);
		}
	}

	return 0;
}

extern void checkmmu(uintptr, uintmem);
void
checkpages(void)
{
	int checked;
	uint by2pg;
	uintptr addr, off;
	Pte *p;
	Page *pg;
	Segment **sp, **ep, *s;
	
	if(up == nil)
		return;

	checked = 0;
	for(sp=up->seg, ep=&up->seg[NSEG]; sp<ep; sp++){
		s = *sp;
		if(s == nil)
			continue;
		qlock(&s->lk);
		by2pg = 1<<s->lgpgsize;
		for(addr=s->base; addr<s->top; addr+=by2pg){
			off = addr - s->base;
			p = s->map[off/s->ptemapmem];
			if(p == 0)
				continue;
			pg = p->pages[(off&(s->ptemapmem-1))>>s->lgpgsize];
			if(pg == 0 || pagedout(pg))
				continue;
			checkmmu(addr, pg->pa);
			checked++;
		}
		qunlock(&s->lk);
	}
	print("%lud %s: %s: checked %d page table entries\n", up->pid, up->text, up->user, checked);
}
