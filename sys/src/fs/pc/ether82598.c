/*
 * intel 10gbe pcie driver
 * copyright © 2007—2012, coraid, inc.
 */
#include "all.h"
#include "io.h"
#include "../ip/ip.h"
#include "etherif.h"
#include "mem.h"

enum{
	/* general */
	Ctrl		= 0x00000/4,	/* Device Control */
	Status		= 0x00008/4,	/* Device Status */
	Ctrlext		= 0x00018/4,	/* Extended Device Control */
	Esdp		= 0x00020/4,	/* extended sdp control */
	Esodp		= 0x00028/4,	/* extended od sdp control */
	Ledctl		= 0x00200/4,	/* led control */
	Tcptimer		= 0x0004c/4,	/* tcp timer */
	Ecc		= 0x110b0/4,	/* errata ecc control magic */

	/* nvm */
	Eec		= 0x10010/4,	/* eeprom/flash control */
	Eerd		= 0x10014/4,	/* eeprom read */
	Fla		= 0x1001c/4,	/* flash access */
	Flop		= 0x1013c/4,	/* flash opcode */
	Grc		= 0x10200/4,	/* general rx control */

	/* interrupt */
	Icr		= 0x00800/4,	/* interrupt cause read */
	Ics		= 0x00808/4,	/* " set */
	Ims		= 0x00880/4,	/* " mask read/set */
	Imc		= 0x00888/4,	/* " mask clear */
	Iac		= 0x00810/4,	/* " auto clear */
	Iam		= 0x00890/4,	/* " auto mask enable */
	Itr		= 0x00820/4,	/* " throttling rate (0-19) */
	Ivar		= 0x00900/4,	/* " vector allocation regs. */
	/*msi interrupt */
	Msixt		= 0x0000/4,	/* msix table (bar3) */
	Msipba		= 0x2000/4,	/* msix pending bit array (bar3) */
	Pbacl		= 0x11068/4,	/* pba clear */
	Gpie		= 0x00898/4,	/* general purpose int enable */

	/* flow control */
	Pfctop		= 0x03008/4,	/* priority flow ctl type opcode */
	Fcttv		= 0x03200/4,	/* " transmit timer value (0-3) */
	Fcrtl		= 0x03220/4,	/* " rx threshold low (0-7) +8n */
	Fcrth		= 0x03260/4,	/* " rx threshold high (0-7) +8n */
	Rcrtv		= 0x032a0/4,	/* " refresh value threshold */
	Tfcs		= 0x0ce00/4,	/* " tx status */

	/* rx dma */
	Rbal		= 0x01000/4,	/* rx desc base low (0-63) +0x40n */
	Rbah		= 0x01004/4,	/* " high */
	Rdlen		= 0x01008/4,	/* " length */
	Rdh		= 0x01010/4,	/* " head */
	Rdt		= 0x01018/4,	/* " tail */
	Rxdctl		= 0x01028/4,	/* " control */

	Srrctl		= 0x02100/4,	/* split and replication rx ctl. */
	Dcarxctl		= 0x02200/4,	/* rx dca control */
	Rdrxctl		= 0x02f00/4,	/* rx dma control */
	Rxpbsize		= 0x03c00/4,	/* rx packet buffer size */
	Rxctl		= 0x03000/4,	/* rx control */
	Dropen		= 0x03d04/4,	/* drop enable control */

	/* rx */
	Rxcsum		= 0x05000/4,	/* rx checksum control */
	Rfctl		= 0x04008/4,	/* rx filter control */
	Mta		= 0x05200/4,	/* multicast table array (0-127) */
	Ral		= 0x05400/4,	/* rx address low */
	Rah		= 0x05404/4,
	Psrtype		= 0x05480/4,	/* packet split rx type. */
	Vfta		= 0x0a000/4,	/* vlan filter table array. */
	Fctrl		= 0x05080/4,	/* filter control */
	Vlnctrl		= 0x05088/4,	/* vlan control */
	Msctctrl		= 0x05090/4,	/* multicast control */
	Mrqc		= 0x05818/4,	/* multiple rx queues cmd */
	Vmdctl		= 0x0581c/4,	/* vmdq control */
	Imir		= 0x05a80/4,	/* immediate irq rx (0-7) */
	Imirext		= 0x05aa0/4,	/* immediate irq rx ext */
	Imirvp		= 0x05ac0/4,	/* immediate irq vlan priority */
	Reta		= 0x05c00/4,	/* redirection table */
	Rssrk		= 0x05c80/4,	/* rss random key */

	/* tx */
	Tdbal		= 0x06000/4,	/* tx desc base low +0x40n */
	Tdbah		= 0x06004/4,	/* " high */
	Tdlen		= 0x06008/4,	/* " len */
	Tdh		= 0x06010/4,	/* " head */
	Tdt		= 0x06018/4,	/* " tail */
	Txdctl		= 0x06028/4,	/* " control */
	Tdwbal		= 0x06038/4,	/* " write-back address low */
	Tdwbah		= 0x0603c/4,

	Dtxctl		= 0x04a80/4,	/* tx dma control !82598 */
	Tdcatxctrl	= 0x07200/4,	/* tx dca register (0-15) */
	Tipg		= 0x0cb00/4,	/* tx inter-packet gap */
	Txpbsize		= 0x0cc00/4,	/* tx packet-buffer size (0-15) */

	/* mac */
	Hlreg0		= 0x04240/4,	/* highlander control reg 0 */
	Hlreg1		= 0x04244/4,	/* highlander control reg 1 (ro) */
	Msca		= 0x0425c/4,	/* mdi signal cmd & addr */
	Msrwd		= 0x04260/4,	/* mdi single rw data */
	Mhadd		= 0x04268/4,	/* mac addr high & max frame */
	Pcss1		= 0x04288/4,	/* xgxs status 1 */
	Pcss2		= 0x0428c/4,
	Xpcss		= 0x04290/4,	/* 10gb-x pcs status */
	Serdesc		= 0x04298/4,	/* serdes control */
	Macs		= 0x0429c/4,	/* fifo control & report */
	Autoc		= 0x042a0/4,	/* autodetect control & status */
	Links		= 0x042a4/4,	/* link status */
	Autoc2		= 0x042a8/4,
};

enum{
	/* Ctrl */
	Rst		= 1<<26,	/* full nic reset */

	/* Txdctl */
	Ten		= 1<<25,

	/* Dtxctl */
	Den		= 1<<0,

	/* Fctrl */
	Rfce		= 1<<15,	/* rcv flow control enable */
	Dpf		= 1<<13,	/* discard pause frames */
	Bam		= 1<<10,	/* broadcast accept mode */
	Upe 		= 1<<9,	/* unicast promiscuous */
	Mpe 		= 1<<8,	/* multicast promiscuous */

	/* Rxdctl */
	Pthresh		= 0,		/* prefresh threshold shift in bits */
	Hthresh		= 8,		/* host buffer minimum threshold " */
	Wthresh		= 16,		/* writeback threshold */
	Renable		= 1<<25,

	/* Rxctl */
	Rxen		= 1<<0,
	Dmbyps		= 1<<1,

	/* Rdrxctl */
	Rdmt½		= 0,
	Rdmt¼		= 1,
	Rdmt⅛		= 2,

	/* Rxcsum */
	Ippcse		= 1<<12,	/* ip payload checksum enable */

	/* Eerd */
	EEstart		= 1<<0,	/* Start Read */
	EEdone		= 1<<1,	/* Read done */

	/* interrupts */
	Irx0		= 1<<0,	/* driver defined */
	Itx0		= 1<<1,	/* driver defined */
	Lsc		= 1<<20,	/* link status change */
	Ioc		= 1<<31,	/* other cause */

	/* Links */
	Lnkup	= 1<<30,
	Lnkspd8	= 1<<29,
	Lnkspd9	= 3<<28,

	/* Hlreg0 */
	Txcrcen	= 1<<0,
	Jumboen	= 1<<2,

	/* Ivar */
	Ivtx	= 1|1<<7,		/* transmit interrupt */
	Ivrx	= 0|1<<7,		/* receive interrupt */
};

typedef	struct	Ctlr	Ctlr;
typedef	struct	Ctlrtype	Ctlrtype;
typedef	struct	Rd	Rd;
typedef	struct	Rbpool	Rbpool;
typedef	struct	Stat	Stat;
typedef	struct	Td	Td;

enum {
	i82598,
	i82599,
	x540,
	Nctlrtype,
};

struct Ctlrtype {
	int	type;
	int	mtu;
	int	flag;
	char	*name;
};

enum {
	Fphyoc		= 1<<0,	/* phy link needs other cause interrupt */
	Fsplitivar	= 1<<1,	/* tx and rx use different ivar entries */
	Fphyspd		= 1<<2,	/* phy speed useful (part supports <10gbe) */
	Ftxctl		= 1<<3,	/* part has txctl register */
};

/* real mtu is 12k.  use standard 9k to save memory */
static Ctlrtype cttab[Nctlrtype] = {
	i82598,	9*1024,		Fsplitivar|Fphyoc,	"i82598",
	i82599,	9*1024,		Fphyspd|Ftxctl,		"i82599",
	x540,	9*1024,		Fphyspd|Ftxctl,		"x540",
};

/* status */
enum{
	Pif	= 1<<7,	/* past exact filter (sic) */
	Ipcs	= 1<<6,	/* ip checksum calcuated */
	L4cs	= 1<<5,	/* layer 2 */
	Tcpcs	= 1<<4,	/* tcp checksum calcuated */
	Vp	= 1<<3,	/* 802.1q packet matched vet */
	Ixsm	= 1<<2,	/* ignore checksum */
	Reop	= 1<<1,	/* end of packet */
	Rdd	= 1<<0,	/* descriptor done */
};

struct Rd {
	u32int	addr[2];
	u16int	length;
	u16int	cksum;
	uchar	status;
	uchar	errors;
	u16int	vlan;
};

enum{
	/* Td cmd */
	Rs	= 1<<3,
	Ic	= 1<<2,
	Ifcs	= 1<<1,
	Teop	= 1<<0,

	/* Td status */
	Tdd	= 1<<0,
};

struct Td {
	u32int	addr[2];
	u16int	length;
	uchar	cso;
	uchar	cmd;
	uchar	status;
	uchar	css;
	u16int	vlan;
};

enum{
	Factive	= 1<<0,
	Fstarted	= 1<<1,
};

typedef void (*Freefn)(Msgbuf*);

struct Ctlr {
	Pcidev	*p;
	uintmem	port;
	u32int	*reg;
	uchar	flag;
	uint	poolno;
	Rbpool	*pool;
	int	nrd, ntd, nrb, rbsz;
	QLock	slock, alock, tlock;
	Rendez	lrendez, trendez, rrendez;
	uint	im, lim, rim, xtim;
	Lock	imlock;
	char	*alloc;
	Rd	*rdba;
	Msgbuf	**rb;
	uint	rdt, rdfree;
	Td	*tdba;
	uint	tdh, tdt;
	Msgbuf	**tb;
	uchar	ra[Easize];
	uchar	mta[128];
//	uvlong	stats[nelem(stattab)];
	int	type;
	uint	speeds[4];
	uint	nobufs;

	char	tname[28];
	char	rname[28];
};

struct Rbpool {
	union {
		struct {
			Lock;
			Msgbuf	*b;
			uint	nstarve;
			uint	nwakey;
			uint	starve;
			Rendez;
		};
		uchar pad[64];		/* cacheline */
	};
	union {
		struct {
			Msgbuf	*x;
			uint	nfast;
			uint	nslow;
		};
		uchar pad[64];		/* cacheline */
	};
};

/* tweakable parameters */
enum{
	Nrd	= 256,
	Ntd	= 256,
	Nrb	= 2048,
	Nctlr	= 8,
	Rbalign	= 8,		/* ideally, 4k */
};

static	Ctlr	*ctlrtab[Nctlr];
static	Lock	rblock[Nctlr];
static	Rbpool	rbtab[Nctlr];
static	int	nctlr;

char*
cname(Ctlr *c)
{
	return cttab[c->type].name;
}

static void
im(Ctlr *c, int i)
{
	ilock(&c->imlock);
	c->im |= i;
	c->reg[Ims] = c->im;
	iunlock(&c->imlock);
}

static int
icansleep(void *v)
{
	Rbpool *p;
	int r;

	p = v;
	ilock(p);
	r = p->starve == 0;
	iunlock(p);

	return r;
}

static Msgbuf*
rballoc(Rbpool *p)
{
	Msgbuf *b;

	for(;;){
		if((b = p->x) != nil){
			p->nfast++;
			p->x = b->next;
			b->next = nil;
			b->flags &= ~FREE;
			return b;
		}

		ilock(p);
		b = p->b;
		p->b = nil;
		if(b == nil){
			p->starve = 1;
			p->nstarve++;
			iunlock(p);
			return nil;
		}
		p->nslow++;
		iunlock(p);
		p->x = b;
	}
}

static void
rbfree(Msgbuf *b, int t)
{
	Rbpool *p;

	p = rbtab + t;
	b->data = (uchar*)ROUNDUP((uintptr)b->xdata, Rbalign);
	b->count = 0;
	b->flags = FREE;

	ilock(p);
	b->next = p->b;
	p->b = b;
	if(p->starve){
		if(1)
			print("wakey %d; %d %d\n", t, p->nstarve, p->nwakey);
		p->nwakey++;
		p->starve = 0;
		wakeup(p);
	}
	iunlock(p);
}

static void
rbfree0(Msgbuf *b)
{
	rbfree(b, 0);
}

static void
rbfree1(Msgbuf *b)
{
	rbfree(b, 1);
}

static void
rbfree2(Msgbuf *b)
{
	rbfree(b, 2);
}

static void
rbfree3(Msgbuf *b)
{
	rbfree(b, 3);
}

static void
rbfree4(Msgbuf *b)
{
	rbfree(b, 4);
}

static void
rbfree5(Msgbuf *b)
{
	rbfree(b, 5);
}

static void
rbfree6(Msgbuf *b)
{
	rbfree(b, 6);
}

static void
rbfree7(Msgbuf *b)
{
	rbfree(b, 7);
}

static Freefn freetab[Nctlr] = {
	rbfree0,
	rbfree1,
	rbfree2,
	rbfree3,
	rbfree4,
	rbfree5,
	rbfree6,
	rbfree7,
};

#define Next(x, m)	(((x)+1) & (m))
static int
cleanup(Ctlr *c, int tdh)
{
	Msgbuf *b;
	uint m, n;

	m = c->ntd-1;
	while(c->tdba[n = Next(tdh, m)].status&Tdd){
		tdh = n;
		b = c->tb[tdh];
		c->tb[tdh] = 0;
		mbfree(b);
		c->tdba[tdh].status = 0;
	}
	return tdh;
}

static void
transmit(Ether *e)
{
	uint i, m, tdt, tdh;
	Ctlr *c;
	Msgbuf *b;
	Td *t;

	c = e->ctlr;
//	qlock(&c->tlock);
	if(!canqlock(&c->tlock)){
		im(c, Itx0);
		return;
	}
	tdh = c->tdh = cleanup(c, c->tdh);
	tdt = c->tdt;
	m = c->ntd-1;
	for(i = 0; i<8; i++){
		if(Next(tdt, m) == tdh){
			im(c, Itx0);
			break;
		}
		if((b = etheroq(e)) == nil)
			break;
		t = c->tdba+tdt;
		t->addr[0] = Pciwaddrl(b->data);
		t->addr[1] = Pciwaddrh(b->data);
		t->length = b->count;
		t->cmd = Rs|Ifcs|Teop;
		c->tb[tdt] = b;
		tdt = Next(tdt, m);
	}
	if(i){
		c->tdt = tdt;
		coherence();
		c->reg[Tdt] = tdt;
	}
	qunlock(&c->tlock);
}

static int
xtim(void *c)
{
	return ((Ctlr*)c)->xtim != 0;
}

static void
tproc(void)
{
	Ether *e;
	Ctlr *c;

	e = u->arg;
	c = e->ctlr;
loop:
	sleep(&c->trendez, xtim, c);		/* transmit kicks us */
	c->xtim = 0;
	transmit(e);
	goto loop;
}

static void
rxinit(Ctlr *c)
{
	Msgbuf *b;
	int i;

	c->reg[Rxctl] &= ~Rxen;
	for(i = 0; i<c->nrd; i++){
		b = c->rb[i];
		c->rb[i] = 0;
		if(b)
			mbfree(b);
	}
	c->rdfree = 0;

	c->reg[Fctrl] |= Bam|Rfce|Dpf;
	c->reg[Rxcsum] |= Ipcs;
	c->reg[Srrctl] = (c->rbsz+1023)/1024;
	c->reg[Mhadd] = c->rbsz<<16;
	c->reg[Hlreg0] |= Txcrcen|Jumboen;

	c->reg[Rbal] = PCIWADDR(c->rdba);
	c->reg[Rbah] = 0;
	c->reg[Rdlen] = c->nrd*sizeof(Rd);
	c->reg[Rdh] = 0;
	c->reg[Rdt] = c->rdt = 0;

	c->reg[Rdrxctl] = Rdmt¼;
	c->reg[Rxdctl] = 8<<Wthresh|8<<Pthresh|4<<Hthresh|Renable;
	c->reg[Rxctl] |= Rxen|Dmbyps;
}

static int
replenish(Ctlr *c, uint rdh, int maysleep)
{
	int rdt, m, i;
	Msgbuf *b;
	Rd *r;
	Rbpool *p;

	m = c->nrd-1;
	i = 0;
	p = c->pool;
	for(rdt = c->rdt; Next(rdt, m) != rdh; rdt = Next(rdt, m)){
		r = c->rdba+rdt;
		while((b = rballoc(c->pool)) == nil){
			c->nobufs++;
			if(maysleep == 0)
				goto nobufs;
			if(1){
				print("%s:%d: starve\n", cname(c), c->poolno);
			}
			sleep(p, icansleep, p);
		}
		c->rb[rdt] = b;
		r->addr[0] = PCIWADDR(b->data);
		r->addr[1] = 0; /* Pciwaddrh(b->data); */
		r->status = 0;
		c->rdfree++;
		i++;
	}
nobufs:
	if(i){
		coherence();
		c->reg[Rdt] = c->rdt = rdt;
	}
	if(rdt == rdh)
		return -1;
	return 0;
}

static int
rim(void *v)
{
	return ((Ctlr*)v)->rim != 0;
}

static void
rproc(void)
{
	Ether *e;
	Ctlr *c;
	Msgbuf *b;
	Rd *r;
	uint m, rdh;

	e = u->arg;
	c = e->ctlr;
	m = c->nrd-1;
	rdh = 0;
loop:
	replenish(c, rdh, 1);
	im(c, Irx0);
	sleep(&c->rrendez, rim, c);
loop1:
	c->rim = 0;
	if(c->nrd-c->rdfree >= 16)
	if(replenish(c, rdh, 0) == -1)
		goto loop;
	r = c->rdba+rdh;
	if(!(r->status&Rdd))
		goto loop;
	b = c->rb[rdh];
	c->rb[rdh] = 0;
	b->count = r->length;
	if(!(r->status&Ixsm)){
		if(r->status&Ipcs)
			b->flags |= Bipck;
		if(r->status&Tcpcs)
			b->flags |= Btcpck|Budpck;
	//	b->checksum = r->cksum;
	}
	r->status = 0;
	etheriq(e, b);
	c->rdfree--;
	rdh = Next(rdh, m);
	goto loop1;
}

static void
promiscuous(void *a, int on)
{
	Ether *e;
	Ctlr *c;

	e = a;
	c = e->ctlr;
	if(on)
		c->reg[Fctrl] |= Upe|Mpe;
	else
		c->reg[Fctrl] &= ~(Upe|Mpe);
}

static void
multicast(void *a, uchar *ea, int on)
{
	Ether *e;
	Ctlr *c;
	int b, i;

	e = a;
	c = e->ctlr;

	i = ea[5]>>1;
	b = (ea[5]&1)<<4|ea[4]>>4;
	b = 1<<b;
	if(on)
		c->mta[i] |= b;
	else
		c->mta[i] &= ~b;
	c->reg[Mta+i] = c->mta[i];
}

static int
detach(Ctlr *c)
{
	int i;

	c->reg[Imc] = ~0;
	c->reg[Ctrl] |= Rst;
	for(i = 0; i < 100; i++){
		delay(1);
		if((c->reg[Ctrl]&Rst) == 0)
			goto good;
	}
	return -1;
good:
	/* errata */
	delay(50);
	c->reg[Ecc] &= ~(1<<21|1<<18|1<<9|1<<6);

	/* not cleared by reset; kill it manually. */
	for(i = 1; i<16; i++)
		c->reg[Rah] &= ~(1<<31);
	for(i = 0; i<128; i++)
		c->reg[Mta+i] = 0;
	for(i = 1; i<640; i++)
		c->reg[Vfta+i] = 0;
	return 0;
}

static void
shutdown(Ether *e)
{
	detach(e->ctlr);
}

/* ≤ 20ms */
static ushort
eeread(Ctlr *c, int i)
{
	c->reg[Eerd] = EEstart|i<<2;
	while((c->reg[Eerd]&EEdone) == 0)
		;
	return c->reg[Eerd]>>16;
}

static int
eeload(Ctlr *c)
{
	ushort u, v, p, l, i, j;

	if((eeread(c, 0)&0xc0) != 0x40)
		return -1;
	u = 0;
	for(i = 0; i < 0x40; i++)
		u +=  eeread(c, i);
	for(i = 3; i < 0xf; i++){
		if(c->type == x540 && (i == 4 || i == 5))
			continue;
		p = eeread(c, i);
		l = eeread(c, p++);
		if((int)p+l+1 > 0xffff)
			continue;
		for(j = p; j < p+l; j++)
			u += eeread(c, j);
	}
	if(u != 0xbaba)
		return -1;
	if(c->reg[Status]&1<<3)
		u = eeread(c, 10);
	else
		u = eeread(c, 9);
	u++;
	for(i = 0; i<Easize;){
		v = eeread(c, u+i/2);
		c->ra[i++] = v;
		c->ra[i++] = v>>8;
	}
	c->ra[5] += (c->reg[Status]&0xc)>>2;
	return 0;
}

static int
reset(Ctlr *c)
{
	uchar *p;
	int i;

	if(detach(c)){
		print("%s: reset timeout\n", cname(c));
		return -1;
	}
	if(eeload(c)){
		print("%s: eeprom failure\n", cname(c));
		return -1;
	}
	p = c->ra;
	c->reg[Ral] = p[3]<<24|p[2]<<16|p[1]<<8|p[0];
	c->reg[Rah] = p[5]<<8|p[4]|1<<31;

//	readstats(c);
//	for(i = 0; i<nelem(c->stats); i++)
//		c->stats[i] = 0;

	c->reg[Ctrlext] |= 1<<16;
	/* make some guesses for flow control */
	c->reg[Fcrtl] = 0x10000|1<<31;
	c->reg[Fcrth] = 0x40000|1<<31;
	c->reg[Rcrtv] = 0x6000;

	/* configure interrupt mapping (don't ask) */
	if(cttab[c->type].flag & Fsplitivar){
		c->reg[Ivar+0] = Ivrx;
		c->reg[Ivar+64/4] = Ivtx;
//		c->reg[Ivar+97/4] = (2|1<<7)<<8*(97%4);
	}else
		c->reg[Ivar+0] = Ivtx<<8 | Ivrx;

	/* interrupt throttling goes here. */
	for(i = Itr; i<Itr+20; i++)
		c->reg[i] = 128;		/* ¼µs intervals */
	c->reg[Itr+Itx0] = 256;
	return 0;
}

static void
txinit(Ctlr *c)
{
	Msgbuf *b;
	int i;

	c->reg[Txdctl] = 16<<Wthresh|16<<Pthresh;
	for(i = 0; i<c->ntd; i++){
		b = c->tb[i];
		c->tb[i] = 0;
		if(b)
			mbfree(b);
	}
	memset(c->tdba, 0, c->ntd*sizeof(Td));
	c->reg[Tdbal] = PCIWADDR(c->tdba);
	c->reg[Tdbah] = 0;
	c->reg[Tdlen] = c->ntd*sizeof(Td);
	c->reg[Tdh] = 0;
	c->reg[Tdt] = 0;
	c->tdh = c->ntd-1;
	c->tdt = 0;
	if(cttab[c->type].flag & Ftxctl)
		c->reg[Dtxctl] |= Den;
	c->reg[Txdctl] |= Ten;
}

static void
attach(Ether *e)
{
	Ctlr *c;
	int t;

	c = e->ctlr;
	qlock(&c->alock);
	if(c->alloc){
		qunlock(&c->alock);
		return;
	}

	c->nrd = Nrd;
	c->ntd = Ntd;
	t = c->nrd*sizeof *c->rdba+255;
	t += c->ntd*sizeof *c->tdba+255;
	t += (c->ntd+c->nrd)*sizeof(Msgbuf*);
	c->alloc = ialloc(t, 0);
	qunlock(&c->alock);

	c->rdba = (Rd*)ROUNDUP((uintptr)c->alloc, 256);
	c->tdba = (Td*)ROUNDUP((uintptr)(c->rdba+c->nrd), 256);
	c->rb = (Msgbuf**)(c->tdba+c->ntd);
	c->tb = (Msgbuf**)(c->rb+c->nrd);

	mballocpool(Nrb, c->rbsz+Rbalign, Rbalign, Mbeth10gbebg, freetab[c->poolno]);

	rxinit(c);
	txinit(c);

	sprint(c->rname, "#l%dr", e->ctlrno);
	userinit(rproc, e, c->rname);
	sprint(c->tname, "#l%dt", e->ctlrno);
	userinit(tproc, e, c->tname);
}

static void
interrupt(Ureg*, void *v)
{
	Ether *e;
	Ctlr *c;
	int icr, im;

	e = v;
	c = e->ctlr;
	ilock(&c->imlock);
	c->reg[Imc] = ~0;
	im = c->im;
	while(icr = c->reg[Icr]&c->im){
		if(icr&Lsc){
			im &= ~Lsc;
			c->lim = icr&Lsc;
			wakeup(&c->lrendez);
		}
		if(icr&Irx0){
			im &= ~Irx0;
			c->rim = icr&Irx0;
			wakeup(&c->rrendez);
		}
		if(icr&Itx0){
			im &= ~Itx0;
			c->xtim = icr&Itx0;
			wakeup(&c->trendez);
		}
	}
	c->reg[Ims] = c->im = im;
	iunlock(&c->imlock);
}

static void
hbafixup(Pcidev *p)
{
	uint i;

	i = pcicfgr32(p, PciSVID);
	if((i & 0xffff) == 0x1b52 && p->did == 1)
		p->did = i>>16;
}

static void
scan(void)
{
	char *name;
	uintmem io;
	int type;
	void *mem;
	Ctlr *c;
	Pcidev *p;

	p = 0;
	while(p = pcimatch(p, 0x8086, 0)){
		hbafixup(p);
		switch(p->did){
		case 0x10c6:	/* 82598 af dual port */
		case 0x10c7:	/* 82598 af single port */
		case 0x10b6:	/* 82598 backplane */
		case 0x10dd:	/* 82598 at cx4 */
		case 0x10ec:	/* 82598 at cx4 */
			type = i82598;
			break;
		case 0x10f7:	/* 82599 kx/kx4 */
		case 0x10f8:	/* 82599 backplane */
		case 0x10f9:	/* 82599 cx4 */
		case 0x10fb:	/* 82599 sfi/sfp+ */
		case 0x10fc:	/* 82599 xaui */
		case 0x151c:	/* 82599 base t kx/kx4 “niantic” */
			type = i82599;
			break;
		case 0x1528:	/* x540-at2 “twinville” */
			type = x540;
			break;
		default:
			continue;
		}
		name = cttab[type].name;
		if(nctlr == nelem(ctlrtab)){
			print("%s: %τ: too many controllers\n", name, p->tbdf);
			return;
		}
		io = p->mem[0].bar&~0xf;
		mem = vmap(io, p->mem[0].size);
		if(mem == 0){
			print("%s: %τ: cant map bar\n", name, p->tbdf);
			continue;
		}
		c = ialloc(sizeof *c, 0);
		c->p = p;
		c->port = io;
		c->reg = (u32int*)mem;
		c->rbsz = cttab[type].mtu;
		c->type = type;
		if(reset(c)){
			print("%s: %τ: cant reset\n", name, p->tbdf);
		//	free(c);
		//	vunmap(mem, p->mem[0].size);
			continue;
		}
		pcisetbme(p);
		c->poolno = nctlr;
		c->pool = rbtab + c->poolno;
		ctlrtab[nctlr++] = c;
	}
}

int
ethercfgmatch(Ether *e, Pcidev *p, uintmem port)
{
	if(e->port == 0 || e->port == port)
	if(1 || e->tbdf == BUSUNKNOWN || p == nil || e->tbdf == p->tbdf){
		return 0;
	}
	return -1;
}

int
i82598pnp(Ether *e)
{
	Ctlr *c;
	int i;

	if(nctlr == 0)
		scan();
	for(i = 0; i<nctlr; i++){
		c = ctlrtab[i];
		if(c == 0 || c->flag&Factive)
			continue;
		if(ethercfgmatch(e, c->p, c->port) == 0)
			goto found;
	}
	return -1;
found:
	c->flag |= Factive;
	e->ctlr = c;
	e->port = (uintptr)c->reg;
	e->irq = c->p->intl;
	e->tbdf = c->p->tbdf;
	e->mbps = 10000;
	e->ifc.maxmtu = c->rbsz;
	memmove(e->ea, c->ra, Easize);
//	e->arg = e;
	e->attach = attach;
	e->interrupt = interrupt;
	e->transmit = transmit;

	return 0;
}
