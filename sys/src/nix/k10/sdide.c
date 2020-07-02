#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/sd.h"
#include <fis.h>
#include "../port/sdfis.h"
#include "ahci.h"

#define Ticks	sys->ticks

enum {
	Nctlr	= 4,
	Nctlrdrv	= 2,
	Nms	= 2*1024,
};

enum {
	BMspan	= 64*1024,
	Nprd	= SDmaxio/BMspan + 2,
};

typedef struct Ctlr Ctlr;
typedef struct Drive Drive;
typedef struct Prd Prd;
typedef struct Ioport Ioport;

struct Prd {
	u32int	pa;			/* not 64-bit safe */
	int	count;
};

struct Ioport {
	Lock;
	uint	cmd;
	uint	ctl;
	uint	bmiba;
};

struct Ctlr {
	SDev	*sdev;
	Drive	*drive[Nctlrdrv];
	Pcidev	*p;
	uint	pi;			/* legacy ports? */
	uint	map;			/* port exist? */
	int	tbdf;
	int	intl;

	int	hpscan;

	QLock;

	Ioport;

	void	(*ienable)(Ctlr*);
	void	(*idisable)(Ctlr*);
	void	(*irqack)(Ctlr*);

	uint	maxsec;			/* max io in sectors */
	uint	span;

//	Lock	irqmask;
	Rendez;
};

struct Drive {
	Sfisx;
	SDunit	*unit;
	Ctlr	*c;

	uint	dev;			/* for progamming Fdev */

	char	name[12];
	uint	state;
	uint	oldsstatus;
	uint	oldstate;
	uint	wait;
	uint	lastseen;
	uint	drvno;
	uint	edmaon;
	uint	*reg;

	uint	lastsig;
	uint	lastsigtk;

	uint	rflag;
};

extern	SDifc	sdideifc;
static	SDev	sdev[Nctlr];
static	Ctlr	ctlr[Nctlr];
static	uint	nctlr;
static	Drive	drive[Nctlr * Nctlrdrv];
static	int	hpevent;
static	Rendez	hprendez;
static	uint	fismap[] = {
[Fcmd]		7,
[Ffeat]		1,
[Flba0]		3,
[Flba8]		4,
[Flba16]		5,
[Fdev]		6,
[Flba24]		3,
[Flba32]		4,
[Flba40]		5,
[Ffeat8]		1,
[Fsc]		2,
[Fsc8]		2,
};

enum {
	Data	= 0,
	Status	= 7,

	As	= 2,
	Dc	= 2,
};

enum {
	/* As — alternate status */
	Bsy	= 0x80,

	/* Dc — device control */
	Nien		= 0x02,		/* (not) interrupt enable */
	Srst		= 0x04,		/* software reset */
	Hob		= 0x80,		/* high order bit [sic] */
};

enum {					/* device/head */
	Dev0		= 0xa0,		/* master */
	Dev1		= 0xb0,		/* slave */
	Devs		= Dev0 | Dev1,
	Lba		= 0x40,		/* lba mode */
};

/* copied from sdiahci */
enum {
	Dnull		= 0,
	Dmissing		= 1<<0,
	Dnopower	= 1<<1,
	Dnew		= 1<<2,
	Dready		= 1<<3,
	Derror		= 1<<4,
	Dreset		= 1<<5,
	Doffline		= 1<<6,
	Dlast		= 8,
};

static char *diskstates[Dlast] = {
	"null",
	"missing",
	"nopower",
	"new",
	"ready",
	"error",
	"reset",
	"offline",
};

static char*
dstate(uint s)
{
	int i;

	for(i = 0; s; i++)
		s >>= 1;
	return diskstates[i];
}

static char*
dnam(Drive *d)
{
	if(d->unit)
		return d->unit->name;
	snprint(d->name, sizeof d->name, "ide%ld", d - drive);
	return d->name;
}

static int
idewait(uint p, uchar mask, uchar v, int ms)
{
	int i;

	ms *= 1000;
	for(i=0; i<ms && (inb(p) & mask) != v; i++)
		microdelay(1);
	return i<ms;
}

int
driveselect(Drive *d)
{
	Ioport *i;

	i = d->c;
	if(!idewait(i->ctl+As, ASbsy|ASdrq, 0, 300))
		return -1;
	outb(i->cmd+fismap[Fdev], d->dev);
	if(!idewait(i->ctl+As, ASbsy|ASdrdy|ASdrq, ASdrdy, 300))
		return -1;
	return 0;
}

static uint
getsig(Drive *d)
{
	uint r, *m, cmd, n;

	r = d->c->cmd;
	cmd = r + fismap[Fcmd];
	m = fismap;
	outb(r+fismap[Fdev], d->dev);
	n = idewait(r + m[Fcmd], ASbsy|ASdrq, 0, 300);
	if(n == 0)
		return ~0;
	n = idewait(cmd, ASdrdy|ASbsy, ASdrdy, 300);
	if(n == 0)
		return ~0;
	outb(r+Dc, Nien);
	outb(cmd,  0x90);		/* execute device diagnostics */
	n = idewait(cmd, ASdrdy|ASbsy, ASdrdy, 6000);
	if(n == 0)
		return ~0;

	return inb(r+m[Flba16])<<24 |
		inb(r+m[Flba8])<<16 |
		inb(r+m[Flba0])<<8 |
		inb(r+m[Fsc])<<0;
}

/* this is slow.  don't do it often */
static uint
validsig(Drive *d)
{
	uint s;

	if(d->lastsigtk != 0 && (long)TK2MS(Ticks - d->lastsigtk) < 20000)
		s = d->lastsig;
	else{
		d->lastsigtk = Ticks | 1;
		d->lastsig = s = getsig(d);
	}
	if((ushort)s == 0x0101)
		return s;
	return 0;
}

/* revisit this if anything is done at interrupt time */
static void
setstate(Drive *d, uint s)
{
	qlock(d->c);		/* ilock ? */
	d->state = s;
	qunlock(d->c);
}

static int
newdrive(Drive *d)
{
	uint s;

	memset(&d->Sfis, 0, sizeof d->Sfis);
	memset(&d->Cfis, 0, sizeof d->Cfis);

	s = getsig(d);
	if(s == ~0){
		print("%s: no sig\n", dnam(d));
		setstate(d, Derror);
		return SDretry;
	}
	setfissig(d, s);
	if(ataonline(d->unit, d) < 0){
		setstate(d, Derror);
		return SDretry;
	}
	d->atamaxxfr = 64*1024 / d->secsize;		/* BOTCH wrong! */
	d->tler = 5000;
	pronline(d->unit, d);
	setstate(d, Dready);
	return 0;
}

static void
sstatuschange(Drive *d, uint s)
{
	switch(d->state){
	case Dmissing:
	case Dnull:
	//	if(s & Sphylink)
	//		setstate(d, Dnew);
		if((ushort)s == 0x0101)
			setstate(d, Dnew);			/* locking botch */
	case Doffline:
		qlock(d->c);
		d->drivechange = 1;
		d->unit->sectors = 0;
		qunlock(d->c);
		break;
	case Dnew:
	case Dready:
		if((s & Sphylink) == 0)
			setstate(d, Dmissing);
		break;
	case Derror:
		setstate(d, Dreset);
		break;
	}

	print("%s: status: %.3ux -> %.3ux: %s\n",
		dnam(d), d->oldsstatus, s, dstate(d->state));
	d->oldsstatus = s;
	d->wait = 0;
}

static void
statechange(Drive *d)
{
	switch(d->state){
	case Dreset:
		d->wait = 0;
		break;
	}
	d->oldstate = d->state;
}

static void
checkdrive(Drive *d)
{
	uint s;

	if(d->unit == nil)
		return;

	/* not really hot-pluggable */
	switch(d->state){
	case Dnew:
	case Dreset:
	case Dnull:
		break;
	default:
		//if(TK2MS(Ticks - d->lastseen) > 750)
		//	break;
		d->lastseen = Ticks;
		return;
	}

	qlock(d->c);
	s = validsig(d);		/* wrong, but somehow compatable with Spresent, Sphylink */
	qunlock(d->c);

	d->wait++;
	if(s & Spresent)
		d->lastseen = Ticks;
	if(s != d->oldsstatus)
		sstatuschange(d, s);
	if(d->state != d->oldstate)
		statechange(d);
	switch(d->state){
	case Dreset:
		if(d->wait % 40 != 0)
			break;
//////		reset(d);
		if((s & Sphylink) == 0)
			break;
		setstate(d, Dnew);
	case Dnew:
		newdrive(d);
d->udma = 0xff;	/* we don't do dma yet */
		break;
	}
	if((d->c->hpscan & (1<<d->unit->subno)) == 0)
		d->c->hpscan |= 1<<d->unit->subno;
}

static Lock hplock;

static int
havehp(void*)
{
	int r;

	ilock(&hplock);
	r = hpevent != 0;
	iunlock(&hplock);

	return r;
}

static void
hp(void)
{
	ilock(&hplock);
	if(hpevent == 0){
		hpevent = 1;
		wakeup(&hprendez);
	}
	iunlock(&hplock);
}

static void
hpproc(void*)
{
	int i;

	for(;;){
		for(i = 0; i < Nctlrdrv*nctlr; i++)
			checkdrive(drive + i);
		tsleep(&hprendez, havehp, nil, Nms);

		ilock(&hplock);
		hpevent = 0;
		iunlock(&hplock);
	}
}

static long
bio(SDunit *u, int lun, int write, void *a, long count, uvlong lba)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	return atabio(u, d, lun, write, a, count, lba);
}

static void
esleep(int ms)
{
	if(waserror())
		return;
	tsleep(&up->sleep, return0, 0, ms);
	poperror();
}

static int
isready(Drive *d, uint okstate)
{
	ulong s, δ;

	if(d->state & (Dreset | Derror))
		return 1;
	δ = TK2MS(Ticks - d->lastseen);
	if(d->state == Dnull || δ > 10*1000)
		return -1;
//	s = d->reg[Sstatus];
	s = Sphylink;
	if((s & Sphylink) == 0 && δ > 3000)
		return -1;
	if(d->state & okstate && (s & Sphylink))
		return 0;
	return -1;
}

static int
lockready(Drive *d, ulong tk, uint okstate)
{
	int r;

	for(;;){
		qlock(d->c);
		if((r = isready(d, okstate)) == 0)
			return r;
		qunlock(d->c);
		if(r == -1)
			return r;
		if(tk - Ticks - 10 < 1ul<<31)
			return -1;
		esleep(10);
	}
}

/* service for pkt cmd is Status&0x10 */
static void
interrupt(Ureg*, void *v)
{
	Ctlr *c;

	c = v;
	wakeup(c);
}

static int
rio(SDreq *r)
{
	Ctlr *c;
	Drive *d;
	SDunit *u;

	u = r->unit;
	c = u->dev->ctlr;
	d = c->drive[u->subno];
	if((d->state & (Dnew | Dready)) == 0)
		return sdsetsense(r, SDcheck, 3, 0x04, 0x24);
	if(r->timeout == 0)
		r->timeout = totk(Ms2tk(600*1000));
	if(d->feat & Datapi)
//		return iariopkt(r, d);
		return -1;
	return atariosata(u, d, r);
}

static int
cmdss(Drive *d, uint proto)
{
	uint ss;

	ss = d->secsize;
	if(proto&P512)
		ss = 512;
	if((proto&Pdatam) == Pnd)
		ss = 512;		/* we're not transferring anything */
	if(ss == 0)
		error("sector size 0");
	return ss;
}

static void
mkrfis(SDreq *r, Drive *d)
{
	uchar *c;
	uint cmd;

	cmd = d->c->cmd;
	c = r->cmd;
	c[Ftype] = 0x34;
	c[Fioport] = 0;
	if((d->feat & Dllba) && (r->ataproto & P28) == 0){
		c[Fsc8] = inb(cmd + fismap[Fsc8]);
		c[Fsc] = inb(cmd + fismap[Fsc]);
		c[Frerror] = inb(cmd + fismap[Frerror]);
		c[Flba24] = inb(cmd + fismap[Flba24]);
		c[Flba0] = inb(cmd + fismap[Flba0]);
		c[Flba32] = inb(cmd + fismap[Flba32]);
		c[Flba8] = inb(cmd + fismap[Flba8]);
		c[Flba40] = inb(cmd + fismap[Flba40]);
		c[Flba16] = inb(cmd + fismap[Flba16]);
		c[Fdev] = inb(cmd + fismap[Fdev]);
		c[Fstatus] = inb(cmd + fismap[Fstatus]);
	}else{
		c[Fsc] = inb(cmd + fismap[Fsc]);
		c[Frerror] = inb(cmd + fismap[Frerror]);
		c[Flba0] = inb(cmd + fismap[Flba0]);
		c[Flba8] = inb(cmd + fismap[Flba8]);
		c[Flba16] = inb(cmd + fismap[Flba16]);
		c[Fdev] = inb(cmd + fismap[Fdev]);
		c[Fstatus] = inb(cmd + fismap[Fstatus]);
	}
}

/* by the book simple polling pio (unused) */
int
basicpio(SDreq *r, Drive *d, uchar *p, int ctl, int ss, int nsec)
{
	int i, n;
	uint cmd;

	cmd = d->c->cmd;

	for(; nsec > 0; nsec--)
		for(i = 0; i < ss; i += 2){
			n = idewait(ctl+As, ASbsy|ASdrq, ASdrq, 300);
			if(n == 0){
				print("%s: %.2ux %d of %d xfr (%d sec left)\n",
					dnam(d), inb(ctl+As), i, ss/2, nsec);
				return n;		/* goto nrdy */
			}
			if(r->ataproto & Pout)
				outs(cmd+Data, p[i+1]<<8 | p[i]);
			else{
				n = ins(cmd+Data);
				p[i+0] = n;
				p[i+1] = n >> 8;
			}
		}
	n = idewait(ctl+As, ASbsy|ASdrdy|ASdrq, ASdrdy, 300);

	return n;
}

/* same as above with inss/outss */
int
piowithss(SDreq *r, Drive *d, uchar *p, int ctl, int ss, int nsec)
{
	int n;
	uint cmd;

	cmd = d->c->cmd;
	for(; nsec > 0; nsec--){
		n = idewait(ctl+As, ASbsy|ASdrq, ASdrq, 300);
		if(n == 0){
			print("%s: %.2ux; %d sec left\n",
				dnam(d), inb(ctl+As), nsec);
			return n;		/* goto nrdy */
		}
		if(r->ataproto & Pout)
			outss(cmd+Data, p, ss/2);
		else
			inss(cmd+Data, p, ss/2);
	}
	n = idewait(ctl+As, ASbsy|ASdrdy|ASdrq, ASdrdy, 300);

	return n;
}

/* same as above with interrupts */
static int
cmdready(void *v)
{
	uint r;
	Ctlr *c;

	c = v;
	r = inb(c->ctl+As);
	if(r & ASbsy)
		return 0;
	return r & (ASdrq | ASerr);
}

int
pioirq(SDreq *r, Drive *d, uchar *p, int ctl, int ss, int nsec)
{
	int n;
	uint s, cmd;
	ulong e;

	cmd = d->c->cmd;

	if(r->ataproto & Pout){
		microdelay(1);
		n = idewait(ctl+As, ASbsy|ASdrq, ASdrq, 1050);
		if(n == 0){
			/* err = inb(cmd+Error); */
			print("%s: %.2ux; %d sec left\n",
				dnam(d), inb(ctl+As)&0xff, nsec);
			return 0;
		}
		outss(cmd+Data, p, ss/2);
		nsec--;
	}
	for(; nsec > 0; nsec--){
		s = ASerr;
		for(e = sys->ticks + 1050; (long)(sys->ticks - e) < 0; ){
			tsleep(d->c, cmdready, d->c, e - sys->ticks);
			s = inb(cmd+Status);
			if((s & ASbsy) == 0)
				break;
		}
	//	s = inb(cmd+Status);		/* read status to reload irq */
		if((s & (ASerr|ASdrq)) != ASdrq){
			/* err = inb(cmd+Error); */
			print("%s: Status=%.2ux As=%.2ux (cmd=%.2ux); %d sec left\n",
				dnam(d), s, inb(ctl+As), r->cmd[Fcmd], nsec);
			return 0;
		}
		if(r->ataproto & Pout)
			outss(cmd+Data, p, ss/2);
		else
			inss(cmd+Data, p, ss/2);
	}
	return 1;
}

static int
piocmd(SDreq *r, Drive *d)
{
	uchar *c, *p;
	uint ss, cmd, ctl, nsec, n, u;

	ss = cmdss(d, r->ataproto);
	nsec = r->dlen / ss;
	if(r->dlen < nsec*ss)
		nsec = r->dlen/ss;
	if(nsec > 256)
		error("too many sectors");
	n = 0;			/* not successful yet */
	cmd = d->c->cmd;
	ctl = d->c->ctl;
	c = r->cmd;
	if(driveselect(d) == -1){
		print("%s: piocmd: drive select fail %.2ux\n", dnam(d), inb(ctl+As));
		goto nrdy;
	}
	n = idewait(cmd + fismap[Fcmd], ASdrdy|ASbsy, ASdrdy, 3000);
	if(n == 0) {
		print("%s: piocmd: notready %.2ux\n", dnam(d), cmd);
		goto nrdy;
	}

	if(r->ataproto & P28){
		outb(cmd + fismap[Fsc], c[Fsc]);
		outb(cmd + fismap[Ffeat], c[Ffeat]);
		outb(cmd + fismap[Flba0], c[Flba0]);
		outb(cmd + fismap[Flba8], c[Flba8]);
		outb(cmd + fismap[Flba16], c[Flba16]);
		outb(cmd + fismap[Fdev], c[Fdev]);
		outb(cmd + fismap[Fcmd], c[Fcmd]);
	}else{
		outb(cmd + fismap[Fsc8], c[Fsc8]);
		outb(cmd + fismap[Fsc], c[Fsc]);
		outb(cmd + fismap[Ffeat], c[Ffeat]);
		outb(cmd + fismap[Flba24], c[Flba24]);
		outb(cmd + fismap[Flba0], c[Flba0]);
		outb(cmd + fismap[Flba32], c[Flba32]);
		outb(cmd + fismap[Flba8], c[Flba8]);
		outb(cmd + fismap[Flba40], c[Flba40]);
		outb(cmd + fismap[Flba16], c[Flba16]);
		u = r->cmd[Fdev] & ~0xb0;
		outb(cmd + fismap[Fdev], u|d->dev);
		outb(cmd + fismap[Fcmd], c[Fcmd]);
	}

	p = r->data;
	n = pioirq(r, d, p, ctl, ss, nsec);
nrdy:
//	mkrfis(r, d);
	if(n==1){
		d->lastseen = Ticks;		/* hokey */
		r->rlen = r->dlen;
		sdsetsense(r, SDok, 0, 0, 0);
	}else{
print("sdide: piocmd fail AS %.2ux; n=%d; nsec=%d\n", inb(ctl+As), n, nsec);
		if(inb(ctl+As) & ASbsy|ASdrq)
			d->state = Dreset;
		print("%s: pio %.2ux\n", dnam(d), inb(ctl+As)&0xff);
		sdsetsense(r, SDcheck, 4, 8, 0);
	}
	return SDok;
}

static int
fisreqchk(Sfis *f, SDreq *r)
{
	if((r->ataproto & Pprotom) == Ppkt)
		return SDnostatus;
	/*
	 * handle oob requests;
	 *    restrict & sanitize commands
	 */
	if(r->clen != 16)
		error(Eio);
	if(r->cmd[0] == 0xf0){
		sigtofis(f, r->cmd);
		r->status = SDok;
		return SDok;
	}
	r->cmd[0] = 0x27;
	r->cmd[1] = 0x80;
	r->cmd[7] |= 0xa0;
	return SDnostatus;
}

static int
ataio(SDreq *r)
{
	int status;
	Ctlr *c;
	Drive *d;
	SDunit *u;
	int (*cmd)(SDreq*, Drive*);

	u = r->unit;
	c = u->dev->ctlr;
	d = c->drive[u->subno];
	if((status = fisreqchk(d, r)) != SDnostatus)
		return status;

	if(lockready(d, r->timeout, Dready|Dnew) == -1){
print("%s: ataio !lockready %.2ux %.2ux r->timeout %ld\n", dnam(d), r->cmd[Fcmd], r->ataproto, r->timeout);
		return SDeio;
}
	if(waserror()){
		qunlock(d->c);
		nexterror();
	}

	switch(r->ataproto & Pprotom){
	default:
		cmd = piocmd;
		break;
	case Pdma:
	case Pdmq:
		//cmd = dmacmd;
		cmd = nil;
		error("no dma commands");
		break;
	}
	status = cmd(r, d);
	qunlock(d->c);

	poperror();
	return status;
}

static int
online(SDunit *u)
{
	uint r;
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	r = 0;
	qlock(c);
	if(d->drivechange){
		r = 2;
		d->drivechange = 0;
		u->sectors = d->sectors;
		u->secsize = d->secsize;
	}else if(d->state == Dready)
		r = 1;
	qunlock(c);

	return r;
}

static int
verify(SDunit *u)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	d->unit = u;

	if((c->hpscan & 1<<u->subno) == 0){
		hp();
		while((c->hpscan & 1<<u->subno) == 0)
			tsleep(&up->sleep, return0, nil, 10);
	}

	return 1;
}

static int
enable(SDev *s)
{
	Ctlr *c;
	char name[32];
	static int once;
	static Lock l;

	c = s->ctlr;
	if(c->bmiba){
//		atadmaclr(ctlr);
//		ctlr->prdt = mallocalign(Nprd*sizeof(Prd), 4, 0, 64*1024);
	}
	snprint(name, sizeof name, "%s (%s)", s->name, s->ifc->name);
	intrenable(c->intl, interrupt, c, c->tbdf, name);
	outb(c->ctl+Dc, 0);
//	if(c->ienable != nil)
//		c->ienable(c);

	lock(&l);
	if(once == 0)
	if(nctlr>0){
		once++;
		kproc("idehp", hpproc, 0);
	}
	unlock(&l);

	return 1;
}

static int
disable(SDev *s)
{
	Ctlr *c;
	char name[32];

	c = s->ctlr;
	outb(c->ctl+Dc, Nien);		/* disable interrupts */
	snprint(name, sizeof(name), "%s (%s)", sdev->name, sdev->ifc->name);
//	intrdisable(c->intl, interrupt, c, c->tbdf, name);
//	free(c->prdt);
//	c->prdt = nil;
	if(c->p != nil)
		pciclrbme(c->p);
	return 0;
}

static void
clear(SDev *s)
{
	USED(s);
	print("fixme — ide clear\n");
}

static int
rctl(SDunit *u, char *p, int l)
{
	char *e, *op;
	Ctlr *c;
	Drive *d;

	if((c = u->dev->ctlr) == nil)
		return 0;
	d = c->drive[u->subno];

	e = p+l;
	op = p;
	if(d->state == Dready)
		p = sfisxrdctl(d, p, e);
	else
		p = seprint(p, e, "no disk present [%s]\n", dstate(d->state));
	p = seprint(p, e, "geometry %llud %lud\n", u->sectors, u->secsize);
	return p - op;
}

static int
wctl(SDunit* u, Cmdbuf* cb)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];

	if(strcmp(cb->f[0], "identify") == 0){
//		atadrive(u, d, c);
		USED(d);
		print("not identifying\n");
	}else
		error(Ebadctl);
	return 0;
}

static int
okide(Pcidev *p)
{
	if(p->ccrb != 1)
		return -1;
	switch(p->ccru){
	case 1:
//	case 4:
	case 0x80:
		break;
	default:
		return -1;
	}

	/* blacklist */
	switch(p->did<<16 | p->did){
	case 0x439c<<16 | 0x1002:
	case 0x438c<<16 | 0x1002:
		print("#S/sdide: blacklist bad ctlr %T\n", p->tbdf);
		return -1;
	}

	return 0;
}

enum {					/* Bus Master IDE I/O Ports */
	Bmicx		= 0,		/* Command */
	Bmisx		= 2,		/* Status */
	Bmidtpx		= 4,		/* Descriptor Table Pointer */
};
static void
ichirqack(Ctlr *c)
{
	int bmiba;

	if(bmiba = c->bmiba)
		outb(bmiba+Bmisx, inb(bmiba+Bmisx));
}

static void
atasrst(Ctlr *c)
{
	int dc0;

	dc0 = inb(c->ctl+Dc);
	microdelay(5);
	outb(c->ctl+Dc, Srst|dc0);
	microdelay(5);
	outb(c->ctl+Dc, dc0);
	microdelay(2*1000);
}

static int
seldev(Ctlr *c, int dev)
{
	if((dev & Devs) == Dev0 && c->map&1)
		return dev;
	if((dev & Devs) == Dev1 && c->map&2)
		return dev;
	return -1;
}

enum {
	Pcidev0		= 1,		/* 1<<0*2 */
	Pcidev1		= 4,		/* 1<<1*2 */

	Pcicap0		= 2,		/* 1<<0*2+1 */
	Pcicap1		= 8,		/* 1<<1*2+1 */
};

/*
 * this should work for intel, but doesn't.  i don't see why.
 */
static void
unlegacy(Ctlr *c)
{
	uint r;
	Pcidev *p;

	p = c->p;

	if(p->mem[0].bar == p->mem[1].bar)
		return;

	r = p->ccrp & (Pcidev0|Pcidev1);
	r = (r ^ (Pcidev0|Pcidev1)) << 1;
	r = (r & p->ccrp)>>1;
	if(r != 0){
		pcicfgw8(p, PciCCRp, p->ccrp | r);
		p->ccrp = pcicfgr8(p, PciCCRp);
		if((p->ccrp & r) != r)
			return;
		pcisetioe(p);
	}
}

static int
devmap(Ctlr *c)
{
	char *s;
	uint u;
	Pcidev *p;

	p = c->p;
	unlegacy(c);
	c->pi = p->ccrp;

	c->map = 3;
	c->span = BMspan;
	c->maxsec = 0;
	if(s = getconf("*idemaxio"))
		c->maxsec = atoi(s);

	u = p->vid<<16 | p->did;
	switch(p->vid){
	case 0x8086:
		u &= ~0xffff;
		break;
	}

	switch(u){
	default:
		return 0;
	case 0x8086<<16:
		c->map = 0;
		if(pcicfgr16(p, 0x40) & 0x8000)
			c->map |= 1;
		if(pcicfgr16(p, 0x42) & 0x8000)
			c->map |= 2;
		c->irqack = ichirqack;
		return 0;
	}
}

void
sdletter(SDev *s, Ctlr *c)
{
	static int nonlegacy = 'C';

	switch(c->cmd){
	default:
		s->idno = nonlegacy;
		break;
	case 0x1f0:
		s->idno = 'C';
		nonlegacy = 'E';
		break;
	case 0x170:
		s->idno = 'D';
		nonlegacy = 'E';
		break;
	}
}

typedef struct Lchan Lchan;
struct Lchan {
	int	cmd;
	int	ctl;
	int	irq;
	int	probed;
};
static Lchan lchan[2] = {
	0x1f0,	0x3f4,	IrqATA0,	0,
	0x170,	0x374,	IrqATA1,	0,
};


static SDev*
pnp(void)
{
	int i, seq;
	Ctlr *c;
	Drive *d;
	Pcidev *p;
	SDev *s;

	for(p = nil; p = pcimatch(p, 0, 0); ){
		if(okide(p) == -1)
			continue;
		if(nctlr+1 >= nelem(ctlr)){
			print("#S/sdide: %T: too many controllers\n", p->tbdf);
			continue;
		}
		for(seq = 0; seq < 2; seq++){
			c = ctlr + nctlr;
			s = sdev + nctlr;
			memset(c, 0, sizeof *c);
			memset(s, 0, sizeof *s);
			c->p = p;
			if(devmap(c) == -1)
				continue;
			pcisetbme(c->p);

			s->nunit = Nctlrdrv;
			s->ifc = &sdideifc;
			s->idno = '0' + nctlr;
			s->ctlr = c;
			c->sdev = s;
			if(c->pi & 1<<2*seq){
				c->tbdf = p->tbdf;
				c->intl = p->intl;
				c->cmd = p->mem[2*seq + 0].bar & ~1;
				c->ctl = p->mem[2*seq + 1].bar & ~1;
print("ide: %T: pci style %#ux %#ux\n", p->tbdf, c->cmd, c->ctl);
				if(c->cmd == c->ctl)
					continue;
			}else if(lchan[seq].probed == 0){
				lchan[seq].probed = 1;
				c->tbdf = BUSUNKNOWN;
				c->intl = lchan[seq].irq;
				c->cmd = lchan[seq].cmd;
				c->ctl = lchan[seq].ctl;
print("ide: %T: pci legacy style %#ux %#ux\n", p->tbdf, c->cmd, c->ctl);
			}else
				continue;
			if(c->pi & 0x80)
				c->bmiba = (p->mem[4].bar & ~1) + seq*8;

			if(ioalloc(c->cmd, 8, 0, "atacmd") == -1)
				continue;
			if(ioalloc(c->ctl, 8, 0, "atactl") == -1){
				iofree(c->cmd);
				continue;
			}
			sdletter(s, c);

			for(i = 0; i < Nctlrdrv; i++){
				d = drive + nctlr*Nctlrdrv + i;
				memset(d, 0, sizeof *d);
				c->drive[i] = d;
				d->c = c;
				d->dev = i==0? Dev0: Dev1;
			}
			sdadddevs(s);
			nctlr++;
		}
	}
	return nil;
}

SDifc sdideifc = {
	"ide",			/* name */

	pnp,			/* pnp */
	nil,			/* legacy */
	enable,			/* enable */
	disable,			/* disable */

	verify,			/* verify */
	online,			/* online */
	rio,			/* rio */
	rctl,			/* rctl */
	wctl,			/* wctl */

	bio,			/* bio */
	nil, //probew,		/* probe */
	clear,			/* clear */
	nil,			/* rtopctl */
	nil,			/* wtopctl */
	ataio,
};
