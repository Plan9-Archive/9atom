#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "../k10/etherif.h"

enum {
	/* tunable parameters */
	Nctlr	= 8,
	Rbsz	= 9600,

	/* controller flags */
	Factive	= 1<<0,

	/* controler parameters */
	Rbalign	= 16,
	Ctxalign	= 4096,

	/* ramrod commands (slow path) */
	Rportsetup	= 80,			/* "leading" connection to port */
	Rportclient	= 85,			/* addn'l connections */
	Rstats		= 90,			/* force update; on leading cid */
	Rupdate		= 100,
	Rhalt		= 105,
	Rsetmac		= 110,			/* set uc/mc/bc macs */
	Rcfcdel		= 115,
	Rportdel	= 120,
	Rethfwd		= 125,
	
	/* shared region */
	Shaddr		= 0xa2b4/4,		/* shared memory address */

	Bond		= 0xa400/4,
	Metal		= 0xa404/4,
	Chipn		= 0xa408/4,
	Rev		= 0xa40c/4,
	Shaddr2		= 0xa460/4,		/* second shared address */

	/* hc registers */
	Hconf0		= 0x108000/4,
	Hirqack0		= 0x108180/4,
	Hprodupt0	= 0x108184/4,
	Hattnupd0	= 0x108188/4,
	Hattnset0	= 0x10818c/4,
	Hattnclr0	= 0x108190/4,
	Hcoal0		= 0x108194/4,
	H1isrmask0	= 0x108198/4,
	H1isr0		= 0x10819c/4,
	
	Hport1off	= 0x108200/4 - Hirqack0,
};

typedef	struct	Ctlr	Ctlr;
typedef	struct	Cttab	Cttab;

struct Cttab {
	int	type;
	char	*name;
	uint	flags;
	uint	shmsz;
};

struct Ctlr {
	Pcidev	*p;
	Cttab	*type;
	uint	flag;
	uint	rbsz;

	uchar	ra[Eaddrlen];
	uintmem	port;
	u32int	*reg;
	void	*db;
};

enum {
	b57710,
	b57711,
	b57712,
	Nctlrtype,
};

static	Cttab	cttab[Nctlrtype] = {
	b57710,	"b57710",	0,	0x6dc,
	b57711,	"b57711",	0,	0x74e,
	b57712,	"b57712",	0,	0x734,
};

static	Ctlr	*ctlrtab[Nctlr];
static	int	nctlr;
static	char	name[] = "b57711";

static int
detach(Ctlr *c)
{
	USED(c);
	return -1;
}

static void
shutdown(Ether *e)
{
	detach(e->ctlr);
}

static void
promiscuous(void *a, int on)
{
	Ether *e;
	Ctlr *c;

	e = a;
	c = e->ctlr;
	if(on)
		USED(c);
	else
		USED(c);
}

static void
multicast(void *a, uchar *ea, int on)
{
	Ctlr *c;
	Ether *e;

	e = a;
	c = e->ctlr;
	print("%s: %E %d\n", c->type->name, ea, on);
}

static void
interrupt(Ureg*, void *v)
{
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;
	iprint("%s irq\n", c->type->name);
}

static long
ifstat(Ether *e, void *a, long n, usize offset)
{
	char *s, *p, *q;
	Ctlr *c;

	c = e->ctlr;

	p = s = malloc(READSTR);
	q = p+READSTR;

	seprint(p, q, "type: %s\n", c->type->name);
	n = readstr(offset, a, n, s);
	free(s);

	return n;
}


static long
ctl(Ether *, void *, long)
{
	error(Ebadarg);
	return -1;
}

static void
attach(Ether *e)
{
	Ctlr *c;

	c = e->ctlr;
	print("#l%d: %s: attach\n", e->ctlrno, c->type->name);
}

static int
reset(Ctlr *c)
{
	u32int r, r2;

	r = c->reg[Shaddr];
	if(r == 0 || r == 0xf){
		print("%s: %T: firmware not loaded\n", name, c->p->tbdf);
		return -1;
	}
	print("%s: %T: shddr %.8ux\n", name, c->p->tbdf, r);
	r >>= 2;

	r2 = c->reg[Shaddr2];
	if(c->type->type != b57712 && (BUSFNO(c->p->tbdf)&1) == 1)
		r2 = c->reg[Shaddr2+1];

	print("%s: %T: shddr2 %.8ux	sz %d\n", name, c->p->tbdf, r2, c->reg[r2/4]);
	r2 >>= 2;

	/* purely debugging junk.  remove. */
	print("	v0	%.8ux\n",	c->reg[r + 0]);
	print("	v1	%.8ux\n",	c->reg[r + 1]);
	print("	bc	%.8ux\n",	c->reg[r + 2]);
	print("	mfcfg	%.8ux\n",	c->reg[r + c->type->shmsz/4 + 11*8]);	/* 11*4 bytes/fun mb * 8 mb */

	for(uint i = 0; i < 3; i++){
		print("		");
		for(uint j = 0; j < 6; j++)
			print("%.8ux ", c->reg[r + c->type->shmsz/4 + 11*4 + (8+1) + i*6 + j]);
		print("\n");
	}

	for(uint i = 0; i < 0x2000/4; i++){
		if(c->reg[r+i] ==  0x0010)
			print("** %d: ea %.4ux%.8ux\n", i, c->reg[r+i], c->reg[r+i+1]);
	}
	for(uint i = 0xa0000/4; i < 0xa0000/4 + 0x20000/4; i++){
//		if(c->reg[i] ==  0x0010)
		if((c->reg[i]&0xffff) == 0x0010 && (c->reg[i+1]&~3) == 0x18f3a59c)
			print("@@ %#ux: cfg %.8ux ea %.4ux%.8ux\n", 4*i, c->reg[i-1], c->reg[i], c->reg[i+1]);
	}

	static uint tab[] = {
		0x80c,
		0x824,
		0x83c,
		0x854,
		0x86c,
		0x884,
		0x89c,
		0x8b4,
	};
	uint *b;
	for(uint i = 0; i < nelem(tab); i++){
		b = c->reg + r + tab[i]/4;
		print("!! %.8ux %.8ux %.8ux	 %.8ux  %.8ux\n", b[-1], b[0], b[1], b[2], b[3]);
	}

	print("	Chipn	%.8ux\n", c->reg[Chipn]);
	print("	hconf0	%.8ux\n", c->reg[Hconf0]);
	print("	flsz	%.8ux\n", 128*1024<<(c->reg[0x8642c/4]&7));
	return 0;
}

static void
scan(void)
{
	int type;
	uintmem mempa, dbpa;
	void *mem, *db;
	Ctlr *c;
	Pcidev *p;

	for(p = nil; p = pcimatch(p, 0x14e4, 0); ){
		switch(p->did){
		default:
			continue;
		case 0x164e:		/* BCM57710 [Everest] */
			type = b57710;
			break;
		case 0x164f:		/* BCM57711 */
		case 0x1650:		/* BCM57711E */
			type = b57711;
			break;
		case 0x1662:		/* BCM57712 */
		case 0x1663:		/* BCM57712 Multi Function */
			type = b57712;
			break;
		}
		if(nctlr == nelem(ctlrtab)){
			print("%s: %T: too many controllers\n", cttab[type].name, p->tbdf);
			continue;
		}
		mempa = p->mem[0].bar&~0xf;
		mem = vmap(mempa, p->mem[0].size);
		if(mem == 0){
			print("%s: %T: cant map bar 0/reg\n", cttab[type].name, p->tbdf);
			continue;
		}
		dbpa = p->mem[2].bar&~0xf;
		db = vmap(dbpa, p->mem[2].size);
		if(db == 0){
			print("%s: %T: cant map bar 2/db\n", cttab[type].name, p->tbdf);
			vunmap(mem, p->mem[0].size);
			continue;
		}
		c = malloc(sizeof *c);
		c->p = p;
		c->type = cttab+type;
		c->rbsz = Rbsz;
		c->port = mempa;
		c->reg = (u32int*)mem;
		c->db = db;
		pcisetbme(p);
		if(reset(c) == -1){
			print("%s: %T: cant reset\n", c->type->name, p->tbdf);
			pciclrbme(p);
			free(c);
			vunmap(mem, p->mem[0].size);
			continue;
		}
		ctlrtab[nctlr++] = c;
	}
}

static int
pnp(Ether *e)
{
	Ctlr *c;
	int i;
	static int once;

	if(once == 0){
		once++;
		scan();
	}
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
	e->maxmtu = c->rbsz;
	memmove(e->ea, c->ra, Eaddrlen);
	e->arg = e;
	e->attach = attach;
	e->ctl = ctl;
	e->ifstat = ifstat;
	e->interrupt = interrupt;
	e->multicast = multicast;
	e->promiscuous = promiscuous;
	e->shutdown = shutdown;
//	e->transmit = transmit;

	return 0;

}

void
ether57711link(void)
{
	addethercard(name, pnp);
}
