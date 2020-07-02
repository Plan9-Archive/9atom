#include	"all.h"
#include	"mem.h"
#include	"io.h"
#include	"ureg.h"

/*
 *  task state segment.  Plan 9 ignores all the task switching goo and just
 *  uses the tss for esp0 and ss0 on gate's into the kernel, interrupts,
 *  and exceptions.  The rest is completely ignored.
 *
 *  This means that we only need one tss in the whole system.
 */
typedef struct Tss	Tss;
struct Tss
{
	ulong	backlink;	/* unused */
	ulong	sp0;		/* pl0 stack pointer */
	ulong	ss0;		/* pl0 stack selector */
	ulong	sp1;		/* pl1 stack pointer */
	ulong	ss1;		/* pl1 stack selector */
	ulong	sp2;		/* pl2 stack pointer */
	ulong	ss2;		/* pl2 stack selector */
	ulong	cr3;		/* page table descriptor */
	ulong	eip;		/* instruction pointer */
	ulong	eflags;		/* processor flags */
	ulong	eax;		/* general (hah?) registers */
	ulong 	ecx;
	ulong	edx;
	ulong	ebx;
	ulong	esp;
	ulong	ebp;
	ulong	esi;
	ulong	edi;
	ulong	es;		/* segment selectors */
	ulong	cs;
	ulong	ss;
	ulong	ds;
	ulong	fs;
	ulong	gs;
	ulong	ldt;		/* local descriptor table */
	ulong	iomap;		/* io map base */
};
Tss tss;

/*
 *  segment descriptor initializers
 */
//#define	DATASEGM(p) 	{ 0xFFFF, SEGG|SEGB|(0xF<<16)|SEGP|SEGPL(p)|SEGDATA|SEGW }
#define	DATASEGM(p) 	{ 1     , SEGG|SEGB|(0<<16)|SEGP|SEGPL(p)|SEGDATA|SEGW|SEGE }

#define	EXECSEGM(p) 	{ 0xFFFF, SEGG|SEGD|(0xF<<16)|SEGP|SEGPL(p)|SEGEXEC|SEGR }
#define CALLGATE(s,o,p)	{ ((o)&0xFFFF)|((s)<<16), (o)&0xFFFF0000|SEGP|SEGPL(p)|SEGCG }
#define	D16SEGM(p) 	{ 0xFFFF, (0x0<<16)|SEGP|SEGPL(p)|SEGDATA|SEGW }
#define	E16SEGM(p) 	{ 0xFFFF, (0x0<<16)|SEGP|SEGPL(p)|SEGEXEC|SEGR }
#define	TSSSEGM(b,p)	{ ((b)<<16)|sizeof(Tss),\
			  ((b)&0xFF000000)|(((b)>>16)&0xFF)|SEGTSS|SEGPL(p)|SEGP }

/*
 *  global descriptor table describing all segments
 */
Segdesc gdt[] =
{
[NULLSEG]	{ 0, 0},		/* null descriptor */
[KDSEG]		DATASEGM(0),		/* kernel data/stack */
[KESEG]		EXECSEGM(0),		/* kernel code */
[UDSEG]		DATASEGM(3),		/* user data/stack */
[UESEG]		EXECSEGM(3),		/* user code */
[TSSSEG]	TSSSEGM(0,0),		/* tss segment */
};

static struct {
	ulong	va;
	ulong	pa;
} ktoppg;			/* prototype top level page table
				 * containing kernel mappings  */
static ulong	*kpt;		/* 2nd level page tables for kernel mem */

#define ROUNDUP(s,v)	(((s)+(v-1))&~(v-1))
/*
 *  offset of virtual address into
 *  top level page table
 */
#define TOPOFF(v)	(((ulong)(v))>>(2*PGSHIFT-2))

/*
 *  offset of virtual address into
 *  bottom level page table
 */
#define BTMOFF(v)	((((ulong)(v))>>(PGSHIFT))&(WD2PG-1))

/*
 *  Change current page table and the stack to use for exceptions
 *  (traps & interrupts).  The exception stack comes from the tss.
 *  Since we use only one tss, (we hope) there's no need for a
 *  puttr().
 */
static void
taskswitch(ulong pagetbl, ulong stack)
{
	tss.ss0 = KDSEL;
	tss.sp0 = stack;
tss.ss1 = KDSEL;
tss.sp1 = stack;
tss.ss2 = KDSEL;
tss.sp2 = stack;
	tss.cr3 = pagetbl;
	putcr3(pagetbl);
}

/*
 *  Create a prototype page map that maps all of memory into
 *  kernel (KZERO) space.  This is the default map.  It is used
 *  whenever the processor is not running a process or whenever running
 *  a process which does not yet have its own map.
 */

void
mmuinit(void)
{
	ulong i, nkpt, npage, x, y, *top, nbytes, pgsz, flag;

	/*
	 *  set up the global descriptor table. we make the tss entry here
	 *  since it requires arithmetic on an address and hence cannot
	 *  be a compile or link time constant.
	 */
	x = (ulong)&tss;
	gdt[TSSSEG].d0 = (x<<16)|sizeof(Tss);
	gdt[TSSSEG].d1 = (x&0xFF000000)|((x>>16)&0xFF)|SEGTSS|SEGPL(0)|SEGP;
	putgdt(gdt, sizeof gdt);

	/*
	 *  set up system page tables.
	 *  map all of physical memory to start at KZERO.
	 *  leave a map entry for a user area.
	 */

	/*
	 *  allocate top level table
	 */
	top = ialloc(BY2PG, BY2PG);

	ktoppg.va = (ulong)top;
	ktoppg.pa = PADDR(ktoppg.va);

	flag = PTEVALID|PTEKERNEL|PTEWRITE;
	pgsz = BY2PG;
	if(m->cpuiddx & 0x08){
		putcr4(getcr4()|0x10);
		pgsz = 4*MB;
		flag |= PTESIZE;
	}

	/*  map all memory to KZERO */
	npage = mconf.topofmem/pgsz;

	if(pgsz == BY2PG){
		nbytes = PGROUND(npage*BY2WD);		/* words of page map */
		nkpt = nbytes/BY2PG;			/* pages of page map */
		kpt = ialloc(nbytes, BY2PG);
		for(i = 0; i < npage; i++)
			kpt[i] = (0+i*pgsz)|flag;
		y = PADDR((ulong)kpt);
	} else{
		nkpt = npage;
		y = 0;
	}
	x = TOPOFF(KZERO);
	for(i = 0; i < nkpt; i++)
		top[x+i] = (y+i*pgsz)|flag;

	/*
	 *  set up the task segment
	 */
	memset(&tss, 0, sizeof(tss));
	taskswitch(ktoppg.pa, pgsz + (ulong)m);
	puttr(TSSSEL);/**/
}

/*
 *  used to map a page into 4 meg - BY2PG for confinit(). tpt is the temporary
 *  page table set up by l.s.
 */
enum{
	Pteoff	= 4*MB-BY2PG,
};

ulong*
mapaddr(ulong paddr)
{
	ulong base;
	ulong off;
	static ulong *pte, top;
	extern ulong tpt[];

	if(pte == 0){
		top = getcr3();
		pte = (ulong*)KADDR(top-BY2PG)+(Pteoff>>PGSHIFT);
	}

	off = paddr&(BY2PG-1);
	base = paddr-off;

	*pte = base|PTEVALID|PTEKERNEL|PTEWRITE; /**/
	putcr3(top);

	return (ulong*)(KZERO+Pteoff+off);
}

#define PDX(va)		((((ulong)(va))>>22) & 0x03FF)
#define PTX(va)		((((ulong)(va))>>12) & 0x03FF)
#define PPN(x)		((x)&~(BY2PG-1))

ulong*
mmuwalk(ulong* pdb, ulong va, int level, int create)
{
	ulong pa, *table;

	/*
	 * Walk the page-table pointed to by pdb and return a pointer
	 * to the entry for virtual address va at the requested level.
	 * If the entry is invalid and create isn't requested then bail
	 * out early. Otherwise, for the 2nd level walk, allocate a new
	 * page-table page and register it in the 1st level.
	 */
//print("mmuwalk(%p, %p, %d, %d)\n", pdb, va, level, create);

	table = &pdb[PDX(va)];
	if(!(*table & PTEVALID) && create == 0){
		print("pte not valid\n");
		return 0;
	}

	switch(level){

	default:
		return 0;

	case 1:
		return table;

	case 2:
		if(*table & PTESIZE)
			panic("mmuwalk2: va 0x%ux entry 0x%ux\n", va, *table);
		if(!(*table & PTEVALID)){
			pa = PADDR(ialloc(BY2PG, BY2PG));
			*table = pa|PTEWRITE|PTEVALID;
		}
		table = KADDR(PPN(*table));
//print("  table -> %p %p\n", table, &table[PTX(va)]);
		return &table[PTX(va)];
	}
}

#define	ROUND(s, sz)	(((s)+((sz)-1))&~((sz)-1))

enum{
	WD2PG		= 4096/4,
	BY2XPG	= 4096*1024,	/* bytes per big page */
	VPTSIZE	= BY2XPG,
	KMAPSIZE	= BY2XPG,
	VMAPSIZE	= 0x10000000-VPTSIZE-KMAPSIZE,
	VPT		= KZERO-VPTSIZE,
	KMAP		= VPT-KMAPSIZE,
	VMAP		= KMAP-VMAPSIZE,
};


static Lock vmaplock;

static int
findhole(ulong *a, int n, int count)
{
	int have, i;
	
	have = 0;
	for(i=0; i<n; i++){
		if(a[i] == 0)
			have++;
		else
			have = 0;
		if(have >= count)
			return i+1 - have;
	}
	return -1;
}

/*
 * Look for free space in the vmap.
 */
static ulong
vmapalloc(ulong size)
{
	int i, n, o;
	ulong *vpdb, *pdb;
	int vpdbsize;

	pdb = (ulong*)ktoppg.va;
	vpdb = &pdb[PDX(VMAP)];
	vpdbsize = VMAPSIZE/(4*MB);

	if(size >= 4*MB){
		n = (size+4*MB-1) / (4*MB);
		if((o = findhole(vpdb, vpdbsize, n)) != -1)
			return VMAP + o*4*MB;
		return 0;
	}
	n = (size+BY2PG-1) / BY2PG;
	for(i=0; i<vpdbsize; i++)
		if((vpdb[i]&PTEVALID) && !(vpdb[i]&PTESIZE))
			if((o = findhole(KADDR(PPN(vpdb[i])), WD2PG, n)) != -1)
				return VMAP + i*4*MB + o*BY2PG;
	if((o = findhole(vpdb, vpdbsize, 1)) != -1)
		return VMAP + o*4*MB;
		
	/*
	 * could span page directory entries, but not worth the trouble.
	 * not going to be very much contention.
	 */
	return 0;
}

int
pdbmap(ulong *pdb, ulong pa, ulong va, int size)
{
	int pse;
	ulong pgsz, *pte, *table;
	ulong flag, off;
	extern int cpuiddx;

	flag = pa&0xFFF;
	pa &= ~0xFFF;

	if((m->cpuiddx & 0x08) && (getcr4() & 0x10))
		pse = 1;
	else
		pse = 0;

	for(off=0; off<size; off+=pgsz){
		table = &pdb[PDX(va+off)];
		if((*table&PTEVALID) && (*table&PTESIZE))
			panic("vmap: va=%#.8lux pa=%#.8lux pde=%#.8lux",
				va+off, pa+off, *table);

		/*
		 * Check if it can be mapped using a 4MB page:
		 * va, pa aligned and size >= 4MB and processor can do it.
		 */
		if(pse && (pa+off)%(4*MB) == 0 && (va+off)%(4*MB) == 0 && (size-off) >= 4*MB){
			*table = (pa+off)|flag|PTESIZE|PTEVALID;
			pgsz = 4*MB;
		}else{
			pte = mmuwalk(pdb, va+off, 2, 1);
			if(*pte&PTEVALID)
				panic("vmap: va=%#.8lux pa=%#.8lux pte=%#.8lux",
					va+off, pa+off, *pte);
			*pte = (pa+off)|flag|PTEVALID;
			pgsz = BY2PG;
		}
	}
	return 0;
}

void*
vmap(ulong pa, int size)
{
	int osize;
	ulong o, va, *pdb;
	
	/*
	 * might be asking for less than a page.
	 */
	osize = size;
	o = pa & (BY2PG-1);
	pa -= o;
	size += o;

	size = ROUND(size, BY2PG);
	if(pa == 0){
		print("vmap pa=0 pc=%#.8lux\n", getcallerpc(&pa));
		return nil;
	}
	ilock(&vmaplock);
	pdb = (ulong*)ktoppg.va;
	if((va = vmapalloc(size)) == 0 
	|| pdbmap(pdb, pa|PTEUNCACHED|PTEWRITE, va, size) < 0){
		iunlock(&vmaplock);
		return 0;
	}
	iunlock(&vmaplock);
	USED(osize);
//	print("  vmap %#.8lux %d => %#.8lux\n", pa+o, osize, va+o);
	return (void*)(va + o);
}

ulong
upamalloc(ulong pa, int size, int)
{
	return (ulong)vmap(pa, size);
}
