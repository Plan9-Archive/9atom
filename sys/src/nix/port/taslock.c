#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "../port/edf.h"

/*
 * measure max lock cycles and max lock waiting time.
 */
#define	LOCKCYCLES	0

uvlong maxlockcycles;
uvlong maxilockcycles;
uintptr maxlockpc;
uintptr maxilockpc;

Lockstats lockstats;
Waitstats waitstats;
Lock waitstatslk;

static void
newwaitstats(void)
{
	lock(&waitstatslk);
	if(waitstats.nsalloc == 0){
		waitstats.stat = malloc(NWstats * sizeof *waitstats.stat);
		if(waitstats.stat != nil)
			waitstats.nsalloc = NWstats;
	}
	unlock(&waitstatslk);
}

void
startwaitstats(int on)
{
	newwaitstats();
	waitstats.on = on;
	print("lockstats %s\n", on? "on": "off");
}

void
clearwaitstats(void)
{
	waitstats.nstats = 0;
}

void
addwaitstat(uintptr pc, uvlong t0, int type)
{
	uint i, nstats;
	uvlong w;
	Waitstat *s;

	if(waitstats.on == 0)
		return;

	cycles(&w);
	w -= t0;

	/* hope to slide by with no lock */
	coherence();
	nstats = waitstats.nstats;
	for(i = 0; i < nstats; i++)
		if((s = waitstats.stat + i)->pc == pc){
			ainc(&s->count);
			if(w > s->maxwait)
				s->maxwait = w;		/* race but ok */
			s->cumwait += w;		/* race but ok */
			return;
		}

	if(!canlock(&waitstatslk))
		return;

	/* newly created stat? */
	for(i = nstats; i < waitstats.nstats; i++)
		if((s = waitstats.stat + i)->pc == pc){
			ainc(&s->count);
			if(w > s->maxwait)
				s->maxwait = w;		/* race but ok */
			s->cumwait += w;
			unlock(&waitstatslk);
			return;
		}

	if(waitstats.nstats == waitstats.nsalloc){
		unlock(&waitstatslk);
		return;
	}

	s = waitstats.stat + waitstats.nstats;
	s->count = 1;
	s->type = type;
	s->maxwait = w;
	s->cumwait = w;
	s->pc = pc;
	waitstats.nstats++;

	unlock(&waitstatslk);
}

void
lockloop(Lock *l, uintptr pc)
{
	Proc *p;

	p = l->p;
	print("lock %#p loop key %#ux pc %#p held by pc %#p proc %d\n",
		l, l->key, pc, l->pc, p ? p->pid : 0);
	dumpaproc(up);
	if(p != nil)
		dumpaproc(p);
}

int
lock(Lock *l)
{
	int i;
	uintptr pc;
	uvlong t0;

	pc = getcallerpc(&l);

	lockstats.locks++;
	if(up)
		ainc(&up->nlocks);	/* prevent being scheded */
	if(TAS(&l->key) == 0){
		if(up)
			up->lastlock = l;
		l->pc = pc;
		l->p = up;
		l->isilock = 0;
		if(LOCKCYCLES)
			cycles(&l->lockcycles);

		return 0;
	}
	if(up)
		adec(&up->nlocks);

	cycles(&t0);
	lockstats.glare++;
	for(;;){
		lockstats.inglare++;
		i = 0;
		while(l->key){
			if(conf.nmach < 2 && up && up->edf && (up->edf->flags & Admitted)){
				/*
				 * Priority inversion, yield on a uniprocessor; on a
				 * multiprocessor, the other processor will unlock
				 */
				print("inversion %#p pc %#p proc %d held by pc %#p proc %d\n",
					l, pc, up ? up->pid : 0, l->pc, l->p ? l->p->pid : 0);
				up->edf->d = todget(nil);	/* yield to process with lock */
			}
			if(i++ > 100000000){
				i = 0;
				lockloop(l, pc);
			}
		}
		if(up)
			ainc(&up->nlocks);
		if(TAS(&l->key) == 0){
			if(up)
				up->lastlock = l;
			l->pc = pc;
			l->p = up;
			l->isilock = 0;
			if(LOCKCYCLES)
				cycles(&l->lockcycles);
			if(l != &waitstatslk)
				addwaitstat(pc, t0, WSlock);
			return 1;
		}
		if(up)
			adec(&up->nlocks);
	}
}

void
ilock(Lock *l)
{
	Mpl pl;
	uintptr pc;
	uvlong t0;

	pc = getcallerpc(&l);
	lockstats.locks++;

	pl = splhi();
	if(TAS(&l->key) != 0){
		cycles(&t0);
		lockstats.glare++;
		/*
		 * Cannot also check l->pc, l->m, or l->isilock here
		 * because they might just not be set yet, or
		 * (for pc and m) the lock might have just been unlocked.
		 */
		for(;;){
			lockstats.inglare++;
			splx(pl);
			while(l->key)
				;
			pl = splhi();
			if(TAS(&l->key) == 0){
				if(l != &waitstatslk)
					addwaitstat(pc, t0, WSlock);
				goto acquire;
			}
		}
	}
acquire:
	m->ilockdepth++;
	if(up)
		up->lastilock = l;
	l->pl = pl;
	l->pc = pc;
	l->p = up;
	l->isilock = 1;
	l->m = m;
	if(LOCKCYCLES)
		cycles(&l->lockcycles);
}

int
canlock(Lock *l)
{
	if(up)
		ainc(&up->nlocks);
	if(TAS(&l->key)){
		if(up)
			adec(&up->nlocks);
		return 0;
	}

	if(up)
		up->lastlock = l;
	l->pc = getcallerpc(&l);
	l->p = up;
	l->m = m;
	l->isilock = 0;
	if(LOCKCYCLES)
		cycles(&l->lockcycles);

	return 1;
}

void
unlock(Lock *l)
{
	uvlong x;

	if(LOCKCYCLES){
		cycles(&x);
		l->lockcycles = x - l->lockcycles;
		if(l->lockcycles > maxlockcycles){
			maxlockcycles = l->lockcycles;
			maxlockpc = l->pc;
		}
	}

	if(l->key == 0)
		print("unlock: not locked: pc %#p\n", getcallerpc(&l));
	if(l->isilock)
		print("unlock of ilock: pc %#p, held by %#p\n", getcallerpc(&l), l->pc);
	if(l->p != up)
		print("unlock: up changed: pc %#p, acquired at pc %#p, lock p %#p, unlock up %#p\n", getcallerpc(&l), l->pc, l->p, up);
	l->m = nil;
	l->key = 0;
	coherence();

	if(up && adec(&up->nlocks) == 0 && up->delaysched && islo()){
		/*
		 * Call sched if the need arose while locks were held
		 * But, don't do it from interrupt routines, hence the islo() test
		 */
		sched();
	}
}

void
iunlock(Lock *l)
{
	Mpl pl;
	uvlong x;

	if(LOCKCYCLES){
		cycles(&x);
		l->lockcycles = x - l->lockcycles;
		if(l->lockcycles > maxilockcycles){
			maxilockcycles = l->lockcycles;
			maxilockpc = l->pc;
		}
	}

	if(l->key == 0)
		print("iunlock: not locked: pc %#p\n", getcallerpc(&l));
	if(!l->isilock)
		print("iunlock of lock: pc %#p, held by %#p\n", getcallerpc(&l), l->pc);
	if(islo())
		print("iunlock while lo: pc %#p, held by %#p\n", getcallerpc(&l), l->pc);
	if(l->m != m){
		print("iunlock by cpu%d, locked by cpu%d: pc %#p, held by %#p\n",
			m->machno, l->m->machno, getcallerpc(&l), l->pc);
	}

	pl = l->pl;
	l->m = nil;
	l->key = 0;
	coherence();
	m->ilockdepth--;
	if(up)
		up->lastilock = nil;
	splx(pl);
}

void
portmwait(void *value)
{
	while (*(void**)value == nil)
		;
}

void (*mwait)(void *) = portmwait;
