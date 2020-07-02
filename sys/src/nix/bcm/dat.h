/*
 * Time.
 *
 * HZ should divide 1000 evenly, ideally.
 * 100, 125, 200, 250 and 333 are okay.
 */
#define	HZ		100			/* clock frequency */
#define	MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define	TK2SEC(t)	((t)/HZ)		/* ticks to seconds */

enum {
	Mhz	= 1000 * 1000,
};

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
typedef struct PhysUart	PhysUart;
typedef struct PMMU	PMMU;
typedef struct Proc	Proc;
typedef u32int		PTE;
typedef struct Sys		Sys;
typedef struct Uart	Uart;
typedef struct Ureg	Ureg;
typedef uvlong		Tval;
typedef ulong		uintmem;		/* should be u32int */
typedef u32int		Mpl;

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
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	int	monitor;	/* flag */
	Confmem	mem[1];		/* physical memory */
	ulong	npage;		/* total physical pages of memory */
	usize	upages;		/* user page pool */
	uint	ialloc;		/* max interrupt time allocation in bytes */
	ulong	pipeqsize;	/* size in bytes of pipe queues */
	int	postdawn;	/* multiprogramming on */
	uint	nimage;		/* number of page cache image headers */
	ulong	hz;		/* processor cycle freq */
	ulong	mhz;
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
	int	npgsz;

	Proc*	proc;			/* current process */

	MMMU;
	int	mmuflush;		/* flush current proc mmu state */

	ulong	ticks;			/* of the clock since boot time */
	Label	sched;			/* scheduler wakeup */
	Lock	alarmlock;		/* access to alarm list */
	void*	alarm;			/* alarms bound to this clock */

	Proc*	readied;		/* for runproc */
	ulong	schedticks;		/* next forced context switch */

	int	cputype;
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
	uvlong	inidle;			/* time spent in idlehands() */
	ulong	spuriousintr;
	int	lastintr;
	int	ilockdepth;
	Perf	perf;			/* performance counters */


	int	cpumhz;
	uvlong	cpuhz;			/* speed of cpu */
	uvlong	cyclefreq;		/* Frequency of user readable cycle counter */

	Sched*	sch;			/* scheduler used */

	/* save areas for exceptions, hold R0-R4 */
	u32int	sfiq[5];
	u32int	sirq[5];
	u32int	sund[5];
	u32int	sabt[5];
	u32int	smon[5];		/* probably not needed */
	u32int	ssys[5];

	int	stack[1];
};

struct Sys
{
	ulong	ticks;
	Mach	**machptr;		/* booge; make port happy */
	uintptr	vmend;
	uintmem	ialloc;
	int	copymode;		/* 0 is copy on write, 1 is copy on reference */
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
	int	machs;			/* bitmap of active CPUs */
	int	exiting;		/* shutdown */
	int	ispanic;		/* shutdown in response to a panic */
}active;

extern register Mach* m;			/* R10 */
extern register Proc* up;			/* R9 */
extern uintptr kseg0;
extern Mach* machaddr[MACHMAX];
extern ulong memsize;
extern int normalprint;
extern	Sys	*sys;

struct Hwconf {
	char	*type;
	int	irq;
};

#define	MACHP(n)	(machaddr[n])

/*
 * Horrid. But the alternative is 'defined'.
 */
#ifdef _DBGC_
#define DBGFLG		(dbgflg[_DBGC_])
#else
#define DBGFLG		(0)
#endif /* _DBGC_ */

extern char dbgflg[256];

#define DBG(...)	if(!DBGFLG){}else print(__VA_ARGS__)
