#include	"all.h"
#include	"mem.h"
#include	"io.h"
#include	"ureg.h"

Rendez dawnrend;
Queue	*raheadseqq;

void
machinit(void)
{
	int n;

	n = m->machno;
	memset(m, 0, sizeof(Mach));
	m->machno = n;
	m->mmask = 1<<m->machno;
	m->lights = 0;
	dofilter(&m->idle);

	active.exiting = 0;
	active.machs = 1;
}

static
void
confinit(void)
{
	conf.nmach = 1;
	conf.nproc = 80;

	conf.mem = meminit();
	conf.sparemem = conf.mem/12;		/* 8% spare for chk etc */
	if(conf.sparemem > 100*MiB)
		conf.sparemem = 100*MiB;

	conf.nuid = 1000;
	conf.nserve = 15;
	conf.nrahead = 20;
	conf.nfile = 30000;
	conf.nlgmsg = 100;
	conf.nsmmsg = 500;
	/*
	 * if you have trouble with IDE DMA or RWM (multi-sector transfers),
	 * perhaps due to old hardware, set idedma to zero in localconfinit().
	 */
	conf.idedma = 1;

	localconfinit();

	conf.nwpath = conf.nfile*8;
	conf.nauth = conf.nfile/10;
	conf.gidspace = conf.nuid*3;

	cons.flags = 0;
}

void
main(void)
{
	int i;

	echo = 1;
	predawn = 1;
		formatinit();
		machinit();
		cpuidentify();
		vecinit();
		confinit();
		if(conf.uartonly)
			uartspecial(0, kbdchar, conschar, conf.uartonly);
		lockinit();
		printinit();
		procinit();
		clockinit();

		print("Plan 9 fileserver\n");
		print("\t%d-bit, %d-byte blocks\n", sizeof(Off)*8-1, RBUFSIZE);

		profinit();

		mainlock.wr.name = "mainw";
		mainlock.rd.name = "mainr";
		reflock.name = "ref";

		qlock(&reflock);
		qunlock(&reflock);
		serveq = newqueue(1000);
		raheadq = newqueue(1000);
		raheadseqq = newqueue(1000);

		mbinit();
		sntpinit();
		otherinit();

		files = ialloc(conf.nfile * sizeof(*files), 0);
		for(i=0; i<conf.nfile; i++) {
			qlock(&files[i]);
			files[i].name = "file";
			qunlock(&files[i]);
		}

		wpaths = ialloc(conf.nwpath * sizeof(*wpaths), 0);
		uid = ialloc(conf.nuid * sizeof(*uid), 0);
		gidspace = ialloc(conf.gidspace * sizeof(*gidspace), 0);
		authinit();

		iobufinit();
		arginit();
		userinit(touser, 0, "ini");

	predawn = 0;
		wakeup(&dawnrend);
		launchinit();
		schedinit();
}

/*
 * read ahead processes.
 * read message from q and then
 * read the device.
 */
//#ifdef notdef
static int
rbcmp(void *va, void *vb)
{
	Rabuf *ra, *rb;

	ra = *(Rabuf**)va;
	rb = *(Rabuf**)vb;
	if(rb == 0)
		return 1;
	if(ra == 0)
		return -1;
	if(ra->dev > rb->dev)
		return 1;
	if(ra->dev < rb->dev)
		return -1;
	if(ra->addr > rb->addr)
		return 1;
	if(ra->addr < rb->addr)
		return -1;
	return 0;
}

static void
raheadq0(void)
{
	Rabuf *rb;
	Iobuf *p;

loop:
	rb = recv(raheadseqq, 1);
	p = getbuf(rb->dev, rb->addr, Bread);
	if(p)
		putbuf(p);

	lock(&rabuflock);
	rb->link = rabuffree;
	rabuffree = rb;
	unlock(&rabuflock);
	goto loop;
}

void
raheadsort(void)
{
	Rabuf *rb[50];
	int i, n;

	for(i = 0; i < conf.nrahead; i++)
		userinit(raheadq0, 0, "rah");
	for(;;){
		rb[0] = recv(raheadq, 1);
		for(n = 1; n < nelem(rb); n++) {
			/* roll the dice; hope waiting helps */
			while(raheadq->count == 0 && raheadseqq->count > 0)
				waitmsec(1);
			if(raheadq->count == 0)
				break;
			rb[n] = recv(raheadq, 1);
		}
//print("n=%d %lld\n", n, rb[0]->addr);
		qsort(rb, n, sizeof *rb, rbcmp);
		for(i = 0; i < n; i++)
			send(raheadseqq, rb[i]);
	}
}
//#endif

void
rahead(void)
{
	Rabuf *rb;
	Iobuf *p;

loop:
	rb = recv(raheadq, 1);
	p = getbuf(rb->dev, rb->addr, Bread);
	if(p)
		putbuf(p);

	lock(&rabuflock);
	rb->link = rabuffree;
	rabuffree = rb;
	unlock(&rabuflock);
	goto loop;
}

/*
 * main filesystem server loop.
 * entered by many processes.
 * they wait for message buffers and
 * then process them.
 */
void
serve(void)
{
	int i;
	Chan *cp;
	Msgbuf *mb;

	for (;;) {
		qlock(&reflock);
		mb = recv(serveq, 1);
		cp = mb->chan;
		rlock(&cp->reflock);
		qunlock(&reflock);

		rlock(&mainlock);

		if(serve9p(mb) != 1){
			print("bad message\n");
			for(i = 0; i < 12; i++)
				print(" %2.2ux", mb->data[i]);
			print("\n");
		}

		mbfree(mb);
		runlock(&mainlock);
		runlock(&cp->reflock);
	}
}

void
init0(void)
{
	m->proc = u;
	u->state = Running;
	u->mach = m;
	spllo();

	u->start();
}

/*
 * allow stuff like the floppy controller to be decoupled.
 */
static struct{
	void	(*f)(void*);
	void	*v;
}aetab[5];

void
atexit(void (*f)(void*), void *v)
{
	int i;
	
	for(i = 0; i < nelem(aetab); i++)
		if(aetab[i].f == 0){
			aetab[i].f = f;
			aetab[i].v = v;
			return;
		}
	panic("too many atexits");
}

static void
doexits(void){
	int i;

	for(i = 0; i < nelem(aetab); i++)
		if(aetab[i].f)
			aetab[i].f(aetab[i].v);
}

enum {
	Keydelay = 5*60,	/* seconds to wait for key press */
};

void
exit(void)
{
	if(m->machno == 0)
		doexits();
	u = 0;
	lock(&active);
	active.machs &= ~(1<<m->machno);
	active.exiting = 1;
	unlock(&active);
	if(!predawn)
		spllo();
	print("cpu %d exiting\n", m->machno);
	while(active.machs)
		delay(1);

	print("halted at %T.\npress a key to reboot sooner than %d mins.\n",
		time(), Keydelay/60);
	delay(500);		/* time to drain print q */

	splhi();
	/* reboot after delay (for debugging) or at key press */
	rawchar(Keydelay);

	spllo();
	delay(500);		/* time to drain echo q */
	print("rebooting...\n");
	delay(500);		/* time to drain print q */

	splhi();
	consreset();
	firmware();
}

#define	DUMPTIME	5	/* 5 am */
#define	WEEKMASK	0	/* every day (1=sun, 2=mon, 4=tue, etc.) */

/*
 * calculate the next dump time.
 * minimum delay is 100 minutes.
 */
Timet nextdump(Timet t)
{
	Timet n;

	n = nextime(t+MINUTE(100), DUMPTIME, WEEKMASK);
	if(!conf.nodump)
		print("next dump at %T\n", n);
	return n;
}

/*
 * process to copy dump blocks from
 * cache to worm. it runs flat out when
 * it gets work, but only looks for
 * work every 10 seconds.
 */
void
wormcopy(void)
{
	int f;
	Filsys *fs;
	Timet dt, t, nddate, ntoytime;

recalc:
	nddate = nextdump(t = time());
	ntoytime = t;
loop:
	dt = time() - t;
	if(dt < 0){
		print("time when back\n");
		goto recalc;
	}
	if(dt > MINUTE(100)){
		print("time jumped ahead\n");
		goto recalc;
	}
	t += dt;
	if(t > ntoytime) {
		dt = time() - rtctime();
		if(dt < 0)
			dt = -dt;
		if(dt > 10)
			print("rtc time more than 10 seconds out\n");
		else if(dt > 1)
			settime(rtctime());
		ntoytime = time() + HOUR(1);
		goto loop;
	}
	if(!conf.nodump)
	if(t > nddate){
		print("automatic dump %T\n", t);
		for(fs=filsys; fs->name; fs++)
			if(fs->dev->type == Devcw)
				cfsdump(fs);
		goto recalc;
	}

	f = 0;
	rlock(&mainlock);
	for(fs=filsys; fs->name; fs++)
		if(fs->dev->type == Devcw)
			f |= dumpblock(fs->dev);
	runlock(&mainlock);

	if(!f)
		waitmsec(10000);
	wormprobe();
	goto loop;
}

/*
 * process to synch blocks
 * it puts out a block/cache-line every second
 * it waits 10 seconds if caught up.
 * in both cases, it takes about 10 seconds
 * to get up-to-date.
 */
void
synccopy(void)
{
	int f;

	for (;;) {
		rlock(&mainlock);
		f = syncblock();
		runlock(&mainlock);
		if(!f)
			waitmsec(10000);
	//	else
	//		waitmsec(1000);
		/* pokewcp();	*/
	}
}
