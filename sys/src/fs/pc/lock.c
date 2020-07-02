 #include	"u.h"
#include	"lib.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"mem.h"

void	_xinc(long*);
long	_xdec(long*);

#define deccnt(x)	if(x)  _xdec(&x->nlock)
#define inccnt(x) if(x)  _xinc(&x->nlock)

void
printlocks(User *up)
{
	int i;

	for(i = 0; i < up->nlock; i++){
		print("%p:%p", up->lstack[i], up->pstack[i]);
		if((i%4) == 0)
			print("\n");
	}
	if(i>0 && i%4)
 		print("\n");
}

void
lock(Lock *l)
{
	int /*i,*/ nl;
	ulong pc;

	pc = getcallerpc(&l);
	nl = 0;
	if(u)
		nl = u->nlock;
loop:
	inccnt(u);		/* prevent being scheded */
	if(tas(l) == 0) {
		l->pc = pc;
		if(u){
			u->lstack[nl] = l;
			u->pstack[nl] = pc;
		}
		return;
	}
	deccnt(u);

#ifdef multiprocessor
	for(i = 0; i < 1000000; i++){
		if(l->sbsem)
			continue;
 		inccnt(u);
   		if(tas(l) == 0) {
			l->pc = pc;
			if(u){
				u->lstack[nl] = l;
				u->pstack[nl] = pc;
			}
			return;
		}
		deccnt(u);
	}
#endif
	l->sbsem = 0;	// BOTCH

	print("lock loop %d:%#p called by %#p held by pc %#p\n", u?u->pid:-1, l, pc, l->pc);
	if(u)
		dumpstack(u);
	dotrace(0);
	if(getstatus() & IFLAG)
		sched();
	else
		print("ilock deadlock\n");
	goto loop;
}

void
unlock(Lock *l)
{
	if(l->sbsem == 0)
		print("unlock: not locked: pc %#p\n", getcallerpc(&l));
	l->pc = 0;
	l->sbsem = 0;
	coherence();

	if(u && _xdec(&u->nlock) == 0)
	if(u->delaysched)
	if(getstatus() & IFLAG){
		/*
		 * Call sched if the need arose while locks were held
		 * But, don't do it from interrupt routines, hence the islo() test
		 */
		u->delaysched = 0;
		sched();
	}
}

int
canlock(Lock *l)
{
	inccnt(u);
	if(tas(l)){
		deccnt(u);
		return 0;
	}
	l->pc = getcallerpc(&l);
	return 1;
}

void
ilock(Lock *l)
{
	ulong x, pc;

	pc = getcallerpc(&l);

	x = splhi();
	if(tas(l) == 0)
		goto acquire;

	if(!l->isilock)
		panic("ilock: not ilock %p", pc);
	if(l->m == MACHP(m->machno))
		panic("ilock: deadlock cpu%d pc %p lpc %p\n", m->machno, pc, l->pc);
	for(;;){
		splx(x);
#ifdef multiprocessor
		while(l->sbsem)
			;
#else
		if(getstatus() & IFLAG)
			sched();
		else
			panic("ilock: locked & splhi\n");
#endif
		x = splhi();
		if(tas(l) == 0)
			goto acquire;
	}
acquire:
//	m->ilockdepth++;
//	if(u)
//		u->lastilock = l;
	l->sr = x;
	l->pc = pc;
	l->p = u;
	l->isilock = 1;
	l->m = MACHP(m->machno);
}

void
iunlock(Lock *l)
{
	ulong sr;

	if(l->sbsem == 0)
		panic("iunlock nolock: pc %p", getcallerpc(&l));
	if(l->isilock == 0)
		print("iunlock lock: pc %p held by %p\n", getcallerpc(&l), l->pc);
	if((getstatus()&IFLAG) != 0)
		print("iunlock lo: %p held by %p\n", getcallerpc(&l), l->pc);

	sr = l->sr;
	l->m = 0;
	l->sbsem = 0;
//	m->ilockdepth--;
	
	coherence();

//	if(u)
//		u->lastilock = 0;
	splx(sr);
}

void
qlock(QLock *q)
{
	User *p;
	int i;

	lock(q);
	if(!q->locked){
		q->locked = 1;
		unlock(q);
		goto out;
	}
	if(u) {
		for(i=0; i<NHAS; i++)
			if(u->has.q[i] == q) {
				print("circular qlock by %d at 0x%lux (other 0x%lux, 0x%lux)\n",
					u->pid, getcallerpc(&q), u->has.pc[i], q->pc);
				dumpstack(u);
				break;
			}
	}
	p = q->tail;
	if(p == 0)
		q->head = u;
	else
		p->qnext = u;
	q->tail = u;
	u->qnext = 0;
	u->state = Queueing;
	u->has.want = q;
	unlock(q);
	sched();
	u->has.want = 0;

out:
	if(u) {
		for(i=0; i<NHAS; i++)
			if(u->has.q[i] == 0) {
				u->has.q[i] = q;
				u->has.pc[i] = getcallerpc(&q);
				return;
			}
		print("NHAS(%d) too small\n", NHAS);
	}
}

int
canqlock(QLock *q)
{
	int i;

	lock(q);
	if(q->locked){
		unlock(q);
		return 0;
	}
	q->locked = 1;
	unlock(q);

	if(u){
		for(i=0; i<NHAS; i++)
			if(u->has.q[i] == 0) {
				u->has.q[i] = q;
				u->has.pc[i] = getcallerpc(&q);
				return 1;
			}
		print("NHAS(%d) too small\n", NHAS);
	}
	return 1;
}

void
qunlock(QLock *q)
{
	User *p;
	int i;

	lock(q);
	p = q->head;
	if(p) {
		q->head = p->qnext;
		if(q->head == 0)
			q->tail = 0;
		unlock(q);
		ready(p);
	} else {
		q->locked = 0;
		unlock(q);
	}

	if(u){
		for(i=0; i<NHAS; i++)
			if(u->has.q[i] == q) {
				u->has.q[i] = 0;
				return;
			}
		panic("qunlock: not there %p, called from %p\n",
			q, getcallerpc(&q));
	}
}

/*
 * readers/writers lock
 * allows 1 writer or many readers
 */
void
rlock(RWlock *l)
{
	QLock *q;

	qlock(&l->wr);			/* wait here for writers and exclusion */

	q = &l->rd;			/* first reader in, qlock(&l->rd) */
	lock(q);
	q->locked = 1;
	l->nread++;
	unlock(q);

	qunlock(&l->wr);

	if(u){
		int i;
		int found;

		found = 0;
		for(i=0; i<NHAS; i++){
			if(u->has.q[i] == q){
				print("circular rlock by %d at 0x%lux (other 0x%lux)\n",
					u->pid, getcallerpc(&l), u->has.pc[i]);
				dumpstack(u);
			}
			if(!found && u->has.q[i] == 0) {
				u->has.q[i] = q;
				u->has.pc[i] = getcallerpc(&l);
				found = 1;
			}
		}
		if(!found)
			print("NHAS(%d) too small\n", NHAS);
	}
}

void
runlock(RWlock *l)
{
	QLock *q;
	User *p;
	int n;

	q = &l->rd;
	lock(q);
	n = l->nread - 1;
	l->nread = n;
	if(n == 0) {			/* last reader out, qunlock(&l->rd) */
		p = q->head;
		if(p) {
			q->head = p->qnext;
			if(q->head == 0)
				q->tail = 0;
			unlock(q);
			ready(p);
			goto accounting;
		} 
		q->locked = 0;
	}
	unlock(q);

accounting:
	if(u){
		int i;
		for(i=0; i<NHAS; i++)
			if(u->has.q[i] == q) {
				u->has.q[i] = 0;
				return;
			}
		panic("runlock: not there %p, called from %p\n",
			(ulong)q, getcallerpc(&l));
	}
}

void
wlock(RWlock *l)
{
	qlock(&l->wr);			/* wait here for writers and exclusion */
	qlock(&l->rd);			/* wait here for last reader */
}

void
wunlock(RWlock *l)
{
	qunlock(&l->rd);
	qunlock(&l->wr);
}
