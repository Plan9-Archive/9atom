#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

enum
{
	Hdrspc		= 64,		/* leave room for high-level headers */
	Bdead		= 0x51494F42,	/* "QIOB" */
	Align		= BLOCKALIGN,
};

struct
{
	Lock;
	uint	bytes;
} ialloc;

typedef struct Quick Quick;
struct Quick {
	Lock;
	int	sz;
	Block	*free;
};

static	Quick	qtab[] = {
	{.sz = 128,},
	{.sz = 1536,},
	{.sz = 16384,},
	{.sz = 65536,},
};

static Block*
binit(uchar *p, int size, uchar** next)
{
	int n;
	Block *b;

	n = Align + ROUNDUP(size+Hdrspc, Align) + sizeof(Block);
	b = (Block*)(p + n - sizeof(Block));	/* block at end of allocated space */
	b->base = p;
	b->next = nil;
	b->list = nil;
	b->free = 0;
	b->flag = 0;

	/* align base and bounds of data */
	b->lim = (uchar*)(PTR2UINT(b) & ~(Align-1));

	/* align start of writable data, leaving space below for added headers */
	b->rp = b->lim - ROUNDUP(size, Align);
	b->wp = b->rp;

	if(b->rp < b->base || b->lim - b->rp < size)
		panic("binit");
	if(next != nil)
		*next = p + n;
	return b;
}

static void*
·allocb(uint size)
{
	uchar *va, *ve;
	int i, bsz, asz;
	uintmem pa;
	Block *b;
	Quick *q;

	for(i = 0; i < nelem(qtab); i++)
		if(size <= qtab[i].sz)
			goto found;
	panic("qallocb: unsupported block size %d", size);
found:
	q = qtab + i;
	ilock(q);
	if(q->free == nil){
		asz = 2*MiB;
		pa = physalloc(asz);
		if(pa == 0){
			iunlock(q);
			return nil;
		}
		va = KADDR(pa);
		bsz = Align + ROUNDUP(q->sz+Hdrspc, Align) + sizeof(Block);
		ve = va + (asz/bsz)*bsz;
		while(va<ve){
			b = binit(va, q->sz, &va);
			b->next = q->free;
			q->free = b;
		}
	}
	b = q->free;
	q->free = b->next;
	iunlock(q);

	b->next = nil;
	b->lim = b->rp + size;		/* lie like a dog (qio counts alloc'd bytes .. pfft.) */
	return b;
}

static void
fre(Block *b)
{
	uint size, i;
	Quick *q;

	size = (uchar*)b - b->base - Hdrspc - Align;
	for(i = 0; i < nelem(qtab); i++)
		if(size <= qtab[i].sz)
			goto found;
	panic("bfree: bad size %d %p %p", size, b, b->base);
found:
	q = qtab + i;
	b = binit(b->base, q->sz, nil);
	ilock(q);
	b->next = q->free;
	q->free = b;
	iunlock(q);
}

Block*
allocb(int size)
{
	Block *b;

	/*
	 * Check in a process and wait until successful.
	 * Can still error out of here, though.
	 */
	if(up == nil)
		panic("allocb no up: %#p", getcallerpc(&size));
	if((b = ·allocb(size)) == nil){
		iprint("allocb: no memory for %d from %#p\n", size, getcallerpc(&size));
//		extern void physstats(void);
//		physstats();
		mallocsummary();
		panic("allocb: no memory for %d from %#p", size, getcallerpc(&size));
	}

	return b;
}

Block*
iallocb(int size)
{
	Block *b;

	if(ialloc.bytes > conf.ialloc){
		panic("iallocb: limited %ud/%ud from %#p", ialloc.bytes, conf.ialloc, getcallerpc(&size));
		return nil;
	}
	if((b = ·allocb(size)) == nil){
		iprint("iallocb: no memory for %d from %#p\n", size, getcallerpc(&size));
//		extern void physstats(void);
//		physstats();
		mallocsummary();
		return nil;
	}

	b->flag = BINTR;

	ilock(&ialloc);
	ialloc.bytes += BALLOC(b);
	iunlock(&ialloc);

	return b;
}

void
freeb(Block *b)
{
	if(b == nil)
		return;
	if(b->free) {
		assert((b->flag & BINTR) == 0);
		b->flag = 0;
		b->free(b);
		return;
	}
	if(b->flag & BINTR) {
		ilock(&ialloc);
		ialloc.bytes -= BALLOC(b);
		iunlock(&ialloc);
	}
	fre(b);
}

void
checkb(Block *b, char *msg)
{
	void *dead = (void*)Bdead;

	if(b == dead)
		panic("checkb b %s %#p", msg, b);
	if(b->base == dead || b->lim == dead || b->next == dead
	  || b->rp == dead || b->wp == dead){
		print("checkb: base %#p lim %#p next %#p\n",
			b->base, b->lim, b->next);
		print("checkb: rp %#p wp %#p\n", b->rp, b->wp);
		panic("checkb dead: %s", msg);
	}

	if(b->base > b->lim)
		panic("checkb 0 %s %#p %#p", msg, b->base, b->lim);
	if(b->rp < b->base)
		panic("checkb 1 %s %#p %#p", msg, b->base, b->rp);
	if(b->wp < b->base)
		panic("checkb 2 %s %#p %#p", msg, b->base, b->wp);
	if(b->rp > b->lim)
		panic("checkb 3 %s %#p %#p", msg, b->rp, b->lim);
	if(b->wp > b->lim)
		panic("checkb 4 %s %#p %#p", msg, b->wp, b->lim);
}

void
iallocsummary(void)
{
	print("ialloc %ud/%ud\n", ialloc.bytes, conf.ialloc);
}
