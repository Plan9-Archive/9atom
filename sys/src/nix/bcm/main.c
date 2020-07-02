#include "u.h"
#include "tos.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "init.h"

/* Firmware compatibility */
#define	Minfirmrev	326770
#define	Minfirmdate	"22 Jul 2012"

/*
 * Where configuration info is left for the loaded programme.
 */
#define BOOTARGS	((char*)CONFADDR)
#define	BOOTARGSLEN	(MACHADDR-CONFADDR)
#define	MAXCONF		64
#define MAXCONFLINE	160

uintptr kseg0 = KZERO;
Mach*	machaddr[MACHMAX];
Sys	thesys;
Sys	*sys = &thesys;
Conf	conf;
ulong	memsize = 128*1024*1024;
usize	segpgsizes = (1<<PGSHIFT);	/* only page size available */

/*
 * Option arguments from the command line.
 * oargv[0] is the boot file.
 */
static int oargc;
static char* oargv[20];
static char oargb[128];
static int oargblen;

static uintptr sp;		/* XXX - must go - user stack of init proc */

/* store plan9.ini contents here at least until we stash them in #ec */
static char confname[MAXCONF][KNAMELEN];
static char confval[MAXCONF][MAXCONFLINE];
static int nconf;

typedef struct Atag Atag;
struct Atag {
	u32int	size;	/* size of atag in words, including this header */
	u32int	tag;	/* atag type */
	union {
		u32int	data[1];	/* actually [size-2] */
		/* AtagMem */
		struct {
			u32int	size;
			u32int	base;
		} mem;
		/* AtagCmdLine */
		char	cmdline[1];	/* actually [4*(size-2)] */
	};
};

enum {
	AtagNone	= 0x00000000,
	AtagCore	= 0x54410001,
	AtagMem		= 0x54410002,
	AtagCmdline	= 0x54410009,
};

static int
findconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return i;
	return -1;
}

char*
getconf(char *name)
{
	int i;

	i = findconf(name);
	if(i >= 0)
		return confval[i];
	return nil;
}

void
addconf(char *name, char *val)
{
	int i;

	i = findconf(name);
	if(i < 0){
		if(val == nil || nconf >= MAXCONF)
			return;
		i = nconf++;
		strecpy(confname[i], confname[i]+sizeof(confname[i]), name);
	}
//	confval[i] = val;
	strecpy(confval[i], confval[i]+sizeof(confval[i]), val);
}

static void
writeconf(void)
{
	char *p, *q;
	int n;

	p = getconfenv();

	if(waserror()) {
		free(p);
		nexterror();
	}

	/* convert to name=value\n format */
	for(q=p; *q; q++) {
		q += strlen(q);
		*q = '=';
		q += strlen(q);
		*q = '\n';
	}
	n = q - p + 1;
	if(n >= BOOTARGSLEN)
		error("kernel configuration too large");
	memmove(BOOTARGS, p, n);
	memset(BOOTARGS + n, '\n', BOOTARGSLEN - n);
	poperror();
	free(p);
}

static void
plan9iniinit(char *s, int cmdline)
{
	char *toks[MAXCONF];
	int i, c, n;
	char *v;

	if((c = *s) < ' ' || c >= 0x80)
		return;
	if(cmdline)
		n = tokenize(s, toks, MAXCONF);
	else
		n = getfields(s, toks, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(toks[i][0] == '#')
			continue;
		v = strchr(toks[i], '=');
		if(v == nil)
			continue;
		*v++ = '\0';
		addconf(toks[i], v);
	}
}

static void
ataginit(Atag *a)
{
	int n;

	if(a->tag != AtagCore){
		plan9iniinit((char*)a, 0);
		return;
	}
	while(a->tag != AtagNone){
		switch(a->tag){
		case AtagMem:
			/* use only first bank */
			if(conf.mem[0].limit == 0 && a->mem.size != 0){
				memsize = a->mem.size;
				conf.mem[0].base = a->mem.base;
				conf.mem[0].limit = a->mem.base + memsize;
			}
			break;
		case AtagCmdline:
			n = (a->size * sizeof(u32int)) - offsetof(Atag, cmdline[0]);
			if(a->cmdline + n < BOOTARGS + BOOTARGSLEN)
				a->cmdline[n] = 0;
			else
				BOOTARGS[BOOTARGSLEN-1] = 0;
			plan9iniinit(a->cmdline, 1);
			break;
		}
		a = (Atag*)((u32int*)a + a->size);
	}
}

static void
sysinit(void)
{
	sys = &thesys;
	sys->machptr = machaddr;
	sys->copymode = 0;		/* copy on write */
}

void
machinit(void)
{
	m->machno = 0;
	machaddr[m->machno] = m;

	m->ticks = 1;
	m->perf.period = 1;
	m->online = 1;
	m->pgsz[0] = 1<<PGSHIFT;
	m->pgszlg2[0] = PGSHIFT;
	m->npgsz = 1;

	conf.nmach = 1;

	active.machs = 1;
	active.exiting = 0;

	up = nil;
}

static void
optionsinit(char* s)
{
	strecpy(oargb, oargb+sizeof(oargb), s);

	oargblen = strlen(oargb);
	oargc = tokenize(oargb, oargv, nelem(oargv)-1);
	oargv[oargc] = nil;
}

/* don't link in libdraw/fmt.c */
#define	Image	IMAGE
#include <draw.h>
int
Rfmt(Fmt *f)
{
	Rectangle r;

	r = va_arg(f->args, Rectangle);
	return fmtprint(f, "[%d %d] [%d %d]", r.min.x, r.min.y, r.max.x, r.max.y);
}

/* wrong name, and not static to defeat libdraw's install of %P */
int
Pfmt(Fmt *f)
{
	uintmem pa;

	pa = va_arg(f->args, uintmem);
	if(f->flags & FmtSharp)
		return fmtprint(f, "%#8.8lux", (ulong)pa);
	return fmtprint(f, "%lud", (ulong)pa);
}

void
main(void)
{
	extern char edata[], end[];
	uint rev;

	okay(1);
	m = (Mach*)MACHADDR;
	memset(edata, 0, end - edata);	/* clear bss */
	sysinit();
	machinit();
	mmuinit1();

	optionsinit("/boot/boot boot");
	quotefmtinstall();
	fmtinstall('P', Pfmt);
	
	ataginit((Atag*)BOOTARGS);
	confinit();		/* figures out amount of memory */
	mallocinit();
	uartconsinit();
	screeninit();

	fmtinit();
	print("\nnix\n");
	rev = getfirmware();
	print("Firmware: rev %d\n", rev);
	if(rev < Minfirmrev){
		print("Sorry, firmware (start.elf) must be at least rev %d (%s)\n",
			Minfirmrev, Minfirmdate);
		for(;;){}
	}
	trapinit();
	clockinit();
	printinit();
	timersinit();
	if(conf.monitor)
		swcursorinit();
	cpuidprint();

	psinit(conf.nproc);
//	initseg();
	initimage();
	links();
	devtabreset();
	pageinit();
	swapinit();
	userinit();
	schedinit();
	assert(0);		/* shouldn't have returned */
}

/*
 *  starting place for first process
 */
void
init0(void)
{
	int i;
	char buf[2*KNAMELEN];

	up->nerrlab = 0;
	coherence();
	spllo();

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);

	devtabinit();

	if(!waserror()){
		snprint(buf, sizeof(buf), "%s %s", "ARM", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "arm", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		snprint(buf, sizeof(buf), "-a %s", getethermac());
		ksetenv("etherargs", buf, 0);

		/* convert plan9.ini variables to #e and #ec */
		for(i = 0; i < nconf; i++) {
			ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	touser(sp);
	assert(0);	/* shouldn't have returned */
}

static void
bootargs(uintptr base)
{
	int i;
	ulong ssize;
	char **av, *p;

	/*
	 * Push the boot args onto the stack.
	 * The initial value of the user stack must be such
	 * that the total used is larger than the maximum size
	 * of the argument list checked in syscall.
	 */
	i = oargblen+1;
	p = UINT2PTR(STACKALIGN(base + PGSZ - sizeof(Tos) - i));
	memmove(p, oargb, i);

	/*
	 * Now push the argv pointers.
	 * The code jumped to by touser in lproc.s expects arguments
	 *	main(char* argv0, ...)
	 * and calls
	 * 	startboot("/boot/boot", &argv0)
	 * not the usual (int argc, char* argv[])
	 */
	av = (char**)(p - (oargc+1)*sizeof(char*));
	ssize = base + PGSZ - PTR2UINT(av);
	for(i = 0; i < oargc; i++)
		*av++ = (oargv[i] - oargb) + (p - base) + (USTKTOP - PGSZ);
	*av = nil;
	sp = USTKTOP - ssize;
}

/*
 *  create the first process
 */
void
userinit(void)
{
	Proc *p;
	Segment *s;
	KMap *k;
	Page *pg;

	/* no processes yet */
	up = nil;

	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);

	/*
	 * Kernel Stack
	 */
	p->sched.pc = PTR2UINT(init0);
	p->sched.sp = PTR2UINT(p->kstack+KSTACK-sizeof(up->arg)-sizeof(uintptr));
	p->sched.sp = STACKALIGN(p->sched.sp);

	/*
	 * User Stack
	 *
	 * Technically, newpage can't be called here because it
	 * should only be called when in a user context as it may
	 * try to sleep if there are no pages available, but that
	 * shouldn't be the case here.
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKTOP);
	s->flushme++;
	p->seg[SSEG] = s;
//	pg = newpage(1, 0, USTKTOP-(1<<s->lgpgsize), s->lgpgsize);
	pg = newpage(1, 0, USTKTOP-m->pgsz[s->pgszi], m->pgsz[s->pgszi], -1);
	segpage(s, pg);
	k = kmap(pg);
	bootargs(VA(k));
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, UTZERO+PGSZ);
	p->seg[TSEG] = s;
//	pg = newpage(1, 0, UTZERO, s->lgpgsize);
	pg = newpage(1, 0, UTZERO, m->pgsz[s->pgszi], -1);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memmove(UINT2PTR(VA(k)), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

void
confinit(void)
{
	int i;
//	ulong kpages;
	uintptr pa;
	char *p;

	if((p = getconf("*maxmem")) != nil) {
		memsize = strtoul(p, 0, 0) - PHYSDRAM;
		if (memsize < 16*MB)		/* sanity */
			memsize = 16*MB;
	}

	getramsize(&conf.mem[0]);
	if(conf.mem[0].limit == 0){
		conf.mem[0].base = PHYSDRAM;
		conf.mem[0].limit = PHYSDRAM + memsize;
	}else if(p != nil)
		conf.mem[0].limit = conf.mem[0].base + memsize;

	conf.npage = 0;
	pa = PADDR(ROUNDUP(PTR2UINT(end), 1<<PGSHIFT));

	/*
	 *  we assume that the kernel is at the beginning of one of the
	 *  contiguous chunks of memory and fits therein.
	 */
	for(i=0; i<nelem(conf.mem); i++){
		/* take kernel out of allocatable space */
		if(pa > conf.mem[i].base && pa < conf.mem[i].limit)
			conf.mem[i].base = pa;

		conf.mem[i].npage = (conf.mem[i].limit - conf.mem[i].base)/PGSZ;
		conf.npage += conf.mem[i].npage;
	}

	conf.upages = (conf.npage*80)/100;
	conf.ialloc = ((conf.npage-conf.upages)/2)*PGSZ;

	/* only one processor */
	conf.nmach = 1;

	/* set up other configuration parameters */
	conf.nproc = 100 + ((conf.npage*PGSZ)/MB)*5;
	if(cpuserver)
		conf.nproc *= 3;
	if(conf.nproc > 2000)
		conf.nproc = 2000;
	conf.nimage = 200;

	/*
	 * Guess how much is taken by the large permanent
	 * datastructures. Mntcache and Mntrpc are not accounted for
	 * (probably ~300KB).
	 */
//	kpages = conf.npage - conf.upages;
//	kpages *= PGSZ;
//	kpages -= conf.upages*sizeof(Page)
//		+ conf.nproc*sizeof(Proc)
//		+ conf.nimage*sizeof(Image)
//		+ conf.nswap
//		+ conf.nswppo*sizeof(Page);
//	mainmem->maxsize = kpages;
//	if(!cpuserver)
//		/*
//		 * give terminals lots of image memory, too; the dynamic
//		 * allocation will balance the load properly, hopefully.
//		 * be careful with 32-bit overflow.
//		 */
//		imagmem->maxsize = kpages;
//
}

static void
shutdown(int ispanic)
{
	int ms, once;

	lock(&active);
	if(ispanic)
		active.ispanic = ispanic;
	else if(m->machno == 0 && (active.machs & (1<<m->machno)) == 0)
		active.ispanic = 0;
	once = active.machs & (1<<m->machno);
	active.machs &= ~(1<<m->machno);
	active.exiting = 1;
	unlock(&active);

	if(once)
		iprint("cpu%d: exiting\n", m->machno);
	spllo();
	for(ms = 5*1000; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(active.machs == 0 && consactive() == 0)
			break;
	}
	delay(1000);
}

/*
 *  exit kernel either on a panic or user request
 */
void
exit(int code)
{
	shutdown(code);
	splhi();
	archreboot();
}

/* TODO */
void
reboot(void *entry, void *code, ulong size)
{
	USED(entry, code, size);
}

int
cmpswap(long *addr, long old, long new)
{
	return cas32(addr, old, new);
}
