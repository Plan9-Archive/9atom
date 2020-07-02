#include	"all.h"
#include	"mem.h"
#include	"io.h"
#include	"ureg.h"

Talarm	talarm;

void
profinit(void)
{
	extern char etext[];

	cons.minpc = KTZERO;
	cons.maxpc = (uintptr)etext;
	cons.nprofbuf = (cons.maxpc-cons.minpc) >> LRES;
	cons.profbuf = ialloc(cons.nprofbuf*sizeof(cons.profbuf[0]), 0);
}

static struct
{
	Lock;
	int	nfilter;
	Filter*	filters[500];
	int	time;
	int	cons;
} f;

void
dofilter(Filter *ft)
{
	int i;

	lock(&f);
	i = f.nfilter;
	if(i >= nelem(f.filters)) {
		print("dofilter: too many filters\n");
		unlock(&f);
		return;
	}
	f.filters[i] = ft;
	f.nfilter = i+1;
	unlock(&f);
}

void
checkalarms(void)
{
	User *p;
	Timet now;

	if(talarm.list == 0 || canlock(&talarm) == 0)
		return;

	now = Ticks;
	for(;;) {
		p = talarm.list;
		if(p == 0)
			break;

		if(p->twhen == 0) {
			talarm.list = p->tlink;
			p->trend = 0;
			continue;
		}
		if(now - p->twhen < 0)
			break;
		wakeup(p->trend);
		talarm.list = p->tlink;
		p->trend = 0;
	}

	unlock(&talarm);
}

void
processfilt(void)
{
	int i;
	Filter *ft;
	ulong c0, c1;

	for(i=0; i<f.nfilter; i++) {
		ft = f.filters[i];
		c0 = ft->count;
		c1 = c0 - ft->oldcount;
		ft->oldcount = c0;
		ft->filter[0] = famd(ft->filter[0], c1, 59, 60);
		ft->filter[1] = famd(ft->filter[1], c1, 599, 600);
		ft->filter[2] = famd(ft->filter[2], c1, 5999, 6000);
	}
}

void
clock(Timet n, ulong pc)
{
	int i;
	User *p;

	clockreload(n);
	m->ticks++;

	if(cons.profile) {
		cons.profbuf[0] += TK2MS(1);
		if(cons.minpc<=pc && pc<cons.maxpc){
			pc -= cons.minpc;
			pc >>= LRES;
			cons.profbuf[pc] += TK2MS(1);
		} else
			cons.profbuf[1] += TK2MS(1);
	}

	lights(Lreal, (m->ticks>>6)&1);
	if(m->machno == 0){
		if(f.cons >= 0) {
			(*consputc)(f.cons);
			f.cons = -1;
		}
		p = m->proc;
		if(p == 0)
			p = m->intrp;
		if(p)
			p->time.count += 10*TK2MS(1);
		else
			m->idle.count += 10*TK2MS(1);
		for(i=1; i<conf.nmach; i++){
			if(active.machs & (1<<i)){
				p = MACHP(i)->proc;
				if(p && p != m->intrp)
					p->time.count += 10*TK2MS(1);
			}
		}
		m->intrp = 0;

		f.time += TK2MS(1);
		while(f.time >= 1000) {
			f.time -= 1000;
			processfilt();
		}
	}

	if(active.exiting && active.machs&(1<<m->machno)){
		print("someone's exiting\n");
		exit();
	}

	checkalarms();
}

void
consstart(int c)
{
	f.cons = c;
}
