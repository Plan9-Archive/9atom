#include "../port/portfns.h"

extern void sysrforkret(void);
Dirtab*	addarchfile(char*, int, long(*)(Chan*,void*,long,vlong), long(*)(Chan*,void*,long,vlong));
extern void archreboot(void);
extern void armtimerset(int);
extern void cachedwbinv(void);
extern void cachedwbse(void*, int);
extern void cachedwbinvse(void*, int);
extern void cacheiinv(void);
extern void cacheuwbinv(void);
extern uintptr cankaddr(uintptr pa);
extern int cas32(void*, u32int, u32int);
#define CASW(p, e, n)	cas32((p), (e), (n))
extern void checkmmu(uintptr, uintptr);
extern void clockinit(void);
extern int cmpswap(long*, long, long);
extern void coherence(void);
extern void cpuidprint(void);
#define cycles(ip) *(ip) = lcycles()
extern void dmastart(int, int, int, void*, void*, int);
extern int dmawait(int);
extern int fbblank(int);
extern void* fbinit(int, int*, int*, int*);
extern u32int farget(void);
extern u32int fsrget(void);
extern ulong getclkrate(int);
extern char* getconf(char*);
extern char *getethermac(void);
extern uint getfirmware(void);
extern int getpower(int);
extern void getramsize(Confmem*);
extern void irqenable(int, void (*)(Ureg*, void*), void*);
#define intrenable(i, f, a, b, n) irqenable((i), (f), (a))
extern long lcycles(void);
extern void links(void);
extern void mmuinit(void);
extern void mmuinit1(void);
extern void mmuinvalidate(void);
extern void mmuinvalidateaddr(u32int);
extern uintptr mmukmap(uintptr, uintptr, usize);
extern void okay(int);
extern void procrestore(Proc *);
extern void procsave(Proc*);
extern void procsetup(Proc*);
extern void screeninit(void);
extern void setpower(int, int);
extern void setr13(int, u32int*);
extern int splfhi(void);
extern int splflo(void);
extern void swcursorinit(void);
extern void syscallfmt(int syscallno, va_list list);
extern void sysretfmt(int, va_list, Ar0*, uvlong, uvlong);
extern int tas32(void*);
#define TAS(x) tas32(x)
extern void touser(uintptr);
extern void trapinit(void);
extern void uartconsinit(void);
extern int userureg(Ureg*);
extern void vectors(void);
extern void vtable(void);

/*
 * floating point emulation
 *   armv6 has fp hardware but compiler doesn't support it yet
 */
extern int fpiarm(Ureg*);
extern int fpudevprocio(Proc*, void*, long, uintptr, int);
extern void fpuinit(void);
extern void fpunoted(void);
extern void fpunotify(Ureg*);
extern void fpuprocrestore(Proc*);
extern void fpuprocsave(Proc*);
extern void fpusysprocsetup(Proc*);
extern void fpusysrfork(Ureg*);
extern void fpusysrforkchild(Proc*, Proc*);
extern int fpuemu(Ureg*);
/*
 * Things called from port.
 */
extern void delay(int);				/* only scheddump() */
extern int islo(void);
extern void microdelay(int);			/* only edf.c */
extern void evenaddr(uintptr);
extern void idlehands(void);
#define hardhalt()	idlehands()
extern void setkernur(Ureg*, Proc*);		/* only devproc.c */
extern void* sysexecregs(uintptr, uint, uint);
extern void sysprocsetup(Proc*);

extern void kexit(Ureg*);

#define	getpgcolor(a)	0
#define	kmapinval()
#define countpagerefs(a, b)

#define PTR2UINT(p)	((uintptr)(p))
#define UINT2PTR(i)	((void*)(i))

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))

#define KADDR(pa)	UINT2PTR(KZERO    | ((uintptr)(pa) & ~KSEGM))
#define PADDR(va)	PTR2UINT(PHYSDRAM | ((uintptr)(va) & ~KSEGM))
#define DMAADDR(va)	PTR2UINT(PHYSIO   | ((uintptr)(va) & ~VIRTIO))
