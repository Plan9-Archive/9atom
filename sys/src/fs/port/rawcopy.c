#include "all.h"
#include "mem.h"

typedef struct{
	Rendez	r;
	char	cowner;		// 0 = free, 1 = rawcopy
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

static void
rawcopy(Dcopy *d)
{
	Msgbuf *b;
	ulong t;

	d->t0 = Ticks;
	b = mballoc(RBUFSIZE, 0, Mxxx);
	for(d->p = d->start; d->p < d->lim; d->p++){
		if(d->iowner){
			d->iowner = 0;
			break;
		}
		if(devread(d->from, d->p, b->data)){
			print("rawcopy: %lld i/o error\n", d->p);
			break;
		}
		if(d->to && devwrite(d->to, d->p, b->data) != 0){
			print("rawcopy: block %lld: write error.\n", d->p);
			break;
		}
	}
	mbfree(b);
	t = Ticks-d->t0;
	print("rawcopy: halt %T\n", time());
	print("copied %lld blocks from %Z to %Z\n", (d->p-d->start), d->from, d->to);
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
rawcopyproc(void)
{
	for(;;){
		sleep(&d.r, owner, &d);

		switch(d.cmd){
		case 'c':
			if(setup(&d) == -1)
				break;
		case 'r':
			print("rawcopy: %lld blocks from %Z to %Z\n", d.lim-d.start, d.from, d.to);
			rawcopy(&d);
			break;
		default:
			print("bad rawcopy command\n");
			break;
		}
		d.cowner = 0;
		d.cmd = 0;
	}
}

static void
rcpause(void)
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
rcresume(void)
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
rchelp(void)
{
	print("usage: rawcopy start fdev tdev [start [end]]\n");
	print("usage: rawcopy pause\n");
	print("usage: rawcopy resume\n");
}

static void
rcstart(int c, char **v)
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
		rchelp();
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
	ulong t;
	char s[12*2+2], *state;

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
cmd_rawcopy(int c, char **v)
{
	if(c == 1){
		pstat();
		return;
	}

	v++, c--;
	if(strcmp("start", *v) == 0)
		rcstart(c, v);
	else if(strcmp("pause", *v) == 0)
		rcpause();
	else if(strcmp("resume", *v) == 0)
		rcresume();
	else
		rchelp();
}
