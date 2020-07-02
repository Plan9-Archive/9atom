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

#define	Ticks		sys->ticks
#define	dprint(...)	do{print(__VA_ARGS__);}while(0)

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

enum{
	Nctlr		= 1,
	Nctlrdrv		= 2,
	Nms		= 1024,

	Ecmdslot	= 31,

	/* command status */
	Factive		= 1<<0,
	Fdone		= 1<<1,
	Ferror		= 1<<2,

	Arb0		= PHYSIO + 0x80000,
	Arbstep		= 0,

	Port0		= PHYSIO + 0x82000,
	Port1		= PHYSIO + 0x84000,
	Portstep		= 0x02000,

	/* arbiter — base 0x80000 regardless of port */
	Hconf		= 0x000/4,
	Hhead		= 0x004/4,

	Htail		= 0x008/4,
	Hcoal		= 0x00c/4,
	Hitt		= 0x010/4,		/* interrupt time threshold */
	Hicr		= 0x014/4,
	Hmicr		= 0x020/4,
	Hmicm		= 0x024/4,
	Hled		= 0x02c/4,
	Win0ctl		= 0x030/4,
	Win0		= 0x034/4,
	Win1ctl		= 0x040/4,
	Win1		= 0x044/4,
	Win2ctl		= 0x050/4,
	Win2		= 0x054/4,
	Win3ctl		= 0x060/4,
	Win3		= 0x064/4,

	/* shadow register; see fismap[] for fis registers */
	Ide		= 0x100/4,
	Data		= Ide + 0,
	As		= Ide + 8,

	/* dma */
	Dcmd		= 0x224/4,
	Dsts		= 0x228/4,
	Cmdtlo		= 0x22c/4,
	Cmdthi		= 0x230/4,
	Prdtlo		= 0x234/4,
	Prdthi		= 0x238/4,

	/* edma */
	Ecf		= 0x000/4,	/* edma configuration */
	Eicr		= 0x008/4,	/* edma interrupt */
	Eimr		= 0x00c/4,

	Ereqhi		= 0x010/4,	/* request base hi */
	Ereqlo		= 0x014/4,	/* multiplexed with 9:5 of rqptr */
	Ereqtl		= 0x018/4,	/* just bits 9:5 system modified */
	Ersphi		= 0x01c/4,	/* response queue hi */
	Ersphd		= 0x020/4,	/* just bits 7:3 system modified */
	Ersplo		= 0x024/4,	/* multiplexed with 7:3 of rspptr */

	Ecmd		= 0x028/4,	/* edma cmd */
	Ests		= 0x030/4,	/* edma status */
	Eiordy		= 0x034/4,
	Edelay		= 0x040/4,
	Ehc		= 0x060/4,	/* halt conditions */
	Encq		= 0x094/4,

	/* general */

	Icfg		= 0x050/4,
	Pll		= 0x054/4,
	Sstatus		= 0x300/4,
	Serror		= 0x304/4,
	Sctl		= 0x308/4,
	Ltmode		= 0x30c/4,
	Phymode3	= 0x310/4,
	Phymode4	= 0x314/4,
	Phymode1	= 0x32c/4,
	Phymode2	= 0x330/4,
	Bistctl		= 0x334/4,
	Serrimsk		= 0x340/4,	/* bits defined by serr */
	Ifccr		= 0x344/4,	/* interace control register */
	Itr		= 0x348/4,	/* test register */
	Isr		= 0x34c/4,
	Fisconf		= 0x360/4,	/* errata 'r' us */
	Fisicr		= 0x364/4,
	Fisimr		= 0x368/4,	/* fis interrupt mask register */
	Fisdata0		= 0x370/4,	/* 0-6 dwords */
	Phymode92	= 0x398/4,
	Phymode91	= 0x39c/4,
	Phyconf		= 0x3a0/4,
	Phyctl		= 0x3a4/4,
	Phymode10	= 0x3a8/4,

	Piobase		= 0x100/4,	/* data, errf, sc, low, mid, high, dh, cs, as */

	/* Hconf */
	Dmanobs	= 1<<8,		/* le or be? */
	Edmanobs	= 1<<9,
	Prdbnobs	= 1<<10,
	Mtoen		= 1<<16,		/* mbus timeout enable */
	P0coaldis	= 1<<24,
	P1coaldis	= 1<<25,

	/* Hicr */
	Irpb0		= 1<<0,		/* basic dma */
	Icrpb1		= 1<<1,
	Icoal		= 1<<4,
	Iedma0		= 1<<8,		/* edma */
	Iedma1		= 1<<9,
	
	/* Hmicr, Hmicm */
	I0err		= 1<<0,
	I0done		= 1<<1,
	I1err		= 1<<2,
	I1done		= 1<<3,
	I0dmadn		= 1<<4,		/* dma or edma */
	I1dmadn		= 1<<5,
	Icoaldn		= 1<<8,

	/* Ecf */
	Enncq		= 1<<5,
	Etcq		= 1<<9,
	Efbs		= 1<<16,	/* fis based switching */
	Eearly		= 1<<18,
	Ehqcache	= 1<<22,
	Empm		= 1<<23,	/* mask pm bits */
	Eresume		= 1<<24,
	Edmafbs		= 1<<26,

	/* Eicr */
	Edeve		= 1<<2,
	Edevdis		= 1<<3,
	Edevcon		= 1<<4,
	Eserrint		= 1<<5,
	Eselfdis		= 1<<7,
	Etransint	= 1<<8,
	Erxiordy		= 1<<12,
	Ectlrx		= 0xf<<13,
	Edatarx		= 0xf<<17,
	Ectltx		= 0x1f<<21,
	Edatatx		= 0x1f<<27,
	Terr		= 1<<31,

	/* Eicr rx bits */
	Ecrc		= 1<<0,
	Efifo		= 1<<1,
	Esync		= 1<<2,
	Edispar		= 1<<3,

	/* Eicr tx bits; first 3 are same as rx */
	Edmat		= 1<<3,
	Ecollision	= 1<<4,

	/* Ecmd */
	Endma		= 1<<0,
	Disdma		= 1<<1,
	Atarst		= 1<<2,
	Freeze		= 1<<3,

	/* Ests */
	Etag		= 0x1f<<0,
	Datadir		= 1<<5,		/* 1 == dev->mem */
	Ece		= 1<<6,		/* edma cache empty */
	Eidle		= 1<<7,
	Estate		= 0x7f<<8,
	Eid		= 0x3f<<16,

	/* Icfg */
	Pllclk		= 1<<0,
	Icfgmagic	= 1<<2 | 1<<4 | 1<<12 | 3<<16 | 1<<20 | 1<<23,
	Giien		= 1<<7,
	Phyoff		= 1<<9,
	Targ		= 1<<10,
	Comch		= 1<<11,	/* target mode stuff */
	Txemph		= 3<<13,
	Clkdeten	= 1<<19,	/* ?? */
	Ignbsy		= 1<<24,
	Lnkrsten		= 1<<25,	/* + rst in status register = sync escape */

	/* Sstatus; same as ahci */
	/* Serror; same as ahci? */
	/* Sctl; same as ahci */

	/* Phymode2 */
	Powertx		= 1<<0,
	Powerrx		= 1<<1,
	Powerpll		= 1<<2,
	Powerivref	= 1<<3,
	Pdtxauto		= 1<<4,
	Tmclkstat	= 1<<6,

	/* Bistctl */
	Bisten		= 1<<9,

	/* Ifccr */
	Vum		= 1<<8,	/* vendor unique mode */
	Vus		= 1<<8,	/* vendor unique send */
	Edmaen		= 1<<16,	/* buggered.  this again :-( */
	Clearsts		= 1<<24,	/* clear 16, 30:31 in sata interface status */
	Softrst		= 1<<25,

	/* Isr */
	Fisrrx		= 0xff<<0,	/* last fis type rx'd */
	Pmrx		= 7<<8,	/* last port multiplier rx'd */
	Vud		= 1<<12,	/* vender unique done */
	Vue		= 1<<13,	/* vender unique err */
	Mbr		= 1<<14,	/* mem bist rdy */
	Mbf		= 1<<15,	/* mem bist fail */
	Aborted		= 1<<16,	/* abort command */
	Dmaact		= 1<<18,	/* dma active */
	Pioact		= 1<<19,	/* dma active */
	Rxhdact		= 1<<20,	/* header */
	Txhdact		= 1<<21,	/* header */
	Cabledet		= 1<<22,	/* device detected */
	Linkdn		= 1<<23,
	Transport	= 7<<24,	/* transport layer status */
	Rxbist		= 1<<30,
	N		= 1<<31,	/* set device bits notification */

	/* Fisicr, Fisimr */
	Ioksh		= 0,		/* okay rx fis type shift */
	Iesh		= 8,		/* err rx fis type shift */
	Fistxdone	= 1<<24,
	Fistxerr		= 1<<25,

	/* memory structure bits */

	/* Erq.ctl */
	Rqwrite	= 0,
	Rqread	= 1<<0,
	Rtag	= 0xf<<1,
	Rpmprt	= 0xf<<12,
	Rprd1	= 1<<16,
	Rhtag	= 0x1f<<17,
};

/* nausiatingly copied from ahci.  for the love of god, fix me.  need sata.h */
/* sctl register bits */
enum {
	Aspm	= 1<<12,
	Aipm	= 1<<8,		/* interface power mgmt. 3=off */
	Aspd	= 1<<4,		/* 0=any */
	Adet	= 1<<0,		/* device detection */
};
/* sstatus register bits */
enum{
	/* sstatus det */
	Smissing		= 0<<0,
	Spresent		= 1<<0,
	Sphylink		= 3<<0,
	Sbist		= 4<<0,
	Smask		= 7<<0,

	/* sstatus speed */
	Gmissing		= 0<<4,
	Gi		= 1<<4,
	Gii		= 2<<4,
	Giii		= 3<<4,
	Gmask		= 7<<4,

	/* sstatus ipm */
	Imissing		= 0<<8,
	Iactive		= 1<<8,
	Isleepy		= 2<<8,
	Islumber		= 6<<8,
	Imask		= 7<<8,

	SImask		= Smask | Imask,
	SSmask		= Smask | Isleepy,
};

typedef	struct	Drive	Drive;
typedef	struct	Ctlr	Ctlr;
typedef	struct	Ereq	Ereq;
typedef	struct	Ersp	Ersp;

/* 32 bytes */
struct Ereq{
	uint	prdlo;
	uint	prdhi;
	uint	ctl;
	uint	count;		/* 0 == 64k */
	uint	cmd[4];
};

/* 8 bytes */
struct Ersp{
	ushort	id;
	ushort	xtask;		/* Status<<8 | Eicr */
	uint	res;
};

struct Ctlr {
	SDev	*sdev;
	uint	*arb;
	Drive	*drive[Nctlrdrv];

	Lock	irqmask;
};

struct Drive {
	QLock;
	Sfisx;
	SDunit	*unit;
	Ctlr	*c;

	uchar	*mem;
	Ereq	*ereq;
	uint	reqslot;
	Ersp	*ersp;
	uint	rspslot;

	char	name[12];
	uint	state;
	uint	oldsstatus;
	uint	oldstate;
	uint	wait;
	uint	lastseen;
	uint	drvno;
	uint	edmaon;
	uint	*reg;

	uint	rflag;
	Rendez;
};

extern	SDifc	sdkwifc;

static	SDev	sdev[Nctlr];
static	Ctlr	ctlr[Nctlr];
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
static	uint	efismap[] = {
[Fcmd]		0*32 + 2*8,
[Ffeat]		0*32 + 3*8,
[Flba0]		1*32 + 0*8,
[Flba8]		1*32 + 1*8,
[Flba16]		1*32 + 2*8,
[Fdev]		1*32 + 3*8,
[Flba24]		2*32 + 0*8,
[Flba32]		2*32 + 1*8,
[Flba40]		2*32 + 2*8,
[Ffeat8]		3*32 + 3*8,
[Fsc]		4*32 + 0*8,
[Fsc8]		4*32 + 1*8,
};

static char*
dnam(Drive *d)
{
	if(d->unit)
		return d->unit->name;
	snprint(d->name, sizeof d->name, "kw%ld", d - drive);
	return d->name;
}

static char*
dstate(uint s)
{
	int i;

	for(i = 0; s; i++)
		s >>= 1;
	return diskstates[i];
}

static void
irqmask(Ctlr *c, uint mask, int set)
{
	ilock(&c->irqmask);
	if(set)
		c->arb[Hmicm] |= mask;
	else
		c->arb[Hmicm] &= ~mask;
	iunlock(&c->irqmask);
}

/* revisit this if anything is done at interrupt time */
static void
setstate(Drive *d, uint s)
{
	qlock(d);		/* ilock ? */
	d->state = s;
	qunlock(d);
}

static void
linkrst(Drive *d)
{
	d->reg[Sctl] = 3*Aipm | 0*Aspd | Adet | 3*Aspm;
	delay(1);
	d->reg[Sctl] = 3*Aipm | 0*Aspd | 0*Adet;
}

static uint spurious;

static int
analyze(Drive *d)
{
	static uint cause;

	cause = d->reg[Eicr];
	d->reg[Eicr] = ~cause;			/* write 0 to clear; clear only bits we see */
	if(cause & Eselfdis){
		irqmask(d->c, I0done, 0);
		d->edmaon = 0;
	}
	if(cause & Edeve)
		d->state = Dreset;
	return cause & ~Edevcon;
}

static void
interrupt(Ureg*, void *v)
{
	uint i, x, y, z;
	Ctlr *c;
	Drive *d;

	c = v;
	x = c->arb[Hmicr];
	y = c->arb[Hicr];
	c->arb[Hicr] = ~y;
	if(0) if(x & (I0err|I1err))
		print("sdkw: irq error %.8ux\n", x);
	for(i = 0; i < Nctlrdrv; i++){
		d = c->drive[i];
		z = analyze(d);
		if(x & (I0done|I0err)<<i*2)
		if((d->rflag & (Fdone|Factive)) == Factive){
			if(x & I0err<<i*2 || z)
				d->rflag |= Ferror;
			d->rflag |= Fdone;
			wakeup(d);
		}
		else
			spurious++;
	}
	intrclear(Irqlo, IRQ0sata);
}

static long
bio(SDunit *u, int lun, int write, void *a, long count0, uvlong lba)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	return atabio(u, d, lun, write, a, count0, lba);
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
	s = d->reg[Sstatus];
	if((s & Sphylink) == 0 && δ > 1500)
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
		qlock(d);
		if((r = isready(d, okstate)) == 0)
			return r;
		qunlock(d);
		if(r == -1)
			return r;
//		if(Ticks >= tk + 10)
		if(tk - Ticks - 10 < 1ul<<31)
			return -1;
		esleep(10);
	}
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

static int
satawait(uint *p, uchar mask, uchar v, int ms)
{
	int i;

	for(i=0; i<50; i++)
		if((*p & mask) == v)
			return 1;

	ms *= 1000;
	for(i=0; i<ms && (*p & mask) != v; i++)
		microdelay(1);
	return (*p & mask) == v;
}

static int
edmaable(Drive *d, int able)
{
	uint x;

	if(d->edmaon == able)
		return 0;
	d->edmaon = ~0;
	if(able&1){
		if(satawait(d->reg + Ide + fismap[Fcmd], ASbsy|ASdrq, 0, 30) != 1)
			return -1;
		d->reg[Fisicr] = 0;
		d->reg[Eicr] = 0;
		d->reg[Ecmd] |= Endma;
		if(satawait(d->reg + Ecmd, Endma|Disdma, Endma, 30) != 1)
			return -1;
	}else{
		d->reg[Ecmd] |= Disdma;
		if(able&2){
			irqmask(d->c, I0done, 0);		/* must not touch registers */
			x = d->state;
			setstate(d, Doffline);

			d->reg[Ecmd] |= Atarst;
			microdelay(25);
			d->reg[Ecmd] = 0;

		//	phyerrata(d);
			d->reg[Serror] = 0x07ffffff;
			d->reg[Icfg] = 1*Pllclk | Icfgmagic | Giien | Txemph | Clkdeten;

			print("link reset\n");
			linkrst(d);

			setstate(d, x);
		}
		if(satawait(d->reg + Ecmd, Endma|Disdma, 0, 30) != 1)
			return -1;
	}
	d->edmaon = able&1;
	irqmask(d->c, I0done<<d->drvno*2, able&1);
	return 0;
}

static void
mkrfis(SDreq *r, Drive *d)
{
	uchar *c;

	c = r->cmd;
	c[Ftype] = 0x34;
	c[Fioport] = 0;
	if((d->feat & Dllba) && (r->ataproto & P28) == 0){
		c[Fsc8] = d->reg[Ide + fismap[Fsc8]];
		c[Fsc] = d->reg[Ide + fismap[Fsc]];
		c[Frerror] = d->reg[Ide + fismap[Frerror]];
		c[Flba24] = d->reg[Ide + fismap[Flba24]];
		c[Flba0] = d->reg[Ide + fismap[Flba0]];
		c[Flba32] = d->reg[Ide + fismap[Flba32]];
		c[Flba8] = d->reg[Ide + fismap[Flba8]];
		c[Flba40] = d->reg[Ide + fismap[Flba40]];
		c[Flba16] = d->reg[Ide + fismap[Flba16]];
		c[Fdev] = d->reg[Ide + fismap[Fdev]];
		c[Fstatus] = d->reg[Ide + fismap[Fstatus]];
	}else{
		c[Fsc] = d->reg[Ide + fismap[Fsc]];
		c[Frerror] = d->reg[Ide + fismap[Frerror]];
		c[Flba0] = d->reg[Ide + fismap[Flba0]];
		c[Flba8] = d->reg[Ide + fismap[Flba8]];
		c[Flba16] = d->reg[Ide + fismap[Flba16]];
		c[Fdev] = d->reg[Ide + fismap[Fdev]];
		c[Fstatus] = d->reg[Ide + fismap[Fstatus]];
	}
}

static int
piocmd(SDreq *r, Drive *d)
{
	uchar *c, *p;
	uint i, ss, *cmd, nsec, n;

	ss = cmdss(d, r->ataproto);
	nsec = r->dlen / ss;
	if(r->dlen < nsec*ss)
		nsec = r->dlen/ss;
	if(nsec > 256)
		error("too many sectors");
	cmd = d->reg + Ide + fismap[Fcmd];

	n = 0;			/* not successful yet */
	if(edmaable(d, 0) == -1){
		print("pio: wait fail\n");
		goto nrdy;
	}
	n = satawait(cmd, ASdrdy|ASbsy, ASdrdy, 3*1000);
	if(n == 0) {
		print("piocmd: notready %.2ux\n", *cmd);
		goto nrdy;
	}

	c = r->cmd;
	if(r->ataproto & P28){
		d->reg[Ide + fismap[Fsc]] = c[Fsc];
		d->reg[Ide + fismap[Ffeat]] = c[Ffeat];
		d->reg[Ide + fismap[Flba0]] = c[Flba0];
		d->reg[Ide + fismap[Flba8]] = c[Flba8];
		d->reg[Ide + fismap[Flba16]] = c[Flba16];
		d->reg[Ide + fismap[Fdev]] = c[Fdev];
		d->reg[Ide + fismap[Fcmd]] = c[Fcmd];
	}else{
		d->reg[Ide + fismap[Fsc8]] = c[Fsc8];
		d->reg[Ide + fismap[Fsc]] = c[Fsc];
		d->reg[Ide + fismap[Ffeat]] = c[Ffeat];
		d->reg[Ide + fismap[Flba24]] = c[Flba24];
		d->reg[Ide + fismap[Flba0]] = c[Flba0];
		d->reg[Ide + fismap[Flba32]] = c[Flba32];
		d->reg[Ide + fismap[Flba8]] = c[Flba8];
		d->reg[Ide + fismap[Flba40]] = c[Flba40];
		d->reg[Ide + fismap[Flba16]] = c[Flba16];
		d->reg[Ide + fismap[Fdev]] = c[Fdev];
		d->reg[Ide + fismap[Fcmd]] = c[Fcmd];
	}

	p = r->data;
	for(; nsec > 0; nsec--)
		for(i = 0; i < ss; i += 2){
			n = satawait(cmd, ASbsy|ASdrq, ASdrq, 300);
			if(n == 0){
				print("%s: %.2ux %d of %d xfr (%d sec left)\n",
					dnam(d), *cmd&0xff, i, ss/2, nsec);
				goto nrdy;
			}
			if(r->ataproto & Pout)
				d->reg[Data] = p[i+1]<<8 | p[i];
			else{
				n = d->reg[Data];
				p[i+0] = n;
				p[i+1] = n >> 8;
			}
		}
	n = satawait(cmd, ASbsy|ASdrdy|ASdrq, ASdrdy, 300);
nrdy:
	mkrfis(r, d);
	if(n==1 && nsec == 0){
		r->rlen = r->dlen;
		sdsetsense(r, SDok, 0, 0, 0);
	}else{
		if(*cmd & ASbsy|ASdrq)
			d->state = Dreset;
		print("%s: pio %.2ux\n", dnam(d), *cmd&0xff);
		sdsetsense(r, SDcheck, 4, 8, 0);
	}
	return SDok;
}

static uint
nextrsp(Drive *d)
{
	uint n, x;
	Ersp *r;

	n = d->rspslot;
	r = d->ersp + n;
	l2cacheuinvse(r, sizeof *r);
	cachedinvse(r, sizeof *r);
	x = r->xtask;

	d->rspslot = ++n & Ecmdslot;
	r = d->ersp + d->rspslot;
	d->reg[Ersplo] = PADDR(r);

	return x;
}

static void
nextreq(Drive *d)
{
	Ereq *r;

	d->reqslot++;
	d->reqslot &= Ecmdslot;
	r = d->ereq + d->reqslot;
	d->reg[Ereqlo] = PADDR(r);
}

static void
edmasubmit(SDreq *r, Drive *d)
{
	uchar *u, *data;
	uint ctl, slot;
	Ereq *e;

	slot = d->reqslot;
	data = r->data;

	if(r->write == 1){
		cachedwbse(data, r->dlen);
		l2cacheuwbse(data, r->dlen);
	}
	else{
		/* protect the malloc magic.  silly */
		cachedwbse(data, CACHELINESZ);
		l2cacheuwbse(data, CACHELINESZ);

		cachedwbse(data + r->dlen, CACHELINESZ);
		l2cacheuwbse(data + r->dlen, CACHELINESZ);
	}

	u = r->cmd;
	ctl = slot<<17 | Rprd1;
	if(!r->write)
		ctl |= Rqread;

	e = d->ereq + slot;
	e->prdlo = PADDR(data);
	e->prdhi = 0;
	e->ctl = ctl;
	e->count = r->dlen & 0xffff;
	e->cmd[0] = 0 | 0 | u[Fcmd]<<16 | u[Ffeat]<<24;
	e->cmd[1] = u[Flba0] | u[Flba8]<<8 | u[Flba16]<<16 | u[Fdev]<<24;
	e->cmd[2] = u[Flba24] | u[Flba32]<<8 | u[Flba40]<<16 | u[Ffeat8]<<24;
	e->cmd[3] = u[Fsc] | u[Fsc8]<<8 | 0<<16 | 0<<24;

	cachedwbse(e, sizeof *e);
	l2cacheuwbse(e, sizeof *e);

	nextreq(d);
}

static int
dmadone(void *v)
{
	Drive *d;

	d = v;
	return d->rflag&Fdone;
}

static int
dmacmd(SDreq *r, Drive *d)
{
	uint x;

	if(edmaable(d, 1) == -1){
		print("dma: wait fail\n");
		sdsetsense(r, SDretry, 0, 0, 0);
		return SDok;
	}

	edmasubmit(r, d);

	d->rflag = Factive;
	if(waserror())
		d->rflag |= Ferror;		/* reset probablly needed */
	else{
		tsleep(d, dmadone, d, TK2MS(r->timeout - Ticks));
		poperror();
	}
	x = nextrsp(d);

	if((d->rflag & Fdone) == 0){
		d->state = Dreset;
		sdsetsense(r, SDretry, 0, 0, 0);
	}else if(x>>8 & (ASerr|ASdf|ASbsy))
		sdsetsense(r, SDeio, x>>8, 0, 0);	/* wrong sense code */
	else if(d->rflag & Ferror)
		sdsetsense(r, SDeio, 1, 1, 1);	/* wrong sense code */
	else
		sdsetsense(r, SDok, 0, 0, 0);
	d->rflag = 0;
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
	int status, l2;
	Ctlr *c;
	Drive *d;
	SDunit *u;
	int (*cmd)(SDreq*, Drive*);

	u = r->unit;
	c = u->dev->ctlr;
	d = c->drive[u->subno];
	l2 = 0;
	if((status = fisreqchk(d, r)) != SDnostatus)
		return status;

	if(lockready(d, r->timeout, Dready|Dnew) != SDok)
		return SDeio;
	if(waserror()){
		qunlock(d);
		nexterror();
	}

	switch(r->ataproto & Pprotom){
	default:
		cmd = piocmd;
		break;
	case Pdma:
	case Pdmq:
		if(r->write == 0)
			l2 = 1;
		cmd = dmacmd;
		break;
	}
	status = cmd(r, d);
	qunlock(d);

	if(l2){
		l2cacheuinvse(r->data, r->dlen);
		cachedinvse(r->data, r->dlen);
	}

	poperror();
	return status;
}

static int
wctl(SDunit *u, Cmdbuf *cmd)
{
	Ctlr *c;
	Drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];

	if(strcmp(cmd->f[0], "det") == 0)
		linkrst(d);
	else if(strcmp(cmd->f[0], "rst") == 0)
		d->reg[Ifccr] |= Softrst;
	else
		cmderror(cmd, Ebadctl);
	return 0;
}

static char*
rctldebug(char *p, char *e, Drive *d)
{
	p = seprint(p, e, "link	%s\n", "up\0down" + 3*((d->reg[Isr] & Linkdn) != 0));
	p = seprint(p, e, "sstatus	%.8ux\n", d->reg[Sstatus]);
	p = seprint(p, e, "serror	%.8ux\n", d->reg[Serror]);
	p = seprint(p, e, "sctl	%.8ux\n", d->reg[Sctl]);
	p = seprint(p, e, "isr	%.8ux\n", d->reg[Isr]);
	p = seprint(p, e, "icfg	%.8ux\n", d->reg[Icfg]);
	p = seprint(p, e, "ifccr	%.8ux\n", d->reg[Ifccr]);
	return p;
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

	op = p;
	e = p+l;
	p = seprint(p, e, "state\t%s\n", dstate(d->state));
	p = seprint(p, e, "sig\t%.8ux\n", d->sig);
	if(d->state == Dready)
		p = sfisxrdctl(d, p, e);
	p = rctldebug(p, e, d);
	p = seprint(p, e, "geometry %llud %lud\n", d->sectors, u->secsize);
	return p-op;
}

static int
rio(SDreq *r)
{
	USED(r);
	return -1;
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
	qlock(d);
	if(d->drivechange){
		r = 2;
		d->drivechange = 0;
		u->sectors = d->sectors;
		u->secsize = d->secsize;
	}else if(d->state == Dready)
		r = 1;
	qunlock(d);

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
	return 1;
}

static int
disable(SDev*)
{
	return 1;
}


static void
setaddrwin(Ctlr *c, uint i, int attr, uintptr base)
{
	uint *w;

	w = c->arb + Win0ctl + 2*i;
	w[0] = Winenable | Targdram << 4 | attr << 8 | 0x0fff0000;
	w[1] = base & ~0xffff;
}

static uint
gettask(Drive *d)
{
	return d->reg[Ide + fismap[Frerror]]<<8 |
		d->reg[Ide + fismap[Fcmd]];
}

static uint
getsig(Drive *d)
{
	uint *r, *m, *cmd, n;

	cmd = d->reg + Ide + fismap[Fcmd];
	n = satawait(cmd, ASdrdy|ASbsy, ASdrdy, 300);
	if(n == 0)
		return ~0;
	*cmd = 0x90;	/* execute device diagnostics */
	n = satawait(cmd, ASdrdy|ASbsy, ASdrdy, 300);
	if(n == 0)
		return ~0;

	r = d->reg + Ide;
	m = fismap;

	return r[m[Flba16]]<<24 |
		r[m[Flba8]]<<16 |
		r[m[Flba0]]<<8 |
		r[m[Fsc]]<<0;
}

static int
newdrive(Drive *d)
{
	uint s;

	memset(&d->Sfis, 0, sizeof d->Sfis);
	memset(&d->Cfis, 0, sizeof d->Cfis);

	if(edmaable(d, 0) == -1){
		setstate(d, Derror);
		return SDretry;
	}
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
	setstate(d, Dready);
	d->atamaxxfr = 64*1024 / d->secsize;
	d->tler = 5000;
	pronline(d->unit, d);
	return 0;
}

static void
reset(Drive *d)
{
	edmaable(d, 2);
}

static void
sstatuschange(Drive *d, uint s)
{
	switch(d->state){
	case Dmissing:
	case Dnull:
		if(s & Sphylink)
			setstate(d, Dnew);
	case Doffline:
		qlock(d);
		d->drivechange = 1;
		d->unit->sectors = 0;
		qunlock(d);
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

	dprint("%s: status: %.3ux -> %.3ux: %s\n",
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

	s = d->reg[Sstatus];
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
		reset(d);
		if((s & Sphylink) == 0)
			break;
		setstate(d, Dnew);
	case Dnew:
		newdrive(d);
		break;
	case Dnopower:
		print("%s: nopower\n", dnam(d));
		break;
	}
}

static int
havehp(void*)
{
	return hpevent != 0;
}

static void
hp(void)
{
	Lock l;

	ilock(&l);
	if(hpevent == 0){
		hpevent = 1;
		wakeup(&hprendez);
	}
	iunlock(&l);
}

static void
hpproc(void*)
{
	int i;

	for(;;){
		for(i = 0; i < Nctlrdrv; i++)
			checkdrive(drive + i);
		tsleep(&hprendez, havehp, 0, Nms);
		hpevent = 0;
	}
}

static int
enable(SDev *s)
{
	Ctlr *c;
	Drive *d;
	int i;

	c = s->ctlr;

	/* config arbiter */
	c->arb[Hconf] = 	0xff | Dmanobs | Edmanobs | Prdbnobs |
		P0coaldis | P1coaldis;
//	memset(c->arb + Win0ctl, 0, 4*4*2);
	setaddrwin(c, 0, Attrcs0, 0);
	setaddrwin(c, 1, Attrcs1, 256*MB);

	/* and the ports */
	for(i = 0; i < Nctlrdrv; i++){
		d = c->drive[i];
		d->reg[Serror] = 0x07ffffff;
		d->reg[Icfg] = 1*Pllclk | Icfgmagic | Giien | Txemph | Clkdeten;
		linkrst(d);
	}

	c->arb[Hmicm] = I0err | I1err;
	intrenable(Irqlo, IRQ0sata, interrupt, c, "kwsata");

	kproc("kwhp", hpproc, 0);
	return 1;
}

static SDev*
pnp(void)
{
	int i, j, drvno;
	uintmem pa;
	Ctlr *c;
	Drive *d;
	SDev *s;

	for(i = 0; i < Nctlr; i++){
		c = ctlr + i;
		s = sdev + i;
		c->arb = (uint*)(Arb0 + i*Arbstep);
		for(j = 0; j < Nctlrdrv; j++){
			drvno = i*Nctlrdrv + j;
			d = drive + drvno;
			c->drive[j] = d;
			d->c = c;
			d->drvno = drvno;
//			d->edmaon = ~0;
			d->reg = (uint*)(Port0 + drvno*Portstep);
			d->mem = mallocalign(1024*2, 1<<10, 0, 0);
			d->ereq = (Ereq*)d->mem;
			d->ersp = (Ersp*)(d->mem+1024);
			pa = PADDR(d->ereq);
			d->reg[Ereqhi] = (uvlong)pa >> 32;
			d->reg[Ereqlo] = (uint)pa;
			d->reg[Ereqtl] = 0;
			pa = PADDR(d->ersp);
			d->reg[Ersphi] = (uvlong)pa >> 32;
			nextrsp(d);
			d->reg[Ersphd] = 0;
		}
		s->nunit = Nctlrdrv;
		s->ifc = &sdkwifc;
		s->idno = '0' + i;
		s->ctlr = c;
		c->sdev = s;
		sdadddevs(s);
		print("#S/%s: sata ii %d ports\n", s->name, Nctlrdrv);
	}
	return nil;
}

SDifc sdkwifc = {
	"kw",

	pnp,
	nil,		/* legacy */
	enable,
	disable,

	verify,
	online,
	rio,
	rctl,
	wctl,

	bio,
	nil,		/* probe */
	nil,		/* clear */
	nil,
	nil,
	ataio,
};
