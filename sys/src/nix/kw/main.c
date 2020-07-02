#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "adr.h"

#include "arm.h"

#include "init.h"
#include "reboot.h"

usize	segpgsizes = (1<<PGSHIFT);	/* only page size available */

/*
 * Where configuration info is left for the loaded programme.
 * This will turn into a structure as more is done by the boot loader
 * (e.g. why parse the .ini file twice?).
 * There are 3584 bytes available at CONFADDR.
 */
#define BOOTARGS	((char*)CONFADDR)
#define	BOOTARGSLEN	(16*KiB)		/* limit in devenv.c */
#define	MAXCONF		64
#define MAXCONFLINE	160

#define isascii(c) ((uchar)(c) > 0 && (uchar)(c) < 0177)

uintptr kseg0 = KZERO;
Mach* machaddr[MACHMAX];

/*
 * Option arguments from the command line.
 * oargv[0] is the boot file.
 * Optionsinit() is called from multiboot()
 * or some other machine-dependent place
 * to set it all up.
 */
static int oargc;
static char* oargv[20];
static char oargb[128];
static int oargblen;
static char oenv[4096];

static uintptr sp;		/* XXX - must go - user stack of init proc */

int vflag;
char debug[256];

/* store plan9.ini contents here at least until we stash them in #ec */
static char confname[MAXCONF][KNAMELEN];
static char confval[MAXCONF][MAXCONFLINE];
static int nconf;

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
	poperror();
	free(p);
}

/*
 * assumes that we have loaded our /cfg/pxe/mac file at 0x1000 with
 * tftp in u-boot.  no longer uses malloc, so can be called early.
 */
static void
plan9iniinit(void)
{
	char *k, *v, *next;

	k = (char *)CONFADDR;
	if(!isascii(*k))
		return;

	for(; k && *k != '\0'; k = next) {
		if (!isascii(*k))		/* sanity check */
			break;
		next = strchr(k, '\n');
		if (next)
			*next++ = '\0';

		if (*k == '\0' || *k == '\n' || *k == '#')
			continue;
		v = strchr(k, '=');
		if(v == nil)
			continue;		/* mal-formed line */
		*v++ = '\0';

		addconf(k, v);
	}
}

static void
optionsinit(char* s)
{
	char *o;

	o = strecpy(oargb, oargb+sizeof(oargb), s)+1;
	if(getenv("bootargs", o, o - oargb) != nil)
		*(o-1) = ' ';

	oargblen = strlen(oargb);
	oargc = tokenize(oargb, oargv, nelem(oargv)-1);
	oargv[oargc] = nil;
}

char*
getenv(char* name, char* buf, int n)
{
	char *e, *p, *q;

	p = oenv;
	while(*p != 0){
		if((e = strchr(p, '=')) == nil)
			break;
		for(q = name; p < e; p++){
			if(*p != *q)
				break;
			q++;
		}
		if(p == e && *q == 0){
			strecpy(buf, buf+n, e+1);
			return buf;
		}
		p += strlen(p)+1;
	}

	return nil;
}

#include "io.h"

typedef struct Spiregs Spiregs;
struct Spiregs {
	ulong	ictl;		/* interface ctl */
	ulong	icfg;		/* interface config */
	ulong	out;		/* data out */
	ulong	in;		/* data in */
	ulong	ic;		/* interrupt cause */
	ulong	im;		/* interrupt mask */
	ulong	_pad[2];
	ulong	dwrcfg;		/* direct write config */
	ulong	dwrhdr;		/* direct write header */
};

enum {
	/* ictl bits */
	Csnact	= 1<<0,		/* serial memory activated */

	/* icfg bits */
	Bytelen	= 1<<5,		/* 2^(this_bit) bytes per transfer */
	Dirrdcmd= 1<<10,	/* flag: fast read */
};

static void
dumpbytes(uchar *bp, long max)
{
	iprint("%#p: ", bp);
	for (; max > 0; max--)
		iprint("%02.2ux ", *bp++);
	iprint("...\n");
}

static void
spiprobe(void)
{
	Spiregs *rp = (Spiregs *)soc.spi;

	rp->ictl |= Csnact;
	coherence();
	rp->icfg |= Dirrdcmd | 3<<8;	/* fast reads, 4-byte addresses */
	rp->icfg &= ~Bytelen;		/* one-byte reads */
	coherence();

//	print("spi flash at %#ux: memory reads enabled\n", PHYSSPIFLASH);
}

void	archconsole(void);

void
machinit(void)
{
	memset(m, 0, sizeof(Mach));
	m->machno = 0;
	m->online = 1;
	machaddr[m->machno] = m;

	m->ticks = 1;
	m->perf.period = 1;

	conf.nmach = 1;

	up = nil;

	active.nonline = 1;
	active.exiting = 0;
}

/*
 * entered from l.s with mmu enabled.
 *
 * we may have to realign the data segment; apparently 5l -H0 -R4096
 * does not pad the text segment.  on the other hand, we may have been
 * loaded by another kernel.
 *
 * be careful not to touch the data segment until we know it's aligned.
 */
void
main(Mach* mach)
{
	extern char bdata[], edata[], end[], etext[];
	static ulong vfy = 0xcafebabe;

	m = mach;
	if (vfy != 0xcafebabe)
		memmove(bdata, etext, edata - bdata);
	if (vfy != 0xcafebabe) {
		wave('?');
		panic("misaligned data segment");
	}
	memset(edata, 0, end - edata);		/* zero bss */
	vfy = 0;

wave('9');
	machinit();
	archreset();
	mmuinit();

	optionsinit("/boot/boot boot");
	fmtinit();
	archconsole();
wave(' ');

	/* want plan9.ini to be able to affect memory sizing in confinit */
	plan9iniinit();		/* before we step on plan9.ini in low memory */

	adrinit();
	confinit();
//	options();

	trapinit();
	clockinit();

	printinit();
	uartkirkwoodconsole();

	conf.nmach = 1;

	fmtinit();
	print("\nnix\n");

	archconfinit();
	cpuidprint();

	timersinit();
	fpuinit();
	psinit(conf.nproc);
	initimage();
	links();
	devtabreset();
	pageinit();
	swapinit();
	userinit();
	schedinit();
}

void
cpuidprint(void)
{
	char name[64];

	cputype2name(name, sizeof name);
	print("cpu%d: %lldMHz ARM %s\n", m->machno, m->cpuhz/1000000, name);
}

static void
shutdown(int ispanic)
{
	int ms, once;

	lock(&active);
	if(ispanic)
		active.ispanic = ispanic;
	else if(m->machno == 0 && m->online == 0)
		active.ispanic = 0;
	once = m->online;
	m->online = 0;
	adec(&active.nonline);
	active.exiting = 1;
	unlock(&active);

	if(once)
		iprint("cpu%d: exiting\n", m->machno);
	spllo();
	for(ms = 5*1000; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(active.nonline == 0 && consactive() == 0)
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

/*
 * the new kernel is already loaded at address `code'
 * of size `size' and entry point `entry'.
 */
void
reboot(void *entry, void *code, usize size)
{
	void (*f)(uintmem, uintmem, usize);

	iprint("starting reboot...");
	writeconf();
	
	shutdown(0);

	/*
	 * should be the only processor running now
	 */

	print("shutting down...\n");
	delay(200);

	/* turn off buffered serial console? */

	devtabshutdown();

	/* call off the dog */
	clockshutdown();

	splhi();

	/* setup reboot trampoline function */
	f = (void*)REBOOTADDR;
	memmove(f, rebootcode, sizeof(rebootcode));
	cacheuwbinv();
	l2cacheuwb();

	print("rebooting...");
	iprint("entry %#lux code %#lux size %ld\n",
		PADDR(entry), PADDR(code), size);
	delay(100);		/* wait for uart to quiesce */

	/* off we go - never to return */
	cacheuwbinv();
	l2cacheuwb();
	(*f)(PADDR(entry), PADDR(code), size);

	iprint("loaded kernel returned!\n");
	delay(1000);
	archreboot();
}

/*
 *  starting place for first process
 */
void
init0(void)
{
	int i;
	char buf[2*KNAMELEN];

	assert(up != nil);
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

		/* convert plan9.ini variables to #e and #ec */
		for(i = 0; i < nconf; i++) {
			ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);

	touser(sp);
}

#include <tos.h>
char*
stkadj(uintptr base, char *p)
{
	uintptr a;

	a = PTR2UINT(p) - base;		/* offset in page */
	a += USTKTOP - PGSZ;	/* + base address */
	return (char*)UINT2PTR(a);
}

static void
bootargs(uintptr base, int argc, char **argv)
{
	int i, len;
	usize ssize;
	char **av, *p, *q, *e;

	len = 0;
	for(i = 0; i < argc; i++)
		len += strlen(argv[i] + 1);

	/*
	 * Push the boot args onto the stack.
	 * Make sure the validaddr check in syscall won't fail
	 * because there are fewer than the maximum number of
	 * args by subtracting sizeof(up->arg).
	 */
	p = UINT2PTR(STACKALIGN(base + PGSZ - sizeof(up->arg) - sizeof(Tos) - len));
	av = (char**)(p - (argc+2)*sizeof(char*));
	ssize = base + PGSZ - PTR2UINT(av);
	sp = USTKTOP - ssize;

	q = p;
	e = q + len;
	av[0] = (char*)argc;
	for(i = 0; i < argc; i++){
		av[i+1] = stkadj(base, argv[i]);
		q = seprint(q, e, "%s", argv[i]);
		*q++ = 0;
	}
	av[i+1] = nil;
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
	p->sched.sp = PTR2UINT(p->kstack+KSTACK-sizeof(up->args)-sizeof(uintptr));
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
	p->seg[SSEG] = s;
	pg = newpage(1, 0, USTKTOP-m->pgsz[s->pgszi], m->pgsz[s->pgszi], -1);
	segpage(s, pg);
	k = kmap(pg);
	bootargs(VA(k), 0, nil);
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, UTZERO+PGSZ);
	s->flushme++;
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO, m->pgsz[s->pgszi], -1);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memmove(UINT2PTR(VA(k)), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

Conf conf;			/* XXX - must go - gag */

Confmem sheevamem[] = {
	/*
	 * Memory available to Plan 9:
	 * the 8K is reserved for ethernet dma access violations to scribble on.
	 */
	{ .base = 0, .limit = 512*MB - 8*1024, },
};

void
confinit(void)
{
	conf.nmach = 1;
	conf.nproc = 2000;
	conf.nimage = 200;
}

void
confinit(void)
{
	int i;
	uintmem pa;

	/*
	 * Copy the physical memory configuration to Conf.mem.
	 */
	memmove(conf.mem, sheevamem, sizeof(sheevamem));

	pa = PADDR(PGROUND(PTR2UINT(end)));

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
}
