#define checkmmu(a, b)
#define countpagerefs(a, b)

#include "../port/portfns.h"

int	led(int, int);
void	ledexit(int);
void	delay(int);
void	uartkirkwoodconsole(void);

#pragma	varargck argpos	_uartprint 1

Dirtab*	addarchfile(char*, int, long(*)(Chan*, void*, long, vlong), long(*)(Chan*, void*, long, vlong));
void	archreboot(void);
void	archconfinit(void);
void	archreset(void);
int	archmmu(void);
void	barriers(void);
void	cachedinv(void);
void	cachedinvse(void*, int);
void	cachedwb(void);
void	cachedwbinv(void);
void	cachedwbinvse(void*, int);
void	cachedwbse(void*, int);
void	cacheiinv(void);
void	cacheuwbinv(void);
uintptr	cankaddr(uintmem pa);
void	clockshutdown(void);
int	clz(u32int);

#define coherence barriers

u32int	controlget(void);
u32int	cpctget(void);
u32int	cpidget(void);
char*	cputype2name(char *, int);
u32int	cprd(int cp, int op1, int crn, int crm, int op2);
u32int	cprdsc(int op1, int crn, int crm, int op2);
void	cpuidprint(void);
void	cpwr(int cp, int op1, int crn, int crm, int op2, u32int val);
void	cpwrsc(int op1, int crn, int crm, int op2, u32int val);
#define cycles(ip) *(ip) = lcycles()
u32int	dacget(void);
void	dacput(u32int);
u32int	farget(void);
u32int	fsrget(void);
int	ispow2(uvlong);
void	l1cachesoff(void);
void	l1cacheson(void);
void	l2cachecfgoff(void);
void	l2cachecfgon(void);
void	l2cacheon(void);
void	l2cacheuinv(void);
void	l2cacheuinvse(void*, int);
void	l2cacheuwb(void);
void	l2cacheuwbinv(void);
void	l2cacheuwbinvse(void*, int);
void	l2cacheuwbse(void*, int);
void	lastresortprint(char *buf, long bp);
int	log2(ulong);
void	mmuinvalidate(void);		/* 'mmu' or 'tlb'? */
void	mmuinvalidateaddr(u32int);		/* 'mmu' or 'tlb'? */
u32int	pidget(void);
void	pidput(u32int);
void	procrestore(Proc *);
void	procsave(Proc*);
void	procsetup(Proc*);
void	_reset(void);
void	setr13(int, u32int*);
void	syscallfmt(int syscallno, va_list list);
void	sysretfmt(int, va_list, Ar0*, uvlong, uvlong);
int	tas(void *);
u32int	ttbget(void);
void	ttbput(u32int);

Dev*	devtabget(int, int);
void	devtabinit(void);
void	devtabreset(void);
long	devtabread(Chan*, void*, long, vlong);
void	devtabshutdown(void);

void	intrclear(int sort, int v);
void	intrenable(int sort, int v, void (*f)(Ureg*, void*), void *a, char *name);
void	intrdisable(int sort, int v, void (*f)(Ureg*, void*), void* a, char *name);
void	vectors(void);
void	vtable(void);

/*
 * Things called in main.
 */
void	clockinit(void);
void	i8250console(void);
void	links(void);
long	lcycles(void);
void	mmuinit(void);
void	touser(uintptr);
void	trapinit(void);

int	fpiarm(Ureg*);
int	fpudevprocio(Proc*, void*, long, uintptr, int);
void	fpuinit(void);
void	fpunoted(void);
void	fpunotify(Ureg*);
void	fpuprocrestore(Proc*);
void	fpuprocsave(Proc*);
void	fpusysprocsetup(Proc*);
void	fpusysrfork(Ureg*);
void	fpusysrforkchild(Proc*, Proc*);
int	fpuemu(Ureg*);

/*
 * Miscellaneous machine dependent stuff.
 */
char*	getenv(char*, char*, int);
char*	getconf(char*);
uintptr	mmukmap(uintptr, uintptr, usize);
uintptr	mmukunmap(uintptr, uintptr, usize);
void*	mmuuncache(void*, usize);
void*	ucalloc(usize);
Block*	ucallocb(int);
void*	ucallocalign(usize size, int align, int span);
void	ucfree(void*);
void	ucfreeb(Block*);
Block*	uciallocb(int);
void	dumpstackwithureg(Ureg*);

/*
 * Things called from port.
 */
void	delay(int);			/* only scheddump() */
int	islo(void);
void	microdelay(int);			/* only edf.c */
void	evenaddr(uintptr);
void	idlehands(void);
void	hardhalt(void);
void	setkernur(Ureg*, Proc*);		/* only devproc.c */
//void	spldone(void);
Mpl	splfhi(void);
Mpl	splflo(void);
void	sysprocsetup(Proc*);

/*
 * PCI
 */
usize	pcibarsize(Pcidev*, int);
void	pcibussize(Pcidev*, usize*, usize*);
int	pcicfgr8(Pcidev*, int);
int	pcicfgr16(Pcidev*, int);
int	pcicfgr32(Pcidev*, int);
void	pcicfgw8(Pcidev*, int, int);
void	pcicfgw16(Pcidev*, int, int);
void	pcicfgw32(Pcidev*, int, int);
void	pciclrbme(Pcidev*);
void	pciclrioe(Pcidev*);
void	pciclrmwi(Pcidev*);
int	pcigetpms(Pcidev*);
void	pcihinv(Pcidev*);
uchar	pciipin(Pcidev*, uchar);
Pcidev* pcimatch(Pcidev*, int, int);
Pcidev* pcimatchtbdf(int);
void	pcireset(void);
int	pciscan(int, Pcidev**);
void	pcisetbme(Pcidev*);
void	pcisetioe(Pcidev*);
void	pcisetmwi(Pcidev*);
int	pcisetpms(Pcidev*, int);
int	cas32(void*, u32int, u32int);
int	tas32(void*);

#define CASU(p, e, n)	cas32((p), (u32int)(e), (u32int)(n))
#define CASV(p, e, n)	cas32((p), (u32int)(e), (u32int)(n))
#define CASW(addr, exp, new)	cas32((addr), (exp), (new))
#define TAS(addr)	tas32(addr)

void	sysrforkret(void);
int	userureg(Ureg*);
void*	vmap(uintptr, usize);
void	vunmap(void*, usize);

void	kexit(Ureg*);

#define	kmapinval()

#define PTR2UINT(p)	((uintptr)(p))
#define UINT2PTR(i)	((void*)(i))

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))

/*
 * this low-level printing stuff is ugly,
 * but there appears to be no other way to
 * print until after #t is populated.
 */
void	wave(int);

/*
 * These are not good enough.
 */
#define KADDR(pa)	UINT2PTR(KZERO|((uintptr)(pa)))
#define PADDR(va)	PTR2UINT(((uintptr)(va)) & ~KSEGM)

#define MASK(v)	((1UL << (v)) - 1)	/* mask `v' bits wide */
