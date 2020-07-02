#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * - locks
 * - auxilary structure allocation and sizing
 * - largest size
 * - could instead coalesce free items on demand (cf. Wulf)
 * - or lazy buddy (cf. Barkley)
 */

#define dprint(...)	do {if(0)iprint(__VA_ARGS__);}while(0)

enum{
	MinK=		PGSHIFT,
	MinBsize=		(uintmem)1<<MinK,
	MaxK=		30,		/* last usable k (largest block is 2^k) */

	Busy=		0x80,	/* bit set in byte map if block busy */
};

typedef struct Blk Blk;
struct Blk{
	Blk*	forw;	/* free list */
	Blk*	back;
};

typedef struct Bfree Bfree;
struct Bfree{
	Blk;	/* header */
	Lock;
};

typedef struct Bpool Bpool;
struct Bpool{
	Lock	lk;	/* TO DO: localise lock (need CAS update of phys.kofb) (also see Johnson & Davis 1992) */
	Bfree	blist[MaxK<=32? 32: 64];	/* increasing powers of two */
	uchar*	kofb;		/* k(block_index) with top bit set if busy */
	uint	maxb;	/* limit to block index, in MinBsize blocks (pool size) */
	Blk*	blocks;	/* free list pointers */
//	uchar*	aspace;
};

static	Bpool	phys;

#define	aspace	0
static	uchar	log2v[256];

#define	BI(a)	(((a)-aspace)>>MinK)
#define	IB(x)	(((uintmem)(x)<<MinK)+aspace)

static void
loginit(void)
{
	int i;

	for(i=2; i<nelem(log2v); i++)
		log2v[i] = log2v[i/2] + 1;
}

static int
log2of(uintmem m)
{
	uint n;
	int r;

	r = (m & (m-1)) != 0;	/* not a power of two => round up */
	n = (uint)m;
	if(sizeof(uintmem)>sizeof(uint) && n != m){
		n = (u64int)m>>32;
		r += 32;
	}
	if((n>>8) == 0)
		return log2v[n] + r;
	if((n>>16) == 0)
		return 8 + log2v[n>>8] + r;
	if((n>>24) == 0)
		return 16 + log2v[n>>16] + r;
	return 24 + log2v[n>>24] + r;
}

void
physinit(uintmem top)
{
	int k;
	uvlong vtop;
	Blk *b;

	loginit();
	vtop = top;
	if(vtop > 48ull*1073741824u)
		top = 48ull*1073741824u;
	phys.maxb = BI(top);
	phys.blocks = xalloc((phys.maxb+1)*sizeof(*phys.blocks));
	if(phys.blocks == nil)
		panic("physinit: can't allocate %ud blocks", phys.maxb+1);
	for(k = 0; k < nelem(phys.blist); k++){
		b = &phys.blist[k];
		b->forw = b->back = b;
	}
	phys.kofb = xalloc((phys.maxb+1)*sizeof(*phys.kofb));
	if(phys.kofb == nil)
		panic("physinit: can't allocate %ud kofb", phys.maxb+1);
	memset(phys.kofb, 0, sizeof(phys.kofb));
	dprint("phys space top=%#P maxb=%#ux (%d)\n", top, phys.maxb, phys.maxb);
}

uintmem
physalloc(usize size)
{
	int j, k;
	Blk *b, *b2;
	uintmem a, a2;
	uint bi;

	k = log2of(size);
	if(k < MinK)
		k = MinK;
	dprint("size=%lud k=%d\n", size, k);
	lock(&phys.lk);
	for(j = k;;){
		b = phys.blist[j].forw;
		if(b != &phys.blist[j])
			break;
		if(++j > MaxK){
			unlock(&phys.lk);
			return 0;	/* out of space */
		}
	}
	b->forw->back = b->back;
	b->back->forw = b->forw;
	/* set busy state */
	bi = b-phys.blocks;
	a = IB(bi);
	phys.kofb[bi] = k | Busy;
	while(j != k){
		/* split */
		j--;
		a2 = a+((uintmem)1<<j);
		bi = BI(a2);
		dprint("split %#P %#P k=%d %#P phys.kofb=%#ux\n", a, a2, j, (uintmem)1<<j, phys.kofb[bi]);
		if(phys.kofb[bi] & Busy)
			panic("bal: busy block %#P k=%d", a, phys.kofb[bi] & ~Busy);
		phys.kofb[bi] = j;	/* new size */
		b2 = &phys.blocks[bi];
		b2->forw = &phys.blist[j];
		b2->back = phys.blist[j].back;
		phys.blist[j].back = b2;
		b2->back->forw = b2;
	}
	unlock(&phys.lk);
	return a;
}

void
physfree(uintmem a, usize size)
{
	int k;
	Blk *b, *b2;
	uintmem a2;
	uint bi, bi2;

	k = log2of(size);	/* could look it up in phys.kofb */
	if(k < MinK)
		k = MinK;
	dprint("free %#P %d\n", a, k);
	bi = BI(a);
	lock(&phys.lk);
	if(phys.kofb[bi] != 0 && phys.kofb[bi] != (Busy|k)){
		unlock(&phys.lk);
		panic("balfree: busy %#P odd k k=%d kfob=%#ux", a, k, phys.kofb[bi]);
	}
	for(; k != MaxK; k++){
		a2 = a ^ ((uintmem)1<<k);	/* buddy */
		bi2 = BI(a2);
		b2 = &phys.blocks[bi2];
		if(bi2 >= phys.maxb || phys.kofb[bi2] != k)	/* valid, not busy, matching size */
			break;
		dprint("combine %#P %#P %d %#P\n", a, a2, k, (uintmem)1<<k);
		b2->back->forw = b2->forw;
		b2->forw->back = b2->back;
		if(a2 < a){
			a = a2;
			bi = bi2;
		}
	}
	phys.kofb[bi] = k;	/* sets size and resets Busy */
	b = &phys.blocks[bi];
	b->forw = &phys.blist[k];
	b->back = phys.blist[k].back;
	phys.blist[k].back = b;
	b->back->forw = b;
	unlock(&phys.lk);
}

void
physallocrange(usize *low, usize *high)
{
	*low = 1<<MinK;
	*high = 1<<MaxK;
}

void
physinitfree(uintmem base, uintmem lim)
{
	uintmem m, size;
	int i;

	/* chop limit to min block alignment */
	lim &= ~(MinBsize-1);
	if(phys.maxb == 0)	/* compatibility */
		physinit(lim);
	if(BI(lim) > phys.maxb){
		print("physinitfree: address space too large");
		lim = IB(phys.maxb);
	}

	/* round base to min block alignment */
	base = (base + MinBsize-1) & ~(MinBsize-1);

	size = lim - base;
	if(size < MinBsize)
		return;
	dprint("physinitfree %#P-%#P [%#P]\n", base, lim, size);

	/* move up from base in largest blocks that remain aligned */
	for(i=0; i<MaxK; i++){
		m = (uintmem)1 << i;
		if(base & m){
			if(size < m)
				break;
			if(base & (m-1)){
				print(" ** error: %#P %#P\n", base, m);
				return;
			}
			physfree(base, m);
			base += m;
			size -= m;
		}
	}

	/* largest chunks, aligned */
	m = (uintmem)1<<MaxK;
	while(size >= m){
		if(base & (m-1)){
			print(" ** error: %#P %#P\n", base, m);
			return;
		}
		physfree(base, m);
		base += m;
		size -= m;
	}

	/* free remaining chunks, decreasing alignment */
	for(; size != 0; m >>= 1){
		if(size & m){
			dprint("\t%#P %#P\n", base, m);
			if(base & (m-1)){
				print(" ** error: %#P %#P\n", base, m);
				return;
			}
			physfree(base, m);
			base += m;
			size &= ~m;
		}
	}
}

void
physdump(void)
{
	char *s;
	uintmem a;
	uint bi;
	int i, k;
	Blk *b;

	s = getconf("*physdump");
	if(s == 0 || atoi(s) == 0)
		return;

	for(i=0; i<nelem(phys.blist); i++){
		b = phys.blist[i].forw;
		if(b != &phys.blist[i]){
			print("%d	", i);
			for(; b != &phys.blist[i]; b = b->forw){
				bi = b-phys.blocks;
				a = IB(bi);
				k = phys.kofb[bi];
				print(" [%#P %d %#ux b=%#P]", a, k, 1<<k, a^((uintmem)1<<k));
			}
			print("\n");
		}
	}
}
