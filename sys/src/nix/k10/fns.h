#include "../port/portfns.h"
void	aamloop(int);
int	acpiinit(void);
Dirtab*	addarchfile(char*, int, long(*)(Chan*, void*, long, vlong), long(*)(Chan*, void*, long, vlong));
void	adrinit(void);
void	apicipi(int);
void	apicpri(int);
void	apmmuinit(void);
vlong	archhz(void);
void	archinit(void);
int	archmmu(void);
void	archreset(void);
void	cgaconsputs(char*, int);
void	cgainit(void);
void	cgapost(int);
void	checkpa(char*, uintmem);
#define	clearmmucache()				/* x86 doesn't have one */
#define coherence()	mfence()
void	confsetenv(void);
u32int	cpuid(u32int, u32int, u32int[4]);
int	dbgprint(char*, ...);
void	delay(int);
void	dumpmmu(Proc*);
void	dumpmmuwalk(uintmem);
void	dumpptepg(int, uintmem);
#define	evenaddr(x)				/* x86 doesn't care */
int	fpudevprocio(Proc*, void*, long, vlong, int);
void	fpuinit(void);
void	fpunoted(void);
void	fpunotify(Ureg*);
void	fpuprocrestore(Proc*);
void	fpuprocsave(Proc*);
void	fpusysprocsetup(Proc*);
void	fpusysrforkchild(Proc*, Proc*);
void	fpusysrfork(Ureg*);
char*	getconf(char*);
void	halt(void);
void	hardhalt(void);
int	i8042auxcmd(int);
int	i8042auxcmds(uchar*, int);
void	i8042auxenable(void (*)(int, int));
void	i8042reset(void);
void*	i8250alloc(int, int, int);
void	i8250console(void);
void	idlehands(void);
void	idthandlers(void);
int	inb(int);
u32int	inl(int);
void	insb(int, void*, int);
ushort	ins(int);
void	insl(int, void*, int);
void	inss(int, void*, int);
int	intrdisable(void*);
void*	intrenable(int, void (*)(Ureg*, void*), void*, int, char*);
void	invlpg(uintptr);
int	ioalloc(int, int, int, char*);
void	iofree(int);
void	ioinit(void);
int	ioreserve(int, int, int, char*);
int	iounused(int, int);
int	iprint(char*, ...);
int	islo(void);
void	kbdenable(void);
void	kbdinit(void);
void	kexit(Ureg*);
#define	kmapinval()
void	lfence(void);
void	links(void);
void	mach0init(void);
void	machinit(void);
void	meminit(void);
void	mfence(void);
void	mmuflushtlb(uintmem);
void	mmuinit(void);
uintmem	mmuphysaddr(uintptr);
int	mmuwalk(PTE*, uintptr, int, PTE**, uintmem (*)(usize));
void	ndnr(void);
void	noerrorsleft(void);
uchar	nvramread(int);
void	nvramwrite(int, uchar);
void	optionsinit(char*);
void	outb(int, int);
void	outl(int, u32int);
void	outsb(int, void*, int);
void	outs(int, ushort);
void	outsl(int, void*, int);
void	outss(int, void*, int);
int	pcicap(Pcidev*, int);
int	pcicfgr16(Pcidev*, int);
int	pcicfgr32(Pcidev*, int);
int	pcicfgr8(Pcidev*, int);
void	pcicfgw16(Pcidev*, int, int);
void	pcicfgw32(Pcidev*, int, int);
void	pcicfgw8(Pcidev*, int, int);
void	pciclrbme(Pcidev*);
void	pciclriome(Pcidev*);
void	pciclrmwi(Pcidev*);
int	pciconfig(char*, int, Pciconf*);
int	pcigetpms(Pcidev*);
void	pcihinv(Pcidev*);
Pcidev*	pcimatch(Pcidev*, int, int);
Pcidev*	pcimatchtbdf(int);
void	pcireset(void);
void	pcisetbme(Pcidev*);
void	pcisetioe(Pcidev*);
void	pcisetmwi(Pcidev*);
int	pcisetpms(Pcidev*, int);
void	physallocdump(void);
void	printcpufreq(void);
int	screenprint(char*, ...);			/* debugging */
void	sfence(void);
int	strtotbdf(char*, char**, int);
void	syscall(uint, Ureg*);
void	trapenable(int, void (*)(Ureg*, void*), void*, char*);
void	trapinit(void);
void	trap(Ureg*);
void	tssrsp0(u64int);
void	umeminit(void);
int	userureg(Ureg*);
void*	vmap(uintmem, usize);
void*	vmappat(uintmem, usize, uint);
int	vmapsync(uintptr);
void	vsvminit(int);
void	vunmap(void*, usize);

u64int	cr0get(void);
void	cr0put(u64int);
u64int	cr2get(void);
u64int	cr3get(void);
void	cr3put(u64int);
u64int	cr4get(void);
void	cr4put(u64int);
void	gdtget(void*);
void	gdtput(int, u64int, u16int);
void	idtput(int, u64int);
u64int	rdmsr(u32int);
void	rdrandbuf(void*, usize);
u64int	rdtsc(void);
void	trput(u64int);
void	wrmsr(u32int, u64int);

int	cas32(void*, u32int, u32int);
int	cas64(void*, u64int, u64int);
int	tas32(void*);
u64int	fas64(u64int*, u64int);

#define CASU(p, e, n)	cas64((p), (u64int)(e), (u64int)(n))
#define CASV(p, e, n)	cas64((p), (u64int)(e), (u64int)(n))
#define CASP(p, e, n)	cas64((p), (u64int)(e), (u64int)(n))
#define CASW(p, e, n)	cas32((p), (e), (n))
#define TAS(addr)	tas32((addr))
#define	FASP(p, v)	((void*)fas64((u64int*)(p), (u64int)(v)))

void	touser(uintptr);
void	syscallentry(void);
void	syscallreturn(void);
void	sysrforkret(void);

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))

#define PTR2UINT(p)	((uintptr)(p))
#define UINT2PTR(i)	((void*)(i))

void*	KADDR(uintmem);
uintmem	PADDR(void*);

#define BIOSSEG(a)	KADDR(((uint)(a))<<4)

/*
 * (l|io)apic.c
 */
int	lapiceoi(int);
void	lapicipi(int);
void	lapicinit(int, uintmem, int);
int	lapicisr(int);
int	lapiconline(void);
void	lapicpri(int);
void	lapicsipi(int, uintmem);

void	ioapicinit(int, int, uintmem);
void	ioapicintrinit(int, int, int, int, u32int);
void	ioapiconline(void);

/*
 * archk10.c
 */
void k10mwait(void*);

/*
 * i8259.c
 */
int	i8259init(int);
int	i8259isr(int);

/*
 * mp.c
 */
void	mpsinit(int);
int	mpacpi(int);

/*
 * sipi.c
 */
void	sipi(void);
