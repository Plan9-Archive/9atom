#include "all.h"
#include "mem.h"
#include "io.h"

ulong	niob;
ulong	nhiob;
Hiob	*hiob;

void
prbanks(void)
{
	Mbank *b;
	int m;

	for(m = 0; m < mconf.nbank; m++){
		b = mconf.bank+m;
		print("bank[%d]: base %#p, limit %#p\n", m, b->base, b->limit);
	}
}


/*
 * Called to allocate permanent data structures
 * Alignment is in number of bytes. It pertains both to the start and
 * end of the allocated memory.
 */
void*
ialloc(ulong n, int align)
{
	Mbank *b;
	ulong p;
	int m;

	ilock(&mconf);
	for(b = mconf.bank; b < mconf.bank+mconf.nbank; b++){
		p = b->base;

		if(align <= 0)
			align = 4;
		if(m = n % align)
			n += align - m;
		if(m = p % align)
			p += align - m;

		if(p+n > b->limit)
			continue;

		b->base = p+n;
		iunlock(&mconf);

		memset((void*)(p+KZERO), 0, n);
		return (void*)(p+KZERO);
	}

	iunlock(&mconf);

	prbanks();
	panic("ialloc(%ld, %ld): out of memory: %#p\n", n, align, getcallerpc(&n));
	return 0;
}

static void
cmd_memory(int, char *[])
{
	prbanks();
}

/*
 * allocate rest of mem
 * for io buffers.
 */
#define	HWIDTH	8	/* buffers per hash */
void
iobufinit(void)
{
	ulong m, i;
	Iobuf *p, *q;
	Hiob *hp;
	Mbank *b;

	wlock(&mainlock);	/* init */
	wunlock(&mainlock);

	prbanks();
	m = 0;
	for(b = mconf.bank; b < mconf.bank+mconf.nbank; b++)
		m += b->limit - b->base;

	m -= conf.sparemem;

	niob = m / (sizeof(Iobuf) + RBUFSIZE + sizeof(Hiob)/HWIDTH);
	nhiob = niob / HWIDTH;
	while(!prime(nhiob))
		nhiob++;
	print("	%ld buffers; %ld hashes\n", niob, nhiob);
	hiob = ialloc(nhiob * sizeof(Hiob), 0);
	hp = hiob;
	for(i=0; i<nhiob; i++) {
		snprint(hp->namebuf, sizeof hp->namebuf, "hiob%uld\n", i);
		hp->name = hp->namebuf;
		qlock(hp);
		qunlock(hp);
		hp++;
	}
	p = ialloc(niob * sizeof(Iobuf), 0);
	hp = hiob;
	for(i=0; i<niob; i++) {
//		p->name = "buf";
		snprint(p->namebuf, sizeof p->namebuf, "buf%uld", i);
		p->name = p->namebuf;
		qlock(p);
		qunlock(p);
		if(hp == hiob)
			hp = hiob + nhiob;
		hp--;
		q = hp->link;
		if(q) {
			p->fore = q;
			p->back = q->back;
			q->back = p;
			p->back->fore = p;
		} else {
			hp->link = p;
			p->fore = p;
			p->back = p;
		}
		p->dev = devnone;
		p->addr = -1;
		p->xiobuf = ialloc(RBUFSIZE, RBUFSIZE);
		p->iobuf = (char*)-1;
		p++;
	}

	/*
	 * Make sure that no more of bank[0] can be used:
	 * 'check' will do an ialloc(0, 1) to find the base of
	 * sparemem.
	 */
	if(mconf.bank[0].limit < 1024*1024)
		mconf.bank[0].base = mconf.bank[0].limit+1;

	i = 0;
	for(b = mconf.bank; b < mconf.bank+mconf.nbank; b++)
		i += b->limit - b->base;
	print("	mem left = %,uld, out of %,uld\n", i, conf.mem);
	/* paranoia: add this command as late as is easy */
	cmd_install("memory", "-- print ranges of memory banks", cmd_memory);
}

void*
iobufmap(Iobuf *p)
{
	return p->iobuf = p->xiobuf;
}

void
iobufunmap(Iobuf *p)
{
	p->iobuf = (char*)-1;
}
