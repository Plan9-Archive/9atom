#include "all.h"
#include "mem.h"

typedef struct{
	Rendez	r;
	char	cowner;		// 0 = free, 1 = devcopy
	char	cmd;
	char	iowner;
	char	icmd;

	char	src[30];
	char	dst[30];
	Device	*from;
	Device	*to;
	vlong	start;
	vlong	p;
	vlong	end;
	vlong	lim;
	ulong	t0;
}Dcopy;

static Dcopy d;

static int
setup(Dcopy *d)
{
	Devsize tosize;

	if((d->from = devstr(d->src)) == 0){
		print("bad src device %s\n", d->src);
		return -1;
	}
	if(strcmp(d->dst, "nil") == 0)
		d->to = nil;
	else if((d->to = devstr(d->dst)) == 0){
		print("bad dest device %s\n", d->dst);
		return -1;
	}
	devinit(d->from);
	d->lim = devsize(d->from);
	if(d->to){
		devinit(d->to);
		tosize = devsize(d->to);
	}else
		tosize = 1LL<<62;
	if(tosize < d->lim)
		d->lim = tosize;
	if(d->end >= 0 && d->end < d->lim)
		d->lim = d->end;
	return 0;
}

static Off
prefetch(Dcopy *d, Off a, Off ra, Off lim)
{
	if(a+RAGAP*5 < lim)
		lim = a+RAGAP*5;
	while(ra < lim){
		ra++;
		preread(d->from, ra);
	}
	return ra+1;
}

static void
devcopy(Dcopy *d)
{
	ulong t;
	Iobuf *b, *w;
	Off ra;

	d->t0 = Ticks;
	ra = d->start;
	for(d->p = d->start; d->p < d->lim; d->p++){
		if(d->iowner){
			d->iowner = 0;
			break;
		}
		ra = prefetch(d, d->p, ra, d->lim);
		b = getbuf(d->from, d->p, Bread);
		if(b == 0){
			print("devcopy: %lld not written yet.\n", d->p);
			continue;
		}
		if(d->to){
			w = getbuf(d->to, b->addr, 0);
			memmove(w->iobuf, b->iobuf, RBUFSIZE);
			w->flags |= Bmod;
			putbuf(w);
		}
		putbuf(b);
	}
	t = Ticks-d->t0;
	print("devcopy: halt %T\n", time());
	print("copied %lld blocks from %Z to %Z\n", d->p-d->start, d->from, d->to);
	print("\t" "%,ld ticks\n", t);
	if(t)
		print("\t" "%,lld bytes/sec\n", (d->p-d->start)*RBUFSIZE*HZ/t);

	d->start = d->p;
}

static int
owner(void *v)
{
	return ((Dcopy*)v)->cowner;
}

void
devcopyproc(void)
{
	for(;;){
		sleep(&d.r, owner, &d);

		switch(d.cmd){
		case 'c':
			if(setup(&d) == -1)
				break;
		case 'r':
			print("devcopy: %lld blocks from %Z to %Z\n", d.lim-d.start, d.from, d.to);
			devcopy(&d);
			break;
		default:
			print("bad devcopy command\n");
			break;
		}
		d.cowner = 0;
		d.cmd = 0;
	}
}

static void
dcpause(void)
{
	if(d.cowner != 1){
		print("copy not running\n");
		return;
	}
	if(d.iowner != 0){
		print("interrupt already issued\n");
		return;
	}
	d.icmd = 'x';
	d.iowner = 1;
}

static void
dcresume(void)
{
	if(d.cowner == 1 || d.iowner == 1){
		print("copy already running\n");
		return;
	}
	if(d.from == 0 || d.to == 0){
		print("not started\n");
		return;
	}
	d.cmd = 'r';
	d.cowner = 1;
	wakeup(&d.r);
}

static void
dchelp(void)
{
	print("usage: devcopy start fdev tdev [start [end]]\n");
	print("usage: devcopy pause\n");
	print("usage: devcopy resume\n");
}

static void
dcstart(int c, char **v)
{
	if(d.cowner != 0 || d.iowner != 0){
		print("copy already running\n");
		return;
	}
	d.start = 0;
	d.end = -1;
	d.p = 0;
	d.lim = 0;
	switch(c){
	default:
		dchelp();
		return;
	case 5:
		d.end = number(v[4], 0, 0);
	case 4:
		d.start = number(v[3], 0, 0);
	case 3:
		if(strlen(v[1]) >= sizeof d.src || strlen(v[2]) >= sizeof d.dst){
			print("device strings too long\n");
			return;
		}
		snprint(d.src, sizeof d.src, "%s", v[1]);
		snprint(d.dst, sizeof d.dst, "%s", v[2]);
		break;
	}
	d.cmd = 'c';
	d.cowner = 1;
	wakeup(&d.r);
}

static void
pstat(void)
{
	char s[12*2+2], *state;
	ulong t;

	if(d.end != -1)
		snprint(s, sizeof s, "%,lld", d.lim);
	else
		snprint(s, sizeof s, "%,lld/%,lld", d.lim, d.end);
	state = d.cowner == 0 ? "idle" : "run";
	print("%s %,lld <- %,lld <- %s %Z %Z\n", state, d.start, d.p, s, d.from, d.to);
	if(d.cowner == 0)
		return;
	print("\t" "%,ld ticks\n", t = Ticks-d.t0);
	if(t)
		print("\t" "%,lld bytes/sec\n", (d.p-d.start)*RBUFSIZE*HZ/t);
}

void
cmd_devcopy(int c, char **v)
{
	if(c == 1){
		pstat();
		return;
	}

	v++, c--;
	if(strcmp("start", *v) == 0)
		dcstart(c, v);
	else if(strcmp("pause", *v) == 0)
		dcpause();
	else if(strcmp("resume", *v) == 0)
		dcresume();
	else
		dchelp();
}
