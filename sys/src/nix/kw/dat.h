typedef struct Conf	Conf;
typedef struct Confmem	Confmem;
typedef struct PFPU	PFPU;
typedef struct Hwconf	Hwconf;
typedef struct Label	Label;
typedef struct Lock	Lock;
typedef struct Memcache	Memcache;
typedef struct MMMU	MMMU;
typedef struct Mach	Mach;
typedef struct PNOTIFY	PNOTIFY;
typedef struct Page	Page;
typedef struct Pcidev	Pcidev;
typedef struct PhysUart	PhysUart;
typedef struct PMMU	PMMU;
typedef struct Proc	Proc;
typedef u32int		PTE;
typedef struct Soc	Soc;
typedef struct Sys		Sys;
typedef struct Uart	Uart;
typedef struct Ureg	Ureg;
typedef uvlong		Tval;
typedef ulong		uintmem;		/* should be u32int */
typedef u32int		Mpl;

#pragma incomplete Pcidev
#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, mpt, flag, arg, srv) */

/*
 *  parameters for sysproc.c
 */
#define AOUT_MAGIC	(E_MAGIC)

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

/*
 * emulated floating point
 */
struct PFPU
{
	ulong	status;
	ulong	control;
	ulong	regs[8][3];

	int	fpstate;
};

/*
 * PFPU.status
 */
enum
{
	FPinit,
	FPactive,
	FPinactive,

	/* bit or'd with the state */
	FPillegal= 0x100,
};

struct Confmem
{
	uintptr	base;
	usize	npage;
	uintptr	limit;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	uint	nmach;		/* processors */
	uint	nproc;		/* processes */
	uint	nimage;		/* number of page cache image headers */
};

/*
 *  things saved in the Proc structure during a notify
 */
struct PNOTIFY
{
	int	emptiness;
};


/*
 *  MMU stuff in Mach.
 */
struct MMMU
{
	PTE*	mmul1;		/* l1 for this processor */
	int	mmul1lo;
	int	mmul1hi;
	int	mmupid;
};

/*
 *  MMU stuff in proc
 */
#define NCOLOR	1		/* 1 level cache, don't worry about VCE's */
struct PMMU
{
	Page*	mmul2;
	Page*	mmul2cache;	/* free mmu pages */
};

enum
{
	NPGSZ = 1	/* # of supported  pages sizes in Mach */
};

#include "../port/portdat.h"

struct Mach
{
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc of last caller to splhi */

	int	online;
	int	pgsz[1];
	int	pgszlg2[1];
	int	pgszmask[1];
	int	npgsz;

	Proc*	proc;			/* current process */

	MMMU;
	int	flushmmu;		/* flush current proc mmu state */

	ulong	ticks;			/* of the clock since boot time */
	Label	sched;			/* scheduler wakeup */
	Lock	alarmlock;		/* access to alarm list */
	void*	alarm;			/* alarms bound to this clock */
	int	inclockintr;

	Proc*	readied;		/* for runproc */
	ulong	schedticks;		/* next forced context switch */

	int	cputype;
	int	socrev;			/* system-on-chip revision */
	ulong	delayloop;

	/* stats */
	int	tlbfault;
	int	tlbpurge;
	int	pfault;
	int	cs;
	int	syscall;
	int	load;
	int	intr;
	uvlong	fastclock;		/* last sampled value */
	int	spuriousintr;
	int	mmuflush;		/* make current proc flush it's mmu state */
	int	ilockdepth;
	Perf	perf;			/* performance counters */

	int	inidle;			/* profiling */
	int	lastintr;

	int	cpumhz;
	uvlong	cpuhz;			/* speed of cpu */
	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */

	Sched*	sch;			/* scheduler used */

	/* save areas for exceptions, hold R0-R4 */
	u32int	sfiq[5];
	u32int	sirq[5];
	u32int	sund[5];
	u32int	sabt[5];
#define fiqstack sfiq
#define irqstack sirq
#define abtstack sabt
#define undstack sund

	int	stack[1];
};

struct Sys
{
	ulong	ticks;
	Mach	**machptr;		/* booge; make port happy */
	uintptr	vmend;
	uintmem	ialloc;
	int	copymode;		/* 0 is copy on write, 1 is copy on reference */
	uintmem	pmstart;
	uintmem	pmend;
};

/*
 * Fake kmap.
 */
typedef void		KMap;
#define	VA(k)		((uintptr)(k))
#define	kmap(p)		(KMap*)((p)->pa|kseg0)
#define	kunmap(k)

struct
{
	Lock;
	int	nonline;			/* # of active CPUs */
	int	exiting;			/* shutdown */
	int	ispanic;			/* shutdown in response to a panic */
}active;

enum {
	Frequency	= 1200*1000*1000,	/* the processor clock */
};

extern register Mach* m;			/* R10 */
extern register Proc* up;			/* R9 */

extern uintptr kseg0;
extern Mach* machaddr[MACHMAX];
extern	Sys	*sys;

enum {
	Nvec = 8,	/* # of vectors at start of lexception.s */
};

/*
 * Layout of physical 0.
 */
typedef struct Vectorpage {
	void	(*vectors[Nvec])(void);
	uint	vtable[Nvec];
} Vectorpage;

/*
 *  a parsed plan9.ini line
 */
#define NHWOPT		8

struct Hwconf {
	char	*type;
	int	irq;
	uintmem	mem;
	int	tbdf;

	int	nopt;
	char	*opt[NHWOPT];
};

#define	MACHP(n)	(machaddr[n])

enum {
	Dcache,
	Icache,
	Unified,
};

/* characteristics of a given cache level */
struct Memcache {
	uint	level;		/* 1 is nearest processor, 2 further away */
	uint	kind;		/* I, D or unified */

	uint	size;
	uint	nways;		/* associativity */
	uint	nsets;
	uint	linelen;	/* bytes per cache line */
	uint	setsways;

	uint	log2linelen;
	uint	waysh;		/* shifts for set/way register */
	uint	setsh;
};

struct Soc {			/* addr's of SoC controllers */
	uintmem	cpu;
	uintmem	devid;
	uintmem	l2cache;
	uintmem	sdramc;
//	uintmem	sdramd;		/* unused */

	uintmem	iocfg;
	uintmem addrmap;
	uintmem	intr;
	uintmem	nand;
	uintmem	cesa;		/* crypto accel. */
	uintmem	ehci;
	uintmem spi;
	uintmem	twsi;

	uintmem	analog;
	uintmem	pci;
	uintmem	pcibase;

	uintmem	rtc;		/* real-time clock */
	uintmem	clock;

	uintmem	ether[2];
	uintmem	etherirq[2];
	uintmem	sata[3];
	uintmem	uart[2];
	uintmem	gpio[2];

	struct {
		uintmem	base;
		uintmem	end;
	}physram[4];
} soc;
extern Soc soc;

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
