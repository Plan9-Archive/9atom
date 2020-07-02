typedef struct Conf Conf;
typedef struct Fxsave Fxsave;
typedef struct Hwconf Hwconf;
typedef struct Label Label;
typedef struct Lock Lock;
typedef struct MFPU MFPU;
typedef struct MMMU MMMU;
typedef struct Mach Mach;
typedef u64int Mpl;
typedef struct Page Page;
typedef struct Pciconf Pciconf;
typedef struct Pcidev Pcidev;
typedef struct PFPU PFPU;
typedef struct PMMU PMMU;
typedef struct PNOTIFY PNOTIFY;
typedef u64int PTE;
typedef struct Proc Proc;
typedef struct Sys Sys;
typedef u64int uintmem;				/* Physical address (hideous) */
typedef struct Ureg Ureg;
typedef struct Vctl Vctl;

#pragma incomplete Ureg
#pragma incomplete Pcidev

#define MAXSYSARG	5	/* for mount(fd, afd, mpt, flag, arg) */

/*
 *  parameters for sysproc.c
 */
#define AOUT_MAGIC	(S_MAGIC)

/*
 *  machine dependent definitions used by ../port/portdat.h
 */
struct Lock
{
	u32int	key;
	int	isilock;
	Mpl	pl;
	uintptr	pc;
	Proc*	p;
	Mach*	m;
	uvlong	lockcycles;
};

struct Label
{
	uintptr	sp;
	uintptr	pc;
};

struct Fxsave {
	u16int	fcw;			/* x87 control word */
	u16int	fsw;			/* x87 status word */
	u8int	ftw;			/* x87 tag word */
	u8int	zero;			/* 0 */
	u16int	fop;			/* last x87 opcode */
	u64int	rip;			/* last x87 instruction pointer */
	u64int	rdp;			/* last x87 data pointer */
	u32int	mxcsr;			/* MMX control and status */
	u32int	mxcsrmask;		/* supported MMX feature bits */
	uchar	st[128];		/* shared 64-bit media and x87 regs */
	uchar	xmm[256];		/* 128-bit media regs */
	uchar	ign[96];		/* reserved, ignored */
};

/*
 *  FPU stuff in Proc
 */
struct PFPU {
	int	fpustate;
	uchar	fxsave[sizeof(Fxsave)+15];
	void*	fpusave;
};

/*
 *  MMU stuff in Proc
 */
struct PMMU
{
	Page*	mmuptp[4];		/* page table pages for each level */
};

/*
 *  things saved in the Proc structure during a notify
 */
struct PNOTIFY
{
	int	emptiness;
};

struct Conf
{
	uint	nmach;		/* processors */
	uint	nproc;		/* processes */
	uint	nimage;		/* number of page cache image headers */
};

enum
{
	NPGSZ = 4	/* # of supported  pages sizes in Mach */
};

#include "../port/portdat.h"

/*
 *  FPU stuff in Mach.
 */
struct MFPU
{
	u16int	fcw;			/* x87 control word */
	u32int	mxcsr;			/* MMX control and status */
	u32int	mxcsrmask;		/* supported MMX feature bits */
};

/*
 *  MMU stuff in Mach.
 */
struct MMMU
{
	Page*	pml4;			/* pml4 for this processor */
	PTE*	pmap;			/* unused as of yet */

	uint	pgszlg2[NPGSZ];		/* per Mach or per Sys? */
	uintmem	pgszmask[NPGSZ];
	uint	pgsz[NPGSZ];
	int	npgsz;

	Page	pml4kludge;		/* NIX KLUDGE: we need a page */
};

/*
 * Per processor information.
 *
 * The offsets of the first few elements may be known
 * to low-level assembly code, so do not re-order:
 *	machno	- no dependency, convention
 *	splpc	- splhi, spllo, splx
 *	proc	- syscallentry
 *	stack	- acsyscall
 */
struct Mach
{
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc of last caller to splhi */

	Proc*	proc;			/* current process on this processor */
	uintptr	stack;

	int	apicno;
	int	online;

	MMMU;

	uchar*	vsvm;
	void*	gdt;
	void*	tss;

	ulong	ticks;			/* of the clock since boot time */
	Label	sched;			/* scheduler wakeup */
	Lock	alarmlock;		/* access to alarm list */
	void*	alarm;			/* alarms bound to this clock */

	Proc*	readied;		/* for runproc */
	ulong	schedticks;		/* next forced context switch */

	int	tlbfault;
	int	tlbpurge;
	int	pfault;
	int	cs;
	int	syscall;
	int	load;
	int	intr;
	int	mmuflush;		/* make current proc flush it's mmu state */
	int	ilockdepth;
	Perf	perf;			/* performance counters */

	int	inidle;			/* profiling */
	int	lastintr;

	Lock	apictimerlock;
	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */
	uvlong	cpuhz;
	int	cpumhz;
	u64int	rdtsc;

	Sched*	sch;			/* scheduler used */

	MFPU;
};

/*
 * This is the low memory map, between 0x100000 and 0x110000.
 * It is located there to allow fundamental datastructures to be
 * created and used before knowing where free memory begins
 * (e.g. there may be modules located after the kernel BSS end).
 * The layout is known in the bootstrap code in l32p.s.
 * It is logically two parts: the per processor data structures
 * for the bootstrap processor (stack, Mach, vsvm, and page tables),
 * and the global information about the system (syspage, ptrpage).
 * Some of the elements must be aligned on page boundaries, hence
 * the unions.
 */
struct Sys {
	uchar	machstk[MACHSTKSZ];

	PTE	pml4[PTSZ/sizeof(PTE)];	/*  */
	PTE	pdp[PTSZ/sizeof(PTE)];
	PTE	pd[PTSZ/sizeof(PTE)];
	PTE	pt[PTSZ/sizeof(PTE)];

	uchar	vsvmpage[4*KiB];

	union {
		Mach	mach;
		uchar	machpage[MACHSZ];
	};

	union {
		struct {
			uintmem	pmstart;	/* physical memory */
			uintmem	pmend;		/* total span */

			uintptr	vmstart;	/* base address for malloc */
			uintptr	vmunused;	/* 1st unused va */
			uintptr	vmunmapped;	/* 1st unmapped va */
			uintptr	vmend;		/* 1st unusable va */
			uintmem	ialloc;		/* maximum bytes concurrently ialloc'd */
			uint	meminit;

			uint	copymode;	/* 0 is copy on write, 1 is copy on reference */

			uvlong	npages;		/* total physical pages of memory */
			uvlong	upages;		/* user page pool */
			uvlong	kpages;		/* kernel pages */

			u64int	epoch;		/* crude time synchronisation */
			ulong	ticks;			/* of the clock since boot time */
		};
		uchar	syspage[4*KiB];
	};

	union {
		Mach*	machptr[MACHMAX];
		uchar	ptrpage[4*KiB];
	};

	uchar	_57344_[2][4*KiB];		/* unused */
};

extern Sys* sys;

/*
 * KMap
 */
typedef void KMap;
extern KMap* kmap(Page*);

#define kunmap(k)
#define VA(k)		PTR2UINT(k)

struct
{
	Lock;
	int	nonline;			/* # of active CPUs */
	int	nbooting;			/* # of CPUs waiting for the bsp to go */
	int	exiting;			/* shutdown */
	int	ispanic;			/* shutdown in response to a panic */
	int	thunderbirdsarego;	/* lets the added processors continue */
}active;

/*
 *  pci device configuration
 */
struct Pciconf {
	char	*type;
	uintmem	port;
	uintmem	mem;

	int	irq;
	uint	tbdf;

	int	nopt;
	char	optbuf[128];
	char	*opt[8];
};

struct Hwconf {
	Pciconf;
};

/*
 * The Mach structures must be available via the per-processor
 * MMU information array machptr, mainly for disambiguation and access to
 * the clock which is only maintained by the bootstrap processor (0).
 */
#define MACHP(n)	(sys->machptr[n])

extern register Mach* m;			/* R15 */
extern register Proc* up;			/* R14 */

extern uintptr kseg0;

#pragma	varargck	type	"R"	u64int
#pragma	varargck	type	"W"	uintptr

/*
 * Horrid.
 */
#ifdef _DBGC_
#define DBGFLG		(dbgflg[_DBGC_])
#else
#define DBGFLG		(0)
#endif /* _DBGC_ */

#define DBG(...)	if(!DBGFLG){}else print(__VA_ARGS__)

extern char dbgflg[256];
