#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "init.h"
#include "io.h"

Conf	conf;
uintptr	kseg0 = KZERO;
Sys*	sys;
char	dbgflg[256];

static uintptr sp;		/* XXX - must go - user stack of init proc */
static int maxmach = MACHMAX;

extern	void	options(void);
extern	void	setmachsched(Mach*);

void
squidboy(int apicno)
{
	sys->machptr[m->machno] = m;
	setmachsched(m);

	m->perf.period = 1;
	m->cpuhz = sys->machptr[0]->cpuhz;
	m->cyclefreq = m->cpuhz;
	m->cpumhz = sys->machptr[0]->cpumhz;

	DBG("Hello Squidboy %d %d\n", apicno, m->machno);

	vsvminit(MACHSTKSZ);
	apmmuinit();
	if(!lapiconline())
		ndnr();
	fpuinit();
	m->splpc = 0;
	m->online = 1;

	/*
	 * CAUTION: no time sync done, etc.
	 */
	DBG("Wait for the thunderbirds!\n");
	while(!active.thunderbirdsarego)
		;
	wrmsr(0x10, sys->epoch);
	m->rdtsc = rdtsc();

	DBG("cpu%d color %d tsc %lld\n",
		m->machno, machcolor(m->machno), m->rdtsc);

	/*
	 * Enable the timer interrupt.
	 */
//	apictimerenab();
	lapicpri(0);

	timersinit();
	adec(&active.nbooting);
	ainc(&active.nonline);

	schedinit();

	panic("squidboy returns");
}

/*
 * Rendezvous with other cores.
 * wait until they are initialized.
 * Sync TSC with them.
 * We assume other processors that could boot had time to
 * set online to 1 by now.
 */
static void
nixsquids(void)
{
	int i;
	uvlong now, start;
	Mach *mp;

	for(i = 1; i < MACHMAX; i++)
		if((mp = sys->machptr[i]) != nil && mp->online != 0){
			conf.nmach++;
			ainc(&active.nbooting);
		}
	sys->epoch = rdtsc();
	coherence();
	wrmsr(0x10, sys->epoch);
	m->rdtsc = rdtsc();
	active.thunderbirdsarego = 1;
	start = fastticks2us(fastticks(nil));
	do{
		now = fastticks2us(fastticks(nil));
	}while(active.nbooting > 0 && now - start < 1000000);
	if(active.nbooting > 0)
		print("cpu0: %d maches couldn't start\n", active.nbooting);
	active.nbooting = 0;
}

void
main(void)
{
	vlong hz;

	memset(edata, 0, end - edata);

	/*
	 * ilock via i8250enable via i8250console
	 * needs m->machno, sys->machptr[] set, and
	 * also 'up' set to nil.
	 */
	cgapost(sizeof(uintptr)*8);
	memset(m, 0, sizeof(Mach));

	m->machno = 0;
	m->online = 1;
	sys->machptr[m->machno] = &sys->mach;
	m->stack = PTR2UINT(sys->machstk);
	m->vsvm = sys->vsvmpage;
	up = nil;
	active.nonline = 1;
	active.exiting = 0;
	active.nbooting = 0;
	log2init();
	adrinit();
	confinit();
	options();

	/*
	 * Need something for initial delays
	 * until a timebase is worked out.
	 */
	m->cpuhz = 2000000000ll;
	m->cpumhz = 2000;
	wrmsr(0x10, 0);				/* reset tsc */

	cgainit();
	i8250console();
	consputs = cgaconsputs;

	vsvminit(MACHSTKSZ);

	conf.nmach = 1;

	fmtinit();
	print("\nnix\n");

	m->perf.period = 1;
	hz = archhz();
	if(hz == 0)
		panic("no hz");
	m->cpuhz = hz;
	m->cyclefreq = hz;
	m->cpumhz = hz/1000000ll;

	/* Mmuinit before meminit because it flushes the TLB via m->pml4->pa.  */
	mmuinit();

	ioinit();
	kbdinit();
	meminit();
	archinit();
	mallocinit();

	/*
	 * Acpiinit will cause the first malloc. If the system dies here it's probably due
	 * to malloc not being initialized correctly, or the data segment is misaligned
	 * (it's amazing how far you can get with things like that completely broken).
	 */
	acpiinit();

	umeminit();
	trapinit();
	printinit();

	/*
	 * This is necessary with GRUB and QEMU. Without it an interrupt can occur
	 * at a weird vector, because the vector base is likely different, causing
	 * havoc. Do it before any APIC initialisation.
	 */
	i8259init(32);

	procinit0();
	if(getconf("*maxmach") != nil)
		maxmach = atoi(getconf("*maxmach"));
	mpsinit(maxmach);
	lapiconline();
	ioapiconline();
	sipi();

	timersinit();
	kbdenable();
	fpuinit();
	psinit(conf.nproc);
	initimage();
	links();
	devtabreset();
	pageinit();
	swapinit();
	userinit();
	nixsquids();
	schedinit();
}

static void
init0(void)
{
	char buf[2*KNAMELEN];

	up->nerrlab = 0;

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
		snprint(buf, sizeof(buf), "%s %s", "AMD64", conffile);
		ksetenv("terminal", buf, 0);
		ksetenv("cputype", "amd64", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		confsetenv();
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
	a += USTKTOP - BIGPGSZ;	/* + base address */
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
	p = UINT2PTR(STACKALIGN(base + BIGPGSZ - sizeof(up->arg) - sizeof(Tos) - len));
	av = (char**)(p - (argc+2)*sizeof(char*));
	ssize = base + BIGPGSZ - PTR2UINT(av);
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

void
userinit(void)
{
	Proc *p;
	Segment *s;
	KMap *k;
	Page *pg;

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
	 *
	 * N.B. make sure there's enough space for syscall to check
	 *	for valid args and
	 *	space for gotolabel's return PC
	 * AMD64 stack must be quad-aligned.
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
	p->seg[SSEG] = s;

	pg = newpage(1, 0, USTKTOP-m->pgsz[s->pgszi], m->pgsz[s->pgszi], -1);
	segpage(s, pg);
	k = kmap(pg);
	bootargs(VA(k), 0, nil);
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, UTZERO+BIGPGSZ);
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

void
confinit(void)
{
	conf.nproc = 2000;
	conf.nimage = 200;
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

	if(active.ispanic && m->machno == 0){
		if(cpuserver)
			delay(30000);
		else
			for(;;)
				halt();
	}
	else
		delay(1000);
}

void
apshut(void *v)
{
	int i;

	i = (int)(uintptr)v;
	procwired(up, i);
	sched();
	splhi();
	lapicpri(0xff);
	adec(&active.nonline);
	for(;;)
		hardhalt();
}

#include "amd64.h"
void
reboot(void *entry, void *code, usize size)
{
	int i;
	void (*f)(uintmem, uintmem, usize);

	panic("reboot");
	procwired(up, 0);
	sched();

	for(i = 0; i < active.nonline; i++)
		kproc("apshut", apshut, (void*)i);
	while(active.nonline>1)
		;

	/* turn off buffered serial console? */

	devtabshutdown();
	pcireset();

	lapicpri(0xff);
	outb(0x21, 0xff);
	outb(0xa1, 0xff);

//	m->pdp[0] = 0 | PtePS | PteP | PteRW;
	m->pml4[0] = m->pml4[PTLX(KZERO, 3)];
	cr3put(PADDR(m->pml4));

//	f = (void*)REBOOTADDR;
//	memmove(f, rebootcode, sizeof(rebootcode));
	f = (void*)0;

	coherence();
	(*f)(PADDR(entry), PADDR(code), size);
}

void
exit(int ispanic)
{
	shutdown(ispanic);
	archreset();
}
