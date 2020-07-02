/*
 * intel/amd ahci sata controller
 * copyright © 2007-10 coraid, inc.
 */

#include "all.h"
#include "io.h"
#include "mem.h"
#include <fis.h>
#include "ahci.h"

#define	dprint(...)	if(debug)	print(__VA_ARGS__); else USED(debug)
#define	idprint(...)	if(prid	)	print(__VA_ARGS__); else USED(prid)
#define	aprint(...)	if(datapi)	print(__VA_ARGS__); else USED(datapi);

enum {
	SDok	= 0,
	SDeio	= -1,
	SDretry	= -2,
	SDcheck	= -3,
};

enum {
	NCtlr	= 1,
	NCtlrdrv	= 6,
	NDrive	= NCtlr*NCtlrdrv,

	Fahdrs	= 4,

	Read	= 0,
	Write,

	/* pci space configuration */
	Pmap	= 0x90,
	Ppcs	= 0x91,
	Prev	= 0xa8,

	Nms		= 256,
	Mphywait	=  2*1024/Nms - 1,
	Midwait		= 16*1024/Nms - 1,
	Mcomrwait	= 64*1024/Nms - 1,
};

enum {
	Tesb,
	Tsb600,
	Tjmicron,
	Tahci,
	Tlast,
};

typedef	struct	Ctlrtype	Ctlrtype;
typedef	struct	Ctlr	Ctlr;
typedef	struct	Drive	Drive;

struct Ctlrtype {
	uint	type;
	uint	maxdmaxfr;
	uint	flags;
	char	*name;
};

Ctlrtype cttab[Tlast] = {
[Tesb]		Tesb,		8192,	0,	"63xxesb",
[Tsb600]		Tsb600,		256,	0,	"sb600",
[Tjmicron]	Tjmicron,	8192,	0,	"jmicron",
[Tahci]		Tahci,		8192,	0,	"ahci",
};

enum {
	Dnull,
	Dmissing,
	Dnew,
	Dready,
	Derror,
	Dreset,
	Doffline,
	Dportreset,
	Dlast,
};

static char *diskstates[Dlast] = {
	"null",
	"missing",
	"new",
	"ready",
	"error",
	"reset",
	"offline",
	"portreset",
};

enum {
	DMautoneg,
	DMsatai,
	DMsataii,
	DMsataiii,
	DMlast,
};

static char *modes[DMlast] = {
	"auto",
	"satai",
	"sataii",
	"sataiii",
};

struct Drive {
	Lock;

	Ctlr	*ctlr;
	char	name[10];
	Aport	*port;
	Aportm	portm;
	Aportc	portc;		/* redundant ptr to port and portm. */

	uchar	drivechange;
	uchar	state;

	uvlong	sectors;
	uint	secsize;
	ulong	totick;
	ulong	lastseen;
	uint	wait;
	uchar	mode;
	uchar	active;

	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];
	uvlong	wwn;

	ushort	info[0x200];

	/*
	 * ahci allows non-sequential ports.
	 * to avoid this hassle, we let
	 * driveno	ctlr*NCtlrdrv + unit
	 * portno	nth available port
	 */
	uint	driveno;
	uint	portno;

	Filter	rate[2];
	int	fflag;
	int	init;
	ulong	reads, writes;
};

struct Ctlr {
	Lock;

	Ctlrtype	*type;
	Pcidev	*pci;

	uchar	*mmio;
	ulong	*lmmio;
	Ahba	*hba;

	Drive	rawdrive[NCtlrdrv];
	Drive*	drive[NCtlrdrv];
	int	ndrive;
};

static	Ctlr	iactlr[NCtlr];
static	int	niactlr;

static	Drive	*iadrive[NDrive];
static	int	niadrive;

static	int	debug;
static	int	prid = 1;
static	int	datapi;

static char stab[] = {
[0]	'i', 'm',
[8]	't', 'c', 'p', 'e',
[16]	'N', 'I', 'W', 'B', 'D', 'C', 'H', 'S', 'T', 'F', 'X'
};

static void
serrstr(ulong r, char *s, char *e)
{
	int i;

	e -= 3;
	for(i = 0; i < nelem(stab) && s < e; i++)
		if(r & (1<<i) && stab[i]){
			*s++ = stab[i];
			if(SerrBad & (1<<i))
				*s++ = '*';
		}
	*s = 0;
}

static void
dreg(char *s, Aport *p)
{
	dprint("%stask=%lux; cmd=%lux; ci=%lux; is=%lux\n",
		s, p->task, p->cmd, p->ci, p->isr);
}

void
xsleep(Rendez *r, int (*f)(void*), void *a)
{
	if(u)
		sleep(r, f, a);
	else
		for(; !f(a);)
			delay(1);
}

void
xtsleep(Rendez *r, int (*f)(void*), void *a, int ms)
{
	int i;

	if(u)
		tsleep(r, f, a, ms);
	else
		for(i = 0; i < ms; i++){
			if(f(a))
				break;
			delay(1);
		}
}

static void
esleep(int ms)
{
	xtsleep(&u->tsleep, no, 0, ms);
}

typedef struct {
	Aport	*p;
	int	i;
} Asleep;

static int
ahciclear(void *v)
{
	Asleep *s;

	s = v;
	return (s->p->ci & s->i) == 0;
}

static void
aesleep(Aportm *m, Asleep *a, int ms)
{
	xtsleep(m, ahciclear, a, ms);
}

static int
ahciwait(Aportc *c, int ms)
{
	Aport *p;
	Asleep as;

	p = c->p;
	p->ci = 1;
	as.p = p;
	as.i = 1;
	aesleep(c->m, &as, ms);
	if((p->task & 1) == 0 && p->ci == 0)
		return 0;
	dreg("ahciwait fail/timeout ", c->p);
	return -1;
}

static void
mkalist(Aportm *m, uint flags, uchar *data, int len)
{
	Actab *t;
	Alist *l;
	Aprdt *p;

	t = m->ctab;
	l = m->list;
	l->flags = flags | 0x5;
	l->len = 0;
	l->ctab = PCIWADDR(t);
	l->ctabhi = Pciwaddrh(t);
	if(data){
		l->flags |= 1<<16;
		p = &t->prdt;
		p->dba = PCIWADDR(data);
		p->dbahi = Pciwaddrh(data);
		p->count = 1<<31 | len - 2 | 1;
	}
}

static int
nop(Aportc *pc)
{
	uchar *c;

	if((pc->m->feat & Dnop) == 0)
		return -1;
	c = pc->m->ctab->cfis;
	nopfis(pc->m, c, 0);
	mkalist(pc->m, Lwrite, 0, 0);
	return ahciwait(pc, 3*1000);
}

static int
setfeatures(Aportc *pc, uchar f, uint w)
{
	uchar *c;

	c = pc->m->ctab->cfis;
	featfis(pc->m, c, f);
	mkalist(pc->m, Lwrite, 0, 0);
	return ahciwait(pc, w);
}

static int
settxmode(Aportc *pc, uchar f)
{
	uchar *c;

	c = pc->m->ctab->cfis;
	if(txmodefis(pc->m, c, f) == -1)
		return 0;
	mkalist(pc->m, Lwrite, 0, 0);
	return ahciwait(pc, 3*1000);
}

static void
asleep(int ms)
{
	esleep(ms);
}

static int
ahciportreset(Aportc *c, uint mode)
{
	ulong *cmd, i;
	Aport *p;

	p = c->p;
	cmd = &p->cmd;
	*cmd &= ~(Afre|Ast);
	for(i = 0; i < 500; i += 25){
		if((*cmd & Acr) == 0)
			break;
		asleep(25);
	}
	p->sctl = 3*Aipm | 0*Aspd | Adet;
	delay(1);
	p->sctl = 3*Aipm | mode*Aspd;
	return 0;
}

static int
ahciidentify0(Aportc *pc, void *id)
{
	uchar *c;
	Actab *t;

	t = pc->m->ctab;
	c = t->cfis;
	memset(id, 0, 0x200);
	identifyfis(pc->m, c);
	mkalist(pc->m, 0, id, 0x200);
	return ahciwait(pc, 3*1000);
}

static vlong
ahciidentify(Aportc *pc, ushort *id, uint *ss, char *d)
{
	int i, n;
	vlong s;
	Aportm *m;

	m = pc->m;
	for(i = 0;; i++){
		if(i > 5 || ahciidentify0(pc, id) != 0)
			return -1;
		n = idpuis(id);
		if(n & Pspinup && setfeatures(pc, 7, 20*1000) == -1)
			print("%s: puis spinup fail\n", d);
		if(n & Pidready)
			break;
		print("%s: puis waiting\n", d);
	}
	s = idfeat(m, id);
	*ss = idss(m, id);
	if(s == -1 || (m->feat&Dlba) == 0){
		if((m->feat&Dlba) == 0)
			dprint("%s: no lba support\n", d);
		return -1;
	}
	return s;
}

static int
ahciquiet(Aport *a)
{
	ulong *p, i;

	p = &a->cmd;
	*p &= ~Ast;
	for(i = 0; i < 500; i += 50){
		if((*p & Acr) == 0)
			goto stop;
		asleep(50);
	}
	return -1;
stop:
	if((a->task & (ASdrq|ASbsy)) == 0){
		*p |= Ast;
		return 0;
	}

	*p |= Aclo;
	for(i = 0; i < 500; i += 50){
		if((*p & Aclo) == 0)
			goto stop1;
		asleep(50);
	}
	return -1;
stop1:
	/* extra check */
	dprint("ahci: clo clear %lux\n", a->task);
	if(a->task & ASbsy)
		return -1;
	*p |= Afre | Ast;
	return 0;
}

static int
ahcicomreset(Aportc *pc)
{
	uchar *c;

	dreg("comreset ", pc->p);
	if(ahciquiet(pc->p) == -1){
		dprint("ahci: ahciquiet failed\n");
		return -1;
	}
	dreg("comreset ", pc->p);

	c = pc->m->ctab->cfis;
	nopfis(pc->m, c, 1);
	mkalist(pc->m, Lclear | Lreset, 0, 0);
	if(ahciwait(pc, 500) == -1){
		dprint("ahci: comreset1 failed\n");
		return -1;
	}
	microdelay(250);
	dreg("comreset ", pc->p);

	nopfis(pc->m, c, 0);
	mkalist(pc->m, Lwrite, 0, 0);
	if(ahciwait(pc, 150) == -1){
		dprint("ahci: comreset2 failed\n");
		return -1;
	}
	dreg("comreset ", pc->p);
	return 0;
}

static int
ahciidle(Aport *port)
{
	ulong *p, i, r;

	p = &port->cmd;
	if((*p & Arun) == 0)
		return 0;
	*p &= ~Ast;
	r = 0;
	for(i = 0; i < 500; i += 25){
		if((*p & Acr) == 0)
			goto stop;
		asleep(25);
	}
	r = -1;
stop:
	if((*p & Afre) == 0)
		return r;
	*p &= ~Afre;
	for(i = 0; i < 500; i += 25){
		if((*p & Afre) == 0)
			return 0;
		asleep(25);
	}
	return -1;
}

/*
 * §6.2.2.1  first part; comreset handled by reset disk.
 *	- remainder is handled by configdisk.
 *	- ahcirecover is a quick recovery from a failed command.
 */
static int
ahciswreset(Aportc *pc)
{
	int i;

	i = ahciidle(pc->p);
	pc->p->cmd |= Afre;
	if(i == -1)
		return -1;
	if(pc->p->task & (ASdrq|ASbsy))
		return -1;
	return 0;
}

static int
ahcirecover(Aportc *pc)
{
	ahciswreset(pc);
	pc->p->cmd |= Ast;
	if(settxmode(pc, pc->m->udma) == -1)
		return -1;
	return 0;
}

static void*
malign(int size, int align)
{
	void *v;

	v = ialloc(size, align);
	memset(v, 0, size);
	return v;
}

static void
setupfis(Afis *f)
{
	f->base = malign(0x100, 0x100);
	f->d = f->base + 0;
	f->p = f->base + 0x20;
	f->r = f->base + 0x40;
	f->u = f->base + 0x60;
	f->devicebits = (ulong*)(f->base + 0x58);
}

static void
ahciwakeup(Aportc *c, uint mode)
{
	ushort s;

	s = c->p->sstatus;
	if((s & Isleepy) == 0)
		return;
	if((s & Smask) != Spresent){
		print("ahci: slumbering drive missing %.3ux\n", s);
		return;
	}
	ahciportreset(c, mode);
//	print("ahci: wake %ux -> %ux\n", s, p->sstatus);
}

static int
ahciconfigdrive(Ahba *h, Aportc *c, int mode)
{
	Aportm *m;
	Aport *p;

	p = c->p;
	m = c->m;

	if(m->list == 0){
		setupfis(&m->fis);
		m->list = malign(sizeof *m->list, 1024);
		m->ctab = malign(sizeof *m->ctab, 128);
	}

	p->list = PCIWADDR(m->list);
	p->listhi = Pciwaddrh(m->list);
	p->fis = PCIWADDR(m->fis.base);
	p->fishi = Pciwaddrh(m->fis.base);

	p->cmd |= Afre;

	if((p->sstatus & Sbist) == 0 && (p->cmd & Apwr) != Apwr)
	if((p->sstatus & Sphylink) == 0 && h->cap & Hss){
		/* staggered spin-up? */
		dprint("ahci:  spin up ... [%.3lux]\n", p->sstatus);
		p->cmd |= Apwr;
		for(int i = 0; i < 1400; i += 50){
			if(p->sstatus & (Sphylink | Sbist))
				break;
			asleep(50);
		}
	}

	p->serror = SerrAll;

	if((p->sstatus & SSmask) == (Isleepy | Spresent))
		ahciwakeup(c, mode);
	/* disable power managment sequence from book. */
	p->sctl = 3*Aipm | mode*Aspd | 0*Adet;
	p->cmd &= ~Aalpe;

	p->cmd |= Ast;
	p->ie = IEM;

	return 0;
}

static int
ahcienable(Ahba *h)
{
	h->ghc |= Hie;
	return 0;
}

static int
ahcidisable(Ahba *h)
{
	h->ghc &= ~Hie;
	return 0;
}

static int
countbits(ulong u)
{
	int i, n;

	n = 0;
	for(i = 0; i < 32; i++)
		if(u & (1<<i))
			n++;
	return n;
}

static int
ahciconf(Ctlr *c)
{
	uint u;
	Ahba *h;

	h = c->hba = (Ahba*)c->mmio;
	u = h->cap;

	if((u & Ham) == 0)
		h->ghc |= Hae;

	print("ahci hba sss %d; ncs %d; coal %d; mports %d; led %d; clo %d; ems %d;\n",
		(u>>27) & 1, (u>>8) & 0x1f, (u>>7) & 1, u & 0x1f, (u>>25) & 1,
		(u>>24) & 1, (u>>6) & 1);
	return countbits(h->pi);
}

static int
ahcihbareset(Ahba *h)
{
	int wait;

	h->ghc |= Hhr;
	for(wait = 0; wait < 1000; wait += 100){
		if(h->ghc == 0)
			return 0;
		delay(100);
	}
	return -1;
}

static char*
tnam(Ctlr *c)
{
	return c->type->name;
}

static char*
dnam(Drive *d)
{
	char *s;

	s = d->name;
	if(d->name[0] == 0)
		snprint(d->name, sizeof d->name, "a%d", d->driveno);
	return s;
}

static int
identify(Drive *d)
{
	uchar oserial[21];
	ushort *id;
	vlong osectors, s;

	id = d->info;
	s = ahciidentify(&d->portc, id, &d->secsize, dnam(d));
	if(s == -1){
		d->state = Derror;
		return -1;
	}
	osectors = d->sectors;
	memmove(oserial, d->serial, sizeof d->serial);

	d->sectors = s;

	idmove(d->serial, id+10, 20);
	idmove(d->firmware, id+23, 8);
	idmove(d->model, id+27, 40);
	d->wwn = idwwn(d->portc.m, id);

	if(osectors != s || memcmp(oserial, d->serial, sizeof oserial))
		d->drivechange = 1;

	return 0;
}

static void
clearci(Aport *p)
{
	if(p->cmd & Ast){
		p->cmd &= ~Ast;
		p->cmd |=  Ast;
	}
}

static int
fmtσ(Fmt *f)
{
	Drive *d;
	char buf[8];

	d = va_arg(f->args, Drive*);
	if(d == nil)
		snprint(buf, sizeof buf, "s[nil]");
	else
		snprint(buf, sizeof buf, "a%d", d->driveno);
	return fmtstrcpy(f, buf);
}

static int
intel(Ctlr *c)
{
	return c->pci->vid == 0x8086;
}

static int
ignoreahdrs(Drive *d)
{
	return d->portm.feat & Datapi && d->ctlr->type->type == Tsb600;
}

static void
updatedrive(Drive *d)
{
	ulong f, cause, serr, s0, pr, ewake;
	Aport *p;
	static ulong last;

	pr = 1;
	ewake = 0;
	f = 0;
	p = d->port;
	cause = p->isr;
	if(d->ctlr->type->type == Tjmicron)
		cause &= ~Aifs;
	serr = p->serror;
	p->isr = cause;

	if(p->ci == 0){
		f |= Fdone;
		pr = 0;
	}else if(cause & Adps)
		pr = 0;
	if(cause & Ifatal){
		ewake = 1;
		dprint("%s: fatal\n", dnam(d));
	}
	if(cause & Adhrs){
		if(p->task & 33){
			if(ignoreahdrs(d) && serr & ErrE)
				f |= Fahdrs;
			dprint("%s: Adhrs cause %lux serr %lux task %lux\n",
				dnam(d), cause, serr, p->task);
			f |= Ferror;
			ewake = 1;
		}
		pr = 0;
	}
	if(p->task & 1 && last != cause)
		dprint("%s: err ca %lux serr %lux task %lux sstat %.3lux\n",
			dnam(d), cause, serr, p->task, p->sstatus);
	if(pr)
		dprint("%s: upd %lux ta %lux\n", dnam(d), cause, p->task);

	if(cause & (Aprcs|Aifs)){
		s0 = d->state;
		switch(p->sstatus & Smask){
		case Smissing:
			d->state = Dmissing;
			break;
		case Spresent:
			if((p->sstatus & Imask) == Islumber)
				d->state = Dnew;
			else
				d->state = Derror;
			break;
		case Sphylink:
			/* power mgnt crap for suprise removal */
			p->ie |= Aprcs|Apcs;	/* is this required? */
			d->state = Dreset;
			break;
		case Sbist:
			d->state = Doffline;
			break;
		}
		dprint("%s: %s → %s [Apcrs] %.3lux\n", dnam(d), diskstates[s0],
			diskstates[d->state], p->sstatus);
		if(s0 == Dready && d->state != Dready)
			idprint("%s: pulled\n", dnam(d));
		if(d->state != Dready)
			f |= Ferror;
		if(d->state != Dready || p->ci)
			ewake = 1;
	}
	p->serror = serr;
	if(ewake)
		clearci(p);
	if(f){
		d->portm.flag = f;
		wakeup(&d->portm);
	}
	last = cause;
}

static void
pstatus(Drive *d, ulong s)
{
	/*
	 * bogus code because the first interrupt is currently dropped.
	 * likely my fault.  serror is maybe cleared at the wrong time.
	 */
	switch(s){
	default:
		print("%s: pstatus: bad status %.3lux\n", dnam(d), s);
	case Smissing:
		d->state = Dmissing;
		break;
	case Spresent:
		break;
	case Sphylink:
		d->wait = 0;
		d->state = Dnew;
		break;
	case Sbist:
		d->state = Doffline;
		break;
	}
}

static int
configdrive(Drive *d)
{
	if(ahciconfigdrive(d->ctlr->hba, &d->portc, d->mode) == -1)
		return -1;
	ilock(d);
	pstatus(d, d->port->sstatus & Smask);
	iunlock(d);
	return 0;
}

static void
resetdisk(Drive *d)
{
	uint state, det, stat;
	Aport *p;

	p = d->port;
	det = p->sctl & 7;
	stat = p->sstatus & Smask;
	state = (p->cmd>>28) & 0xf;
	dprint("%s: resetdisk: icc %ux  det %.3ux sdet %.3ux\n", dnam(d), state, det, stat);

	ilock(d);
	state = d->state;
	if(d->state != Dready || d->state != Dnew)
		d->portm.flag |= Ferror;
	clearci(p);			/* satisfy sleep condition. */
	wakeup(&d->portm);
	d->state = Derror;
	iunlock(d);

	if(stat != Sphylink){
		ilock(d);
		d->state = Dportreset;
		iunlock(d);
		return;
	}

	qlock(&d->portm);
	if(p->cmd&Ast && ahciswreset(&d->portc) == -1){
		ilock(d);
		d->state = Dportreset;	/* get a bigger stick. */
		iunlock(d);
	}else{
		ilock(d);
		d->state = Dmissing;
		iunlock(d);
		configdrive(d);
	}
	dprint("%s: resetdisk: %s → %s\n", dnam(d), diskstates[state], diskstates[d->state]);
	qunlock(&d->portm);
}

static int
newdrive(Drive *d)
{
	char *s;
	Aportc *c;
	Aportm *m;

	c = &d->portc;
	m = &d->portm;

	qlock(c->m);
	setfissig(m, c->p->sig);
	if(identify(d) == -1){
		dprint("%s: identify failure\n", dnam(d));
		goto lose;
	}
	if(settxmode(c, m->udma) == -1){
		dprint("%s: can't set udma mode\n", dnam(d));
		goto lose;
	}
	if(m->feat & Dpower && setfeatures(c, 0x85, 3*1000) == -1){
		m->feat &= ~Dpower;
		if(ahcirecover(c) == -1)
			goto lose;
	}

	ilock(d);
	d->state = Dready;
	iunlock(d);

	qunlock(c->m);

	s = "";
	if(m->feat & Dllba)
		s = "L";
	idprint("%s: %sLBA %,lld sectors\n", dnam(d), s, d->sectors);
	idprint("  %s %s %s %s\n", d->model, d->firmware, d->serial,
		d->drivechange? "[newdrive]": "");
	return 0;

lose:
	idprint("%s: can't be initialized\n", dnam(d));
	ilock(d);
	d->state = Dnull;
	iunlock(d);
	qunlock(c->m);
	return -1;
}

static void
hangck(Drive *d)
{
	if((d->portm.feat & Datapi) == 0 && d->active &&
	    d->totick != 0 && (long)(Ticks - d->totick) > 0){
		print("%s: drive hung; resetting [%lux] ci %lux\n",
			dnam(d), d->port->task, d->port->ci);
		d->state = Dreset;
	}
}

static ushort olds[NCtlr*NCtlrdrv];

static int
doportreset(Drive *d)
{
	int i;

	i = -1;
	qlock(&d->portm);
	if(ahciportreset(&d->portc, d->mode) == -1)
		dprint("ahci: ahciportreset fails\n");
	else
		i = 0;
	qunlock(&d->portm);
	dprint("ahci: portreset → %s  [task %.4lux ss %.3lux]\n",
		diskstates[d->state], d->port->task, d->port->sstatus);
	return i;
}

/* drive must be locked */
static void
statechange(Drive *d)
{
	switch(d->state){
	case Dnull:
	case Doffline:
	case Dready:
		d->wait = 0;
	}
}

static uint
maxmode(Ctlr *c)
{
	return (c->hba->cap & 0xf*Hiss)/Hiss;
}

static void
checkdrive(Drive *d, int i)
{
	ushort s, sig;

	ilock(d);
	s = d->port->sstatus;
	if(s)
		d->lastseen = Ticks;
	if(s != olds[i]){
		dprint("%s: status: %.3ux -> %.3ux: %s\n",
			dnam(d), olds[i], s, diskstates[d->state]);
		olds[i] = s;
		d->wait = 0;
	}
	hangck(d);
	switch(d->state){
	case Dnull:
	case Dready:
		break;
	case Dmissing:
	case Dnew:
		switch(s & (Iactive|Smask)){
		case Spresent:
			ahciwakeup(&d->portc, d->mode);
		case Smissing:
			break;
		default:
			dprint("%s: unknown status %.3ux\n", dnam(d), s);
			/* fall through */
		case Iactive:		/* active, no device */
			if(++d->wait&Mphywait)
				break;
reset:
			if(d->mode == 0)
				d->mode = maxmode(d->ctlr);
			else
				d->mode--;
			if(d->mode == DMautoneg){
				d->state = Dportreset;
				goto portreset;
			}
			dprint("%s: reset; new mode %s\n", dnam(d),
				modes[d->mode]);
			iunlock(d);
			resetdisk(d);
			ilock(d);
			break;
		case Iactive | Sphylink:
			if((++d->wait&Midwait) == 0){
				dprint("%s: slow reset %.3ux task=%lux; %d\n",
					dnam(d), s, d->port->task, d->wait);
				goto reset;
			}
			s = (uchar)d->port->task;
			sig = d->port->sig >> 16;
			if(s == 0x7f || s&ASbsy ||
			    (sig != 0xeb14 && (s & ASdrdy) == 0))
				break;
			iunlock(d);
			newdrive(d);
			ilock(d);
			break;
		}
		break;
	case Doffline:
		if(d->wait++ & Mcomrwait)
			break;
		/* fallthrough */
	case Derror:
	case Dreset:
		dprint("%s: reset [%s]: mode %d; status %.3ux\n",
			dnam(d), diskstates[d->state], d->mode, s);
		iunlock(d);
		resetdisk(d);
		ilock(d);
		break;
	case Dportreset:
portreset:
		if(d->wait++ & 0xff && (s & Iactive) == 0)
			break;
		dprint("%s: portreset [%s]: mode %d; status %.3ux\n",
			dnam(d), diskstates[d->state], d->mode, s);
		d->portm.flag |= Ferror;
		clearci(d->port);
		wakeup(&d->portm);
		if((s & Smask) == 0){
			d->state = Dmissing;
			break;
		}
		iunlock(d);
		doportreset(d);
		ilock(d);
		break;
	}
	statechange(d);
	iunlock(d);
}

Rendez kprocr;

static void
satakproc(void)
{
	int i;

	for(;;){
		xtsleep(&kprocr, no, 0, Nms);
		for(i = 0; i < niadrive; i++)
			checkdrive(iadrive[i], i);
	}
}

static void
iainterrupt(Ureg*, void *a)
{
	int i;
	ulong cause, m;
	Ctlr *c;
	Drive *d;

	c = a;
	ilock(c);
	cause = c->hba->isr;
	for(i = 0; cause; i++){
		m = 1 << i;
		if((cause & m) == 0)
			continue;
		cause &= ~m;
		d = c->rawdrive + i;
		ilock(d);
		if(d->port->isr && c->hba->pi & m)
			updatedrive(d);
		c->hba->isr = m;
		iunlock(d);
	}
	iunlock(c);
}

/* returns locked list! */
static Alist*
ahcibuild(Aportm *m, int rw, void *data, uint n, vlong lba)
{
	uchar *c;
	uint flags;
	Alist *l;

	l = m->list;
	c = m->ctab->cfis;
	rwfis(m, c, rw, n, lba);
	flags = Lpref;
	if(rw)
		flags |= Lwrite;
	mkalist(m, flags, data, 512*n);
	return l;
}

static int
waitready(Drive *d)
{
	ulong s, i, δ;

	for(i = 0; i < 15000; i += 250){
		if(d->state == Dreset || d->state == Dportreset ||
		    d->state == Dnew)
			return 1;
		δ = Ticks - d->lastseen;
		if(d->state == Dnull || δ > 10*1000)
			return -1;
		ilock(d);
		s = d->port->sstatus;
		iunlock(d);
		if((s & Imask) == 0 && δ > 1500)
			return -1;
		if(d->state == Dready && (s & Smask) == Sphylink)
			return 0;
		esleep(250);
	}
	print("%s: not responding; offline\n", dnam(d));
	ilock(d);
	d->state = Doffline;
	iunlock(d);
	return -1;
}

static int
lockready(Drive *d)
{
	int i;

	qlock(&d->portm);
	while ((i = waitready(d)) == 1) {
		qunlock(&d->portm);
		esleep(1);
		qlock(&d->portm);
	}
	return i;
}

static int
io(Drive *d, uint proto, int to, int)
{
	uint task, flag, rv;
	Aport *p;
	Asleep as;

	switch(waitready(d)){
	case -1:
		return SDeio;
	case 1:
		return SDretry;
	}

	ilock(d);
	d->portm.flag = 0;
	iunlock(d);
	p = d->port;
	p->ci = 1;

	as.p = p;
	as.i = 1;
	d->totick = 0;
	if(to > 0)
		d->totick = Ticks + MS2TK(to) | 1;	/* fix fencepost */
	d->active++;

	xsleep(&d->portm, ahciclear, &as);

	d->active--;
	ilock(d);
	flag = d->portm.flag;
	task = d->port->task;
	iunlock(d);

	rv = SDok;
	if(proto & Ppkt){
		rv = task >> 8 + 4 & 0xf;
		flag &= ~Fahdrs;
		flag |= Fdone;
	}else if(task & (Efatal<<8) || task & (ASbsy|ASdrq) && d->state == Dready){
		d->port->ci = 0;
		ahcirecover(&d->portc);
		task = d->port->task;
		flag &= ~Fdone;		/* either an error or do-over */
	}
	if(flag == 0){
		print("%s: retry\n", dnam(d));
		return SDretry;
	}
	if(flag & (Fahdrs | Ferror)){
		if((task & Eidnf) == 0)
			print("%s: i/o error %ux\n", dnam(d), task);
		return SDcheck;
	}
	return rv;
}

static int
rw(Drive *d, int rw, uchar *a, ulong len, uvlong lba)
{
	int n, try, status, max, count;
	uchar *data;
	Ctlr *c;

	count = len / d->secsize;
	c = d->ctlr;
	if(d->portm.feat & Datapi){
		print("%s: no atapi support\n", dnam(d));
		return -1;
	}

	max = 128;
	if(d->portm.feat & Dllba){
		max = 8192;		/* ahci maximum */
		if(c->type->type == Tsb600)
			max = 255;	/* errata */
	}
	data = a;
	for(try = 0; try < 10; esleep(50)){
		n = count;
		if(n > max)
			n = max;
		qlock(&d->portm);
		ahcibuild(&d->portm, rw, data, n, lba);
		status = io(d, Pdma, 5000, 0);
		qunlock(&d->portm);
		switch(status){
		case SDeio:
			return -1;
		case SDretry:
			try++;
			continue;
		}
		try = 0;
		count -= n;
		lba   += n;
		data += n * d->secsize;
		if(count == 0)
			return data - (uchar*)a;
	}
	print("%s: bad disk\n", dnam(d));
	return -1;
}

/* configure drives 0-5 as ahci sata  (c.f. errata)  */
static int
iaahcimode(Pcidev *p)
{
	uint u;

	u = pcicfgr16(p, 0x92);
	dprint("ahci: iaahcimode %.2ux %.4ux\n", pcicfgr8(p, 0x91), u);
	pcicfgw16(p, 0x92, u | 0xf);		/* ports 0-15 (sic) */
	return 0;
}

enum{
	Ghc	= 0x04/4,	/* global host control */
	Pi	= 0x0c/4,	/* ports implemented */
	Cmddec	= 1<<15,	/* enable command block decode */

	/* Ghc bits */
	Ahcien	= 1<<31,	/* ahci enable */
};

static void
iasetupahci(Ctlr *c)
{
	pcicfgw16(c->pci, 0x40, pcicfgr16(c->pci, 0x40) & ~Cmddec);
	pcicfgw16(c->pci, 0x42, pcicfgr16(c->pci, 0x42) & ~Cmddec);

	c->lmmio[Ghc] |= Ahcien;
	c->lmmio[Pi] = (1 << 6) - 1;	/* 5 ports (supposedly ro pi reg) */

	/* enable ahci mode; from ich9 datasheet */
	pcicfgw16(c->pci, 0x90, 1<<6 | 1<<5);
}

static void
sbsetupahci(Pcidev *p)
{
	print("sbsetupahci: tweaking %.4ux ccru %.2ux ccrp %.2ux\n",
		p->did, p->ccru, p->ccrp);
	pcicfgw8(p, 0x40, pcicfgr8(p, 0x40) | 1);
	pcicfgw8(p, PciCCRu, 6);
	pcicfgw8(p, PciCCRp, 1);
	p->ccru = 6;
	p->ccrp = 1;
}

static ushort itab[] = {
	0xfffc,	0x2680,	Tesb,
	0xfffb,	0x27c1,	Tahci,		/* 82801g[bh]m */
	0xffff,	0x2821,	Tahci,		/* 82801h[roh] */
	0xfffe,	0x2824,	Tahci,		/* 82801h[b] */
	0xfeff,	0x2829,	Tahci,		/* ich8 */
	0xfffe,	0x2922,	Tahci,		/* ich9 */
	0xffff,	0x3a02,	Tahci,		/* 82801jd/do */
	0xfefe,	0x3a22,	Tahci,		/* ich10, pch */
	0xfff7,	0x3b28,	Tahci,		/* pchm */
	0xfffe,	0x3b22,	Tahci,		/* pch */
};

static int
didtype(Pcidev *p)
{
	int type, i;

	type = Tahci;
	switch(p->vid){
	default:
		return -1;
	case 0x8086:
		for(i = 0; i < nelem(itab); i += 3)
			if((p->did & itab[i]) == itab[i+1])
				return itab[i+2];
		break;
	case 0x1002:
		if(p->ccru == 1 || p->ccrp != 1)
		if(p->did == 0x4380 || p->did == 0x4390)
			sbsetupahci(p);
		type = Tsb600;
		break;
	case 0x1106:
		/*
		 * unconfirmed report that the programming
		 * interface is set incorrectly.
		 */
		if(p->did == 0x3349)
			return Tahci;
		break;
	case 0x10de:
	case 0x1039:
	case 0x1b4b:
	case 0x11ab:
		break;
	case 0x197b:
	case 0x10b9:
		type = Tjmicron;
		break;
	}
	if(p->ccrb == Pcibcstore && (uchar)p->ccru == 6 && p->ccrp == 1)
		return type;
	return -1;
}

static void
iapnp(void)
{
	int i, n, nunit, type;
	ulong io;
	Ctlr *c;
	Pcidev *p;
	Drive *d;

	memset(olds, 0xff, sizeof olds);
	p = nil;
loop:
	while(p = pcimatch(p, 0, 0)){
		if((type = didtype(p)) == -1)
			continue;
		if(p->mem[Abar].bar == 0)
			continue;
		if(niactlr == NCtlr){
			print("iapnp: %s: too many controllers\n", cttab[type].name);
			break;
		}
		c = iactlr + niactlr;
		memset(c, 0, sizeof *c);
		io = p->mem[Abar].bar & ~0xf;
		c->mmio = vmap(io, p->mem[Abar].size);
		if(c->mmio == 0){
			print("%s: address %#p in use did %.4ux\n",
				tnam(c), io, p->did);
			continue;
		}
		c->lmmio = (ulong*)c->mmio;
		c->pci = p;
		c->type = cttab + type;

		if(intel(c) && p->did != 0x2681)
			iasetupahci(c);
//		ahcihbareset((Ahba*)c->mmio);
		nunit = ahciconf(c);
		if(intel(c) && iaahcimode(p) == -1)
			continue;
		if(nunit < 1){
//			vunmap(c->mmio, p->mem[Abar].size);
			continue;
		}
		c->ndrive = nunit;

		i = (c->hba->cap >> 21) & 1;
		print("%s: sata-%s with %d ports\n",
			tnam(c), "I\0II" + i*2, nunit);

		/* map the drives -- they don't all need to be enabled. */
		memset(c->rawdrive, 0, sizeof c->rawdrive);
		n = 0;
		for(i = 0; i < NCtlrdrv; i++){
			d = c->rawdrive + i;
			d->portno = i;
			d->driveno = -1;
			d->sectors = 0;
			d->serial[0] = ' ';
			d->ctlr = c;
			if((c->hba->pi & 1<<i) == 0)
				continue;
			snprint(d->name, sizeof d->name, "a%d", niadrive + n);
			d->port = (Aport*)(c->mmio + 0x80*i + 0x100);
			d->portc.p = d->port;
			d->portc.m = &d->portm;
			d->driveno = n++;
			c->drive[d->driveno] = d;
			iadrive[niadrive + d->driveno] = d;
		}
		for(i = 0; i < n; i++)
			if(ahciidle(c->drive[i]->port) == -1){
				print("%s: port %d wedged; abort\n",
					tnam(c), i);
				goto loop;
			}
		for(i = 0; i < n; i++){
			c->drive[i]->mode = DMautoneg;
			configdrive(c->drive[i]);
		}

		pcisetbme(c->pci);
		setvec(p->intl+24, iainterrupt, c);
		ahcienable(c->hba);
		
		// do we want to do this here?
		for(i = 0; i < n; i++)
			checkdrive(c->drive[i], i);

		niadrive += nunit;
		niactlr++;
	}
	userinit(satakproc, 0, "iasata");
}

static void
statc(Ctlr *c)
{
	Drive *d;
	int j;

	for(j = 0; j < c->ndrive; j++){
		d = c->drive[j];
		if(d->fflag == 0)
			continue;
		print("%s:\n", dnam(d));
		print("  r\t%W\n", d->rate+Read);
		print("  w\t%W\n", d->rate+Write);
		print("	r %uld w %uld\n", d->reads, d->writes);
	}
}

static void
cmd_stat(int, char*[])
{
	int i;

	for(i = 0; i < niactlr; i++)
		statc(iactlr+i);
}

static Drive*
iadev(Device *d)
{
	int i, j;
	Drive *dr;

	i = d->wren.ctrl;
	j = d->wren.targ;

	for(; i < niactlr; i++){
		if(j < iactlr[i].ndrive){
			dr = iactlr[i].drive[j];
			if(dr->state&Dready)
				return dr;
			return 0;
		}
		j -= iactlr[i].ndrive;
	}
	panic("ia: bad drive %Z\n", d);
	return 0;
}

void
iainit0(void)
{
	fmtinstall(L'σ', fmtσ);
	iapnp();
	if(niactlr > 0){
		cmd_install("statr", "-- intel sata stats", cmd_stat);
	}
}

void
iainit(Device *dv)
{
	Drive *d;
	vlong s;
	char *lba;
	static int once;

	if(once++ == 0)
		iainit0();

top:
	d = iadev(dv);
	if(d == nil || d->secsize == 0){
		print("\t\t" "a%d.%d.%d not ready yet\n", dv->wren.ctrl, dv->wren.targ, dv->wren.lun);
		waitmsec(500);
		goto top;
	}

	if(d->init++ == 0){
		dofilter(d->rate+Read);
		dofilter(d->rate+Write);
	}

	s = d->sectors;
	lba = "";
	if(d->portm.feat&Dllba)
		lba = "L";
	print("\t\t" "%lld sectors/%lld blocks %sLBA\n", s, s/(RBUFSIZE/d->secsize), lba);
}


Devsize
iasize(Device *dv)
{
	Drive *d;

	d = iadev(dv);
	if(d == nil || d->secsize == 0)
		return 0;

	return d->sectors/(RBUFSIZE/d->secsize);
}

int
iaread(Device *dv, Devsize b, void *c)
{
	Drive *d;
	int rv;

	d = iadev(dv);
	if(d == nil || d->secsize == 0)
		return 1;

	rv = rw(d, 0, c, RBUFSIZE, b*(RBUFSIZE/d->secsize));
	if(rv != RBUFSIZE)
		return 1;
	d->rate[Read].count++;
	d->reads++;
	d->fflag = 1;
	return 0;
}

int
iawrite(Device *dv, Devsize b, void *c)
{
	Drive *d;
	int rv;

	d = iadev(dv);
	if(d == nil || d->secsize == 0)
		return 1;

	rv = rw(d, 1, c, RBUFSIZE, b*(RBUFSIZE/d->secsize));
	if(rv != RBUFSIZE)
		return 1;
	d->rate[Write].count++;
	d->writes++;
	d->fflag = 1;
	return 0;
}
