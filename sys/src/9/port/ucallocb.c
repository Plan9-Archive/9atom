#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"

enum
{
	Hdrspc		= 64,		/* leave room for high-level headers */
	Bdead		= 0x51494F42,	/* "QIOB" */
};

struct
{
	Lock;
	uint	bytes;
} ucialloc;

static Block*
_ucallocb(int size)
{
	Block *b;
	uchar *p;
	int n;

	n = BLOCKALIGN + ROUNDUP(size+Hdrspc, BLOCKALIGN) + sizeof(Block);
	if((p = ucalloc(n)) == nil)
		return nil;

	b = (Block*)(p + n - sizeof(Block));	/* block at end of allocated space */
	b->base = p;
	b->next = nil;
	b->list = nil;
	b->free = 0;
	b->flag = 0;

	/* align base and bounds of data */
	b->lim = (uchar*)(PTR2UINT(b) & ~(BLOCKALIGN-1));

	/* align start of writable data, leaving space below for added headers */
	b->rp = b->lim - ROUNDUP(size, BLOCKALIGN);
	b->wp = b->rp;

	if(b->rp < b->base || b->lim - b->rp < size)
		panic("_ucallocb");

	return b;
}

Block*
ucallocb(int size)
{
	Block *b;

	/*
	 * Check in a process and wait until successful.
	 * Can still error out of here, though.
	 */
	if(up == nil)
		panic("ucallocb without up: %#p", getcallerpc(&size));
	if((b = _ucallocb(size)) == nil){
		xsummary();
		mallocsummary();
		panic("ucallocb: no memory for %d bytes", size);
	}
	setmalloctag(b, getcallerpc(&size));

	return b;
}

Block*
iucallocb(int size)
{
	Block *b;
	static int m1, m2, mp;

	if(ucialloc.bytes > conf.ialloc){
		if((m1++%10000)==0){
			if(mp++ > 1000){
				active.exiting = 1;
				exit(0);
			}
			iprint("iucallocb: limited %ud/%ud\n",
				ucialloc.bytes, conf.ialloc);
		}
		return nil;
	}

	if((b = _ucallocb(size)) == nil){
		if((m2++%10000)==0){
			if(mp++ > 1000){
				active.exiting = 1;
				exit(0);
			}
			iprint("iucallocb: no memory %ud/%ud\n",
				ucialloc.bytes, conf.ialloc);
		}
		return nil;
	}
	setmalloctag(b, getcallerpc(&size));
	b->flag = BINTR;

	ilock(&ucialloc);
	ucialloc.bytes += b->lim - b->base;
	iunlock(&ucialloc);

	return b;
}

void
ucfreeb(Block *b)
{
	void *dead = (void*)Bdead;
	uchar *p;

	if(b == nil)
		return;

	/*
	 * drivers which perform non cache coherent DMA manage their own buffer
	 * pool of uncached buffers and provide their own free routine.
	 */
	if(b->free) {
		b->free(b);
		return;
	}
	if(b->flag & BINTR) {
		ilock(&ucialloc);
		ucialloc.bytes -= b->lim - b->base;
		iunlock(&ucialloc);
	}

	p = b->base;

	/* poison the block in case someone is still holding onto it */
	b->next = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;

	free(p);
}
