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
	uvlong	bytes;
} ialloc;

/* this doesn't work because physalloc hands us 2mb chunks.  stupid interface. */
static void*
mal(uint n)
{
	int color;
	uintmem pa;

	color = -1;
	pa = physalloc(n, &color, &ialloc);
	if(pa)
		return KADDR(pa);
	return nil;
}

static void
fre(Block *b)
{
	uint n;
	uintmem pa;
	void *dead;


	n = (uchar*)b - b->base + sizeof(Block);		/* correct? */
	pa = PADDR(b->base);

	/* poison the block in case someone is still holding onto it */
	dead = (void*)Bdead;
	b->next = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;

	physfree(pa, 2*1024*1024 /*n*/);
}

static Block*
·allocb(int size)
{
	int n;
	Block *b;
	uchar *p;

	n = Align + ROUNDUP(size+Hdrspc, Align) + sizeof(Block);
	if((p = mal(n)) == nil)
		return nil;

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
		panic("·allocb");
	return b;
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
		panic("allocb without up: %#p", getcallerpc(&size));
	if((b = ·allocb(size)) == nil){
		iprint("allocb: no memory for %d from %#p\n", size, getcallerpc(&size));
		extern void physstats(void);
		physstats();
		mallocsummary();
		panic("allocb: no memory for %d from %#p", size, getcallerpc(&size));
	}

	return b;
}

Block*
iallocb(int size)
{
	Block *b;

	if(ialloc.bytes > sys->ialloc){
		panic("iallocb: limited %llud/%llud from %#p", ialloc.bytes, sys->ialloc, getcallerpc(&size));
		return nil;
	}
	if((b = ·allocb(size)) == nil){
		iprint("iallocb: no memory for %d from %#p", size, getcallerpc(&size));
		extern void physstats(void);
		physstats();
		mallocsummary();
		return nil;
	}

	b->flag = BINTR;

	ilock(&ialloc);
	ialloc.bytes += b->lim - b->base;
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
		ialloc.bytes -= b->lim - b->base;
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
	print("ialloc %llud/%llud\n", ialloc.bytes, sys->ialloc);
}
