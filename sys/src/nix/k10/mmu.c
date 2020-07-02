#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "amd64.h"
#include "adr.h"

/*
 * To do:
 *	PteNX;
 *	mmukmapsync grot for >1 processor;
 *	mmuptcopy (PteSHARED trick?);
 */

#define PPN(x)		((x)&~(PGSZ-1))

/*
 * set up a pat mappings.  the system depends
 * on the first 4 mappings not changing.
 */
enum{
	Patmsr	= 0x277,
};

static uchar pattab[8] = {
	PATWB,
	PATWT,
	PATUCMINUS,
	PATUC, 

	PATWB,
	PATWT,
	PATUCMINUS,
	PATUC,
};

static uint patflags[8] = {
	0,
					PtePWT,
			PtePCD,
			PtePCD |	PtePWT,
	Pte4KPAT,
	Pte4KPAT | 			PtePWT,
	Pte4KPAT |	PtePCD,
	Pte4KPAT |	PtePCD |	PtePWT,
};

static void
setpatreg(int rno, int type)
{
	int i;
	Mpl s;
	u64int pat;

	s = splhi();
	pat = rdmsr(Patmsr);
	pat &= ~(0xffull<<rno*8);
	pat |= (u64int)type<<rno*8;
	wrmsr(Patmsr, pat);
	splx(s);

	if(m->machno == 0)
		print("pat: %.16llux\n", pat);
	for(i = 0; i < 64; i += 8)
		pattab[i>>3] = pat>>i;
}

static void
patinit(void)
{
	setpatreg(7, PATWC);
}

/* adjust memory flags based on page table level (bits shift around) */
static uint
memflagssz(uint flag, int ps)
{
	if(flag & Pte4KPAT && ps > 4*1024){
		flag &= ~Pte4KPAT;
		flag |= Pte2MPAT | PtePS;
	}
	else if(ps > 4*1024)
		flag |= PtePS;
	return flag;
}

void
mmuflushtlb(uintmem)
{

	m->tlbpurge++;
	if(m->pml4->daddr){
		memset(UINT2PTR(m->pml4->va), 0, m->pml4->daddr*sizeof(PTE));
		m->pml4->daddr = 0;
	}
	cr3put(m->pml4->pa);
}

void
mmuflush(void)
{
	Mpl pl;

	pl = splhi();
	up->newtlb = 1;
	mmuswitch(up);
	splx(pl);
}

static void
mmuptpfree(Proc* proc, int clear)
{
	int l;
	PTE *pte;
	Page **last, *page;

	for(l = 1; l < 4; l++){
		last = &proc->mmuptp[l];
		if(*last == nil)
			continue;
		for(page = *last; page != nil; page = page->next){
//what is right here? 2 or 1?
			if(l <= 2 && clear)
				memset(UINT2PTR(page->va), 0, PTSZ);
			pte = UINT2PTR(page->prev->va);
			pte[page->daddr] = 0;
			last = &page->next;
		}
		*last = proc->mmuptp[0];
		proc->mmuptp[0] = proc->mmuptp[l];
		proc->mmuptp[l] = nil;
	}

	m->pml4->daddr = 0;
}

static void
tabs(int n)
{
	int i;

	for(i = 0; i < n; i++)
		print("  ");
}

void
dumpptepg(int lvl, uintmem pa)
{
	PTE *pte;
	int tab, i;

	tab = 4 - lvl;
	pte = UINT2PTR(KADDR(pa));
	for(i = 0; i < PTSZ/sizeof(PTE); i++)
		if(pte[i] & PteP){
			tabs(tab);
			print("l%d %#p[%#05x]: %#llux\n", lvl, pa, i, pte[i]);

			/* skip kernel mappings */
			if((pte[i]&PteU) == 0){
				tabs(tab+1);
				print("...kern...\n");
				continue;
			}
			if(lvl > 2)
				dumpptepg(lvl-1, PPN(pte[i]));
		}
}

void
dumpmmu(Proc *p)
{
	int i;
	Page *pg;

	print("proc %#p\n", p);
	for(i = 3; i > 0; i--){
		print("mmuptp[%d]:\n", i);
		for(pg = p->mmuptp[i]; pg != nil; pg = pg->next)
			print("\tpg %#p = va %#p pa %#P"
				" daddr %#lux next %#p prev %#p\n",
				pg, pg->va, pg->pa, pg->daddr, pg->next, pg->prev);
	}
	print("pml4 %#P\n", m->pml4->pa);
	if(0)dumpptepg(4, m->pml4->pa);
}

void
dumpmmuwalk(uintmem addr)
{
	int l;
	PTE *pte, *pml4;

	pml4 = UINT2PTR(m->pml4->va);
	if((l = mmuwalk(pml4, addr, 3, &pte, nil)) >= 0)
		print("cpu%d: mmu l%d pte %#p = %llux\n", m->machno, l, pte, *pte);
	if((l = mmuwalk(pml4, addr, 2, &pte, nil)) >= 0)
		print("cpu%d: mmu l%d pte %#p = %llux\n", m->machno, l, pte, *pte);
	if((l = mmuwalk(pml4, addr, 1, &pte, nil)) >= 0)
		print("cpu%d: mmu l%d pte %#p = %llux\n", m->machno, l, pte, *pte);
	if((l = mmuwalk(pml4, addr, 0, &pte, nil)) >= 0)
		print("cpu%d: mmu l%d pte %#p = %llux\n", m->machno, l, pte, *pte);
}

static Page mmuptpfreelist;

static Page*
mmuptpalloc(void)
{
	void* va;
	Page *page;

	/*
	 * Do not really need a whole Page structure,
	 * but it makes testing this out a lot easier.
	 * Could keep a cache and free excess.
	 * Have to maintain any fiction for pexit?
	 */
	lock(&mmuptpfreelist);
	if((page = mmuptpfreelist.next) != nil){
		mmuptpfreelist.next = page->next;
		mmuptpfreelist.ref--;
		unlock(&mmuptpfreelist);

		if(page->ref++ != 0)
			panic("mmuptpalloc ref");
		page->prev = page->next = nil;
		memset(UINT2PTR(page->va), 0, PTSZ);

		if(page->pa == 0)
			panic("mmuptpalloc: free page with pa == 0");
		return page;
	}
	unlock(&mmuptpfreelist);

	if((page = malloc(sizeof(Page))) == nil){
		print("mmuptpalloc Page\n");

		return nil;
	}
	if((va = mallocalign(PTSZ, PTSZ, 0, 0)) == nil){
		print("mmuptpalloc va\n");
		free(page);

		return nil;
	}

	page->va = PTR2UINT(va);
	page->pa = PADDR(va);
	page->ref = 1;

	if(page->pa == 0)
		panic("mmuptpalloc: no pa");
	return page;
}

void
mmuswitch(Proc* proc)
{
	PTE *pte;
	Page *page;
	Mpl pl;

	pl = splhi();
	if(proc->newtlb){
		mmuptpfree(proc, 1);
		proc->newtlb = 0;
	}

	if(m->pml4->daddr){
		memset(UINT2PTR(m->pml4->va), 0, m->pml4->daddr*sizeof(PTE));
		m->pml4->daddr = 0;
	}

	pte = UINT2PTR(m->pml4->va);
	for(page = proc->mmuptp[3]; page != nil; page = page->next){
		pte[page->daddr] = PPN(page->pa)|PteU|PteRW|PteP;
		if(page->daddr >= m->pml4->daddr)
			m->pml4->daddr = page->daddr+1;
		page->prev = m->pml4;
	}

	tssrsp0(STACKALIGN(PTR2UINT(proc->kstack+KSTACK)));
	cr3put(m->pml4->pa);
	splx(pl);
}

void
mmurelease(Proc* proc)
{
	Page *page, *next;

	mmuptpfree(proc, 0);

	for(page = proc->mmuptp[0]; page != nil; page = next){
		next = page->next;
		if(--page->ref)
			panic("mmurelease: page->ref %d", page->ref);
		lock(&mmuptpfreelist);
		page->next = mmuptpfreelist.next;
		mmuptpfreelist.next = page;
		mmuptpfreelist.ref++;
		page->prev = nil;
		unlock(&mmuptpfreelist);
	}
	if(proc->mmuptp[0] && pga.r.p)
		wakeup(&pga.r);
	proc->mmuptp[0] = nil;

	tssrsp0(STACKALIGN(m->stack+MACHSTKSZ));
	cr3put(m->pml4->pa);
}

static void
checkpte(uintmem ppn, void *a)
{
	int l;
	PTE *pte, *pml4;
	u64int addr;
	char buf[240], *s;

	addr = PTR2UINT(a);
	pml4 = UINT2PTR(m->pml4->va);
	pte = 0;
	s = buf;
	*s = 0;
	if((l = mmuwalk(pml4, addr, 3, &pte, nil)) < 0 || (*pte&PteP) == 0)
		goto Panic;
	s = seprint(buf, buf+sizeof buf,
		"check3: l%d pte %#p = %llux\n",
		l, pte, pte?*pte:~0);
	if((l = mmuwalk(pml4, addr, 2, &pte, nil)) < 0 || (*pte&PteP) == 0)
		goto Panic;
	s = seprint(s, buf+sizeof buf,
		"check2: l%d  pte %#p = %llux\n",
		l, pte, pte?*pte:~0);
	if(*pte&PtePS)
		return;
	if((l = mmuwalk(pml4, addr, 1, &pte, nil)) < 0 || (*pte&PteP) == 0)
		goto Panic;
	seprint(s, buf+sizeof buf,
		"check1: l%d  pte %#p = %llux\n",
		l, pte, pte?*pte:~0);
	return;
Panic:
	
	seprint(s, buf+sizeof buf,
		"checkpte: l%d addr %#p ppn %#P kaddr %#p pte %#p = %llux",
		l, a, ppn, KADDR(ppn), pte, pte?*pte:~0);
	print("%s\n", buf);
	seprint(buf, buf+sizeof buf, "start %#p unused %#p"
		" unmap %#p end %#p\n",
		sys->vmstart, sys->vmunused, sys->vmunmapped, sys->vmend);
	panic("%s", buf);
}


static void
mmuptpcheck(Proc *proc)
{
	int lvl, npgs, i;
	Page *lp, *p, *pgs[16], *fp;
	uint idx[16];

	if(proc == nil)
		return;
	lp = m->pml4;
	for(lvl = 3; lvl >= 2; lvl--){
		npgs = 0;
		for(p = proc->mmuptp[lvl]; p != nil; p = p->next){
			for(fp = proc->mmuptp[0]; fp != nil; fp = fp->next)
				if(fp == p){
					dumpmmu(proc);
					panic("ptpcheck: using free page");
				}
			for(i = 0; i < npgs; i++){
				if(pgs[i] == p){
					dumpmmu(proc);
					panic("ptpcheck: dup page");
				}
				if(idx[i] == p->daddr){
					dumpmmu(proc);
					panic("ptcheck: dup daddr");
				}
			}
			if(npgs >= nelem(pgs))
				panic("ptpcheck: pgs is too small");
			idx[npgs] = p->daddr;
			pgs[npgs++] = p;
			if(lvl == 3 && p->prev != lp){
				dumpmmu(proc);
				panic("ptpcheck: wrong prev");
			}
		}
		
	}
	npgs = 0;
	for(fp = proc->mmuptp[0]; fp != nil; fp = fp->next){
		for(i = 0; i < npgs; i++)
			if(pgs[i] == fp)
				panic("ptpcheck: dup free page");
		pgs[npgs++] = fp;
	}
}

static uint
pteflags(uint attr)
{
	uint flags;

	flags = 0;
	if(attr & ~(PTEVALID|PTEWRITE|PTERONLY|PTEUNCACHED))
		panic("mmuput: wrong attr bits: %#ux", attr);
	if(attr&PTEVALID)
		flags |= PteP;
	if(attr&PTEWRITE)
		flags |= PteRW;
	if(attr&PTEUNCACHED)
		flags |= PtePCD;
	return flags;
}

static int lvltab[4] = {4096, 2*MiB, 1*GiB, -1};

/*
 * pg->pgszi indicates the page size in m->pgsz[] used for the mapping.
 * For the user, it can be either 2*MiB or 1*GiB pages.
 * For 2*MiB pages, we use three levels, not four.
 * For 1*GiB pages, we use two levels.
 */
void
mmuput(uintptr va, Page *pg, uint attr)
{
	int lvl, x, pgsz;
	PTE *pte;
	Page *page, *prev;
	Mpl pl;
	uintmem pa, ppn;
	char buf[80];

	ppn = 0;
	pa = pg->pa;
	if(pa == 0)
		panic("mmuput: zero pa");

	if(DBGFLG){
		snprint(buf, sizeof buf, "cpu%d: up %#p mmuput %#p %#P %#ux\n", 
			m->machno, up, va, pa, attr);
		print("%s", buf);
	}
	assert(pg->pgszi >= 0);
	pgsz = m->pgsz[pg->pgszi];
	if(pa & (pgsz-1))
		panic("mmuput: pa offset non zero: %#P", pa);

	pl = splhi();
	if(DBGFLG)
		mmuptpcheck(up);
	assert(va < KSEG2);

	x = PTLX(va, 3);

	pte = UINT2PTR(m->pml4->va);
	pte += x;
	prev = m->pml4;

	for(lvl = 3; lvl >= 0; lvl--){
		if(lvltab[lvl] == pgsz)
			break;
		for(page = up->mmuptp[lvl]; page != nil; page = page->next)
			if(page->prev == prev && page->daddr == x){
			//	assert(*pte != 0);
				if(*pte == 0){
					print("mmu: parent pte 0 lvl %d va %#p\n", lvl, va);
					*pte = PPN(page->pa)|PteU|PteRW|PteP;
				}
				break;
			}

		if(page == nil){
			if(up->mmuptp[0] == nil)
				page = mmuptpalloc();
			else {
				page = up->mmuptp[0];
				up->mmuptp[0] = page->next;
			}
			page->daddr = x;
			page->next = up->mmuptp[lvl];
			up->mmuptp[lvl] = page;
			page->prev = prev;
			*pte = PPN(page->pa)|PteU|PteRW|PteP;
			if(lvl == 3 && x >= m->pml4->daddr)
				m->pml4->daddr = x+1;
		}
		x = PTLX(va, lvl-1);

		ppn = PPN(*pte);
		if(ppn == 0)
			panic("mmuput: ppn=0 l%d pte %#p = %#P", lvl, pte, *pte);

		pte = UINT2PTR(KADDR(ppn));
		pte += x;
		prev = page;
	}

	if(DBGFLG)
		checkpte(ppn, pte);
	*pte = pa|PteU|pteflags(attr)|memflagssz(adrmemflags(pa), pgsz);

	splx(pl);

	if(DBGFLG){
		snprint(buf, sizeof buf, "cpu%d: up %#p new pte %#p = %#llux\n", 
			m->machno, up, pte, pte?*pte:~0);
		print("%s", buf);
	}

	invlpg(va);			/* only if old entry valid? */
}

static Lock mmukmaplock;
static Lock vmaplock;

#define PML4X(v)	PTLX((v), 3)
#define PDPX(v)		PTLX((v), 2)
#define PDX(v)		PTLX((v), 1)
#define PTX(v)		PTLX((v), 0)

/* allocate page directories &c for vmaps */
static uintmem
walkalloc(usize size)
{
	void *va;

	if((va = mallocalign(size, PTSZ, 0, 0)) != nil)
		return PADDR(va);
	panic("walkalloc: fail");
	return 0;
}

uintptr
kseg2map(uintmem pa, uintmem len, uint basef)
{
	int i, l;
	uintptr va;
	uintmem mem, nextmem;
	PTE *pte, *pml4;

	DBG("kseg2map: %#P %#P size %P\n", pa, pa+len, len);
	pml4 = UINT2PTR(m->pml4->va);
	va = KSEG2+pa;
	for(mem = pa; mem < pa+len; mem = nextmem){
		nextmem = (mem + PGLSZ(0)) & ~m->pgszmask[0];
		for(i = m->npgsz - 1; i >= 0; i--){
			if((mem & m->pgszmask[i]) != 0)
				continue;
			if(mem + PGLSZ(i) > pa+len)
				continue;
			if((l = mmuwalk(pml4, va, i, &pte, walkalloc)) < 0)
				panic("mmu: kseg2map");
			*pte = mem|memflagssz(basef, PGLSZ(l));
			nextmem = mem + PGLSZ(i);
			va += PGLSZ(i);
			break;
		}
	}
	return KSEG2+pa;
}

int
vmapsync(uintptr va)
{
	int use, flags;
	uintmem pa, sz;

	pa = va - KSEG2;
	if((pa = adrmemtype(pa, &sz, &flags, &use)) == 0 || use != Mvmap)
		return -1;
	iprint("%d: vmapsync! %#P %#P\n", m->machno, pa, sz);
	kseg2map(pa, sz, flags|PteP|PteG);
	return 0;
}

/*
 * KSEG0 maps (some of) low memory.
 * KSEG2 lazily maps all physical addresses at KSEG2 + pa.
 * Valid physical addresses are determined by the address space
 * map (adr.c).  Regular memory is already mapped.  Vmap() is
 * required to access pci space and other sundary items.
 */
void*
vmapflags(uintmem pa, usize size, uint flags)
{
	uintptr va;
	usize o, sz;

	DBG("%d: vmapflags(%#P, %lud, %ux)\n", m->machno, pa, size, flags);
	assert(sys->meminit == 1);

	/* Might be asking for less than a page. */
	o = pa & ((1<<PGSHIFT)-1);
	pa -= o;
	sz = ROUNDUP(size+o, PGSZ);

	/*
	 * This is incomplete; the checks are not comprehensive
	 * enough.  Assume requests for low memory are already mapped.
	 */
	if(pa+size <= 1ull*MiB)
		return KADDR(pa+o);
	if(pa < 1ull*MiB)
		return nil;

	/*
	 * only adralloc the actual request.  pci bars can be less than 1 page.
	 * take it on faith that they don't overlap.
	 */
	if(pa == 0 || adralloc(pa+o, size, 0, -1, Mvmap, flags) == 0){
		print("vmapflags(0, %lud) pc=%#p\n", size, getcallerpc(&pa));
		return nil;
	}

	ilock(&vmaplock);
	kseg2map(pa, sz, flags|PteP|PteG);
	va = KSEG2 + pa;
	iunlock(&vmaplock);

	DBG("%d: vmapflags(%#P, %lud, %#ux) → %#p + %lux\n", m->machno, pa, sz, flags, va, o);
	return UINT2PTR(va + o);
}

void*
vmap(uintmem pa, usize size)
{
	DBG("vmap(%#p, %lud) pc=%#p\n", pa, size, getcallerpc(&pa));
	return vmapflags(pa, size, PtePCD|PteRW);
}

void*
vmappat(uintmem pa, usize size, uint pattype)
{
	int i;

	DBG("vmappat(%#p, %lud, %#ux) pc=%#p\n", pa, size, pattype, getcallerpc(&pa));
	for(i = 0; i < nelem(pattab); i++)
		if(pattab[i] == pattype)
			return vmapflags(pa, size, patflags[i]|PteRW);
	return vmap(pa, size);
}

void
vunmap(void* v, usize size)
{
	uintptr va;
	uintmem pa;
	usize o, sz;

	DBG("vunmap(%#p, %lud)\n", v, size);

	/* See the comments above in vmap. */
	va = PTR2UINT(v);
	if(va >= KZERO && va+size < KZERO+1ull*MiB)
		return;

	/* Might be asking for less than a page. */
	pa = va - KSEG2;
	o = pa & ((1<<PGSHIFT)-1);
	pa -= o;
	sz = ROUNDUP(size+o, PGSZ);

	adrfree(pa+o, size);
//	kseg2map(pa, sz, 0);
	if(active.thunderbirdsarego == 0){
		kseg2map(pa, sz, 0);
		cr3put(m->pml4->pa);
	}else{
//		wait for all other maches to reload cr3
	}

	/*
	 * Here we might deal with releasing resources
	 * used for the allocation (e.g. page table pages).
	 */
	DBG("vunmap(%#p, %lud)\n", v, size);
}

int
mmuwalk(PTE* pml4, uintptr va, int level, PTE** ret, uintmem (*alloc)(usize))
{
	int l;
	uintmem pa;
	PTE *pte;
	Mpl pl;

	pl = splhi();
	if(DBGFLG > 1)
		DBG("mmuwalk%d: va %#p level %d\n", m->machno, va, level);
	pte = &pml4[PTLX(va, 3)];
	for(l = 3; l >= 0; l--){
		if(l == level)
			break;
		if(!(*pte & PteP)){
			if(alloc == nil)
				break;
			pa = alloc(PTSZ);
			if(pa == ~0)
				return -1;
			memset(UINT2PTR(KADDR(pa)), 0, PTSZ);
			*pte = pa|PteRW|PteP;
		}
		else if(*pte & PtePS)
			break;
		pte = UINT2PTR(KADDR(PPN(*pte)));
		pte += PTLX(va, l-1);
	}
	*ret = pte;
	splx(pl);
	return l;
}

uintmem
mmuphysaddr(uintptr va)
{
	int l;
	PTE *pte;
	uintmem mask, pa;

	/*
	 * Given a VA, find the PA.
	 * This is probably not the right interface,
	 * but will do as an experiment. Usual
	 * question, should va be void* or uintptr?
	 */
	l = mmuwalk(UINT2PTR(m->pml4->va), va, 0, &pte, nil);
	DBG("physaddr: va %#p l %d\n", va, l);
	if(l < 0)
		return ~0;

	mask = PGLSZ(l)-1;
	pa = (*pte & ~mask) + (va & mask);

	DBG("physaddr: l %d va %#p pa %#P\n", l, va, pa);

	return pa;
}

Page mach0pml4;

static void
nxeon(void)
{
	u32int idres[4];

	/* on intel64, cpuid 0x8::1 DX bit 20 means "Nxe bit in Efer allowed" */
	cpuid(0x80000001, 0, idres);
	if (idres[3] & (1<<20))
		wrmsr(Efer, rdmsr(Efer) | Nxe);
}

void
apmmuinit(void)
{
	uchar *p;

	archmmu();
	DBG("mach%d: %#p pml4 %#p npgsz %d\n", m->machno, m, m->pml4, m->npgsz);

	/*
	 *  NIX: KLUDGE: Has to go when each mach is using
	 * its own page table
	 */
	p = UINT2PTR(m->stack);
	p += MACHSTKSZ;

	memmove(p, UINT2PTR(mach0pml4.va), PTSZ);
	m->pml4 = &m->pml4kludge;
	m->pml4->va = PTR2UINT(p);
	m->pml4->pa = PADDR(p);
	m->pml4->daddr = mach0pml4.daddr;	/* # of user mappings in pml4 */

	nxeon();
	patinit();
	cr3put(m->pml4->pa);
	DBG("m %#p pml4 %#p\n", m, m->pml4);
}

void
mmuinit(void)
{
	Page *page;

	assert(m->machno == 0);
	archmmu();
	DBG("mach%d: %#p pml4 %#p npgsz %d\n", m->machno, m, m->pml4, m->npgsz);

	page = &mach0pml4;
	page->pa = cr3get();
	page->va = PTR2UINT(KADDR(page->pa));

	m->pml4 = page;

	nxeon();
	patinit();

	/*
	 * Set up the various kernel memory allocator limits:
	 * pmstart/pmend bound the unused physical memory;
	 * (but pmstart is also 0+INIMAP)
	 * vmstart/vmend bound the total possible virtual memory
	 * used by the kernel;
	 * vmunused is the highest virtual address currently mapped
	 * and used by the kernel;
	 * vmunmapped is the highest virtual address currently
	 * mapped by the kernel.
	 * Vmunused can be bumped up to vmunmapped before more
	 * physical memory needs to be allocated and mapped.
	 *
	 * This is set up here so meminit can map appropriately.
	 */
	sys->vmstart = KSEG0;
	sys->vmend = sys->vmstart + TMFM;
	sys->vmunused = sys->vmstart + ROUNDUP(PADDR(end), BIGPGSZ);
	sys->vmunmapped = sys->vmstart + INIMAP;

	print("pmstart %#P pmend %#P\n", sys->pmstart, sys->pmend);
	print("mmuinit: vmstart %#p vmunused %#p vmunmapped %#p vmend %#p\n",
		sys->vmstart, sys->vmunused, sys->vmunmapped, sys->vmend);
	if(sys->vmunused > sys->vmunmapped)
		panic("kernel too big: PADDR(end) > inimap %#P", (uintmem)INIMAP);

	if(DBGFLG)
		dumpmmuwalk(KZERO);
	mmuphysaddr(PTR2UINT(end));
}
