typedef struct BIOS32si	BIOS32si;
typedef struct BIOS32ci	BIOS32ci;
typedef struct Conf	Conf;
typedef struct Confmem	Confmem;
typedef struct Cpuidreg	Cpuidreg;
typedef struct FPsave	FPsave;
typedef struct ISAConf	ISAConf;
typedef struct Label	Label;
typedef struct Lock	Lock;
typedef struct MMU	MMU;
typedef struct Mach	Mach;
typedef struct Notsave	Notsave;
typedef struct PCArch	PCArch;
typedef struct Pcidev	Pcidev;
typedef struct PCMmap	PCMmap;
typedef struct PCMslot	PCMslot;
typedef struct Page	Page;
typedef struct PMMU	PMMU;
typedef struct Proc	Proc;
typedef struct Segdesc	Segdesc;
typedef uvlong		Tval;
typedef struct Ureg	Ureg;
typedef struct Vctl	Vctl;
typedef ulong uintmem;
typedef ulong PTE;

#pragma incomplete BIOS32si
#pragma incomplete Pcidev
#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, afd, mpt, flag, arg) */

/*
 *  parameters for sysproc.c
 */
#define AOUT_MAGIC	(I_MAGIC)

struct Lock
{
	ulong	key;
	ulong	sr;
	ulong	pc;
	Proc	*p;
	Mach	*m;
	ushort	isilock;
	long	lockcycles;
};

struct Label
{
	ulong	sp;
	ulong	pc;
};


/*
 * FPsave.status
 */
enum
{
	/* this is a state */
	FPinit=		0,
	FPactive=	1,
	FPinactive=	2,

	/* the following is a bit that can be or'd into the state */
	FPillegal=	0x100,
};

struct	FPsave
{
	ushort	control;
	ushort	r1;
	ushort	status;
	ushort	r2;
	ushort	tag;
	ushort	r3;
	ulong	pc;
	ushort	selector;
	ushort	r4;
	ulong	operand;
	ushort	oselector;
	ushort	r5;
	uchar	regs[80];	/* floating point registers */
};

struct Confmem
{
	uintmem	base;
	ulong	npage;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	ulong	monitor;	/* has monitor? */
	Confmem	mem[8];		/* physical memory */
	ulong	npage;		/* total physical pages of memory */
	ulong	upages;		/* user page pool */
	ulong	nimage;		/* number of page cache image headers */
	ulong	nswap;		/* number of swap pages */
	int	nswppo;		/* max # of pageouts per segment pass */
	ulong	copymode;	/* 0 is copy on write, 1 is copy on reference */
	uint	ialloc;		/* max interrupt time allocation in bytes */
	ulong	pipeqsize;	/* size in bytes of pipe queues */
	int	postdawn;	/* multiprogramming on */
	int	nuart;		/* number of uart devices */
	int	nokbd;		/* no 8042-based (64/60) keyboard? */
	int	nolegacy;	/* don't poke around randomly for i/o ports */
	int	apicmode;	/* force apicmode */
};

/*
 *  MMU stuff in proc
 */
#define NCOLOR 1
struct PMMU
{
	Page*	mmupdb;			/* page directory base */
	Page*	mmufree;		/* unused page table pages */
	Page*	mmuused;		/* used page table pages */
	Page*	kmaptable;		/* page table used by kmap */
	uint	lastkmap;		/* last entry used by kmap */
	int	nkmap;			/* number of current kmaps */
};

/*
 *  things saved in the Proc structure during a notify
 */
struct Notsave
{
	ulong	svflags;
	ulong	svcs;
	ulong	svss;
};

#include "../port/portdat.h"

typedef struct {
	ulong	link;			/* link (old TSS selector) */
	ulong	esp0;			/* privilege level 0 stack pointer */
	ulong	ss0;			/* privilege level 0 stack selector */
	ulong	esp1;			/* privilege level 1 stack pointer */
	ulong	ss1;			/* privilege level 1 stack selector */
	ulong	esp2;			/* privilege level 2 stack pointer */
	ulong	ss2;			/* privilege level 2 stack selector */
	ulong	xcr3;			/* page directory base register - not used because we don't use trap gates */
	ulong	eip;			/* instruction pointer */
	ulong	eflags;			/* flags register */
	ulong	eax;			/* general registers */
	ulong 	ecx;
	ulong	edx;
	ulong	ebx;
	ulong	esp;
	ulong	ebp;
	ulong	esi;
	ulong	edi;
	ulong	es;			/* segment selectors */
	ulong	cs;
	ulong	ss;
	ulong	ds;
	ulong	fs;
	ulong	gs;
	ulong	ldt;			/* selector for task's LDT */
	ulong	iomap;			/* I/O map base address + T-bit */
} Tss;

struct Segdesc
{
	ulong	d0;
	ulong	d1;
};

struct Mach
{
	int	machno;			/* physical id of processor (KNOWN TO ASSEMBLY) */
	ulong	splpc;			/* pc of last caller to splhi */

	PTE*	pdb;			/* kernel region page directory base for this processor (va) */
	Tss*	tss;			/* tss for this processor */
	Segdesc	*gdt;			/* gdt for this processor */

	Proc*	proc;			/* current process on this processor */
	Proc*	externup;		/* extern register Proc *up */

	Page*	pdbpool;
	int	pdbcnt;

	ulong	ticks;			/* of the clock since boot time */
	Label	sched;			/* scheduler wakeup */
	Lock	alarmlock;		/* access to alarm list */
	void*	alarm;			/* alarms bound to this clock */
	int	inclockintr;

	Proc*	readied;		/* for runproc */
	ulong	schedticks;		/* next forced context switch */

	int	tlbfault;
	int	tlbpurge;
	int	pfault;
	int	cs;
	int	syscall;
	int	load;
	int	intr;
	int	flushmmu;		/* make current proc flush it's mmu state */
	int	ilockdepth;
	Perf	perf;			/* performance counters */

	ulong	spuriousintr;
	int	lastintr;

	int	loopconst;

	Lock	apictimerlock;
	int	cpumhz;
	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */
	uvlong	cpuhz;
	int	cpuidax;
	int	cpuiddx;
	char	cpuidid[16];
	char*	cpuidtype;
	int	havetsc;
	int	havepge;
	uvlong	tscticks;
	int	pdballoc;
	int	pdbfree;

	vlong	mtrrcap;
	vlong	mtrrdef;
	vlong	mtrrfix[11];
	vlong	mtrrvar[32];		/* 256 max. */

	int	stack[1];
};

/*
 * KMap the structure doesn't exist, but the functions do.
 */
typedef struct KMap		KMap;
#define	VA(k)		((void*)(k))
KMap*	kmap(Page*);
void	kunmap(KMap*);

struct
{
	Lock;
	int	machs;			/* bitmap of active CPUs */
	int	exiting;		/* shutdown */
	int	ispanic;		/* shutdown in response to a panic */
	int	thunderbirdsarego;	/* lets the added processors continue to schedinit */
}active;

/*
 *  routines for things outside the PC model, like power management
 */
struct PCArch
{
	char*	id;
	int	(*ident)(void);		/* this should be in the model */
	void	(*reset)(void);		/* this should be in the model */
	int	(*serialpower)(int);	/* 1 == on, 0 == off */
	int	(*modempower)(int);	/* 1 == on, 0 == off */

	void	(*intrinit)(void);
	int	(*intrenable)(Vctl*);
	int	(*intrdisable)(Vctl*);
	void	(*introff)(void);
	void	(*intron)(void);
	void	(*intrreset)(void);

	void	(*clockenable)(void);
	uvlong	(*fastclock)(uvlong*);
	void	(*timerset)(uvlong);
};

struct Cpuidreg {
	ulong	ax;
	ulong	bx;
	ulong	cx;
	ulong	dx;
};

/* cpuid instruction result register bits */
enum {
	/* dx */
	Fpuonchip = 1<<0,
	Vmex	= 1<<1,		/* virtual-mode extensions */
	Pse	= 1<<3,		/* page size extensions */
	Tsc	= 1<<4,		/* time-stamp counter */
	Cpumsr	= 1<<5,		/* model-specific registers, rdmsr/wrmsr */
	Pae	= 1<<6,		/* physical-addr extensions */
	Mce	= 1<<7,		/* machine-check exception */
	Cmpxchg8b = 1<<8,
	Cpuapic	= 1<<9,
	Mtrr	= 1<<12,	/* memory-type range regs.  */
	Pge	= 1<<13,	/* page global extension */
	Pat	= 1<<16,
	Pse2	= 1<<17,	/* more page size extensions */
	Clflush	= 1<<19,
	Acpif	= 1<<22,
	Mmx	= 1<<23,
	Fxsr	= 1<<24,	/* have SSE FXSAVE/FXRSTOR */
	Sse	= 1<<25,	/* thus sfence instr. */
	Sse2	= 1<<26,	/* thus mfence & lfence instr.s */
};

/*
 *  a parsed plan9.ini line
 */
#define NISAOPT		8

struct ISAConf {
	char	*type;
	ulong	port;
	int	irq;
	ulong	dma;
	ulong	mem;
	ulong	size;
	ulong	freq;
	uint	tbdf;

	int	nopt;
	char	*opt[NISAOPT];
	char	buf[80];
};

extern PCArch	*arch;			/* PC architecture */

/*
 * Each processor sees its own Mach structure at address MACHADDR.
 * However, the Mach structures must also be available via the per-processor
 * MMU information array machp, mainly for disambiguation and access to
 * the clock which is only maintained by the bootstrap processor (0).
 */
Mach* machp[MAXMACH];
	
#define	MACHP(n)	(machp[n])

extern Mach	*m;
#define up	(((Mach*)MACHADDR)->externup)

/*
 *  hardware info about a device
 */
typedef struct {
	ulong	port;	
	int	size;
} Devport;

struct DevConf
{
	ulong	intnum;			/* interrupt number */
	char	*type;			/* card type, malloced */
	int	nports;			/* Number of ports */
	Devport	*ports;			/* The ports themselves */
};

typedef struct BIOS32ci {		/* BIOS32 Calling Interface */
	u32int	eax;
	u32int	ebx;
	u32int	ecx;
	u32int	edx;
	u32int	esi;
	u32int	edi;
} BIOS32ci;
