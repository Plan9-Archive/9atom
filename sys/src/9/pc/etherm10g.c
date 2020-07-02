/*
 *	myricom 10 gbit ethernet
 *	© 2007—13 erik quanstrom, coraid, inc.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "etherif.h"

#define		K	* 1024
#define		MB	* 1024 K

#define	dprint(...)	if(debug) print(__VA_ARGS__); else {}
#define malign(n)	mallocalign(n, 4 K, 0, 0)
#define if64(...)		(sizeof(uintmem) == 8? (__VA_ARGS__): 0)
#define pbit32h(x)	if64(pbit32((uvlong)x >> 32))

#include "etherm10g2k.i"
#include "etherm10g4k.i"

enum {
	Epromsz	= 256,
	Maxslots	= 1024,
	Rbalign	= 4096,
	Noconf	= 0xffffffff,
	Fwoffset	= 1 MB,
	Hdroff	= 0x00003c,
	Cmdoff	= 0xf80000,		/* offset of command port */
	Fwsubmt	= 0xfc0000,		/* offset of firmware submission command port */
	Rdmaoff	= 0xfc01c0,		/* offset of rdma command port */
};

enum {
	CZero,
	Creset,
	Cversion,

	CSintrqdma,		/* issue these before Cetherup */
	CSbigsz,			/* in bytes bigsize = 2^n */
	CSsmallsz,

	CGsendoff,
	CGsmallrxoff,
	CGbigrxoff,
	CGirqackoff,
	CGirqdeassoff,
	CGsendrgsz,
	CGrxrgsz,

	CSintrqsz,		/* 2^n */
	Cetherup,		/* above paramters + mtu/mac addr must be set first */
	Cetherdn,

	CSmtu,			/* below may be issued live */
	CGcoaloff,		/* in µs */
	CSstatsrate,		/* in µs */
	CSstatsdma,

	Cpromisc,
	Cnopromisc,
	CSmac,

	Cenablefc,
	Cdisablefc,

	Cdmatest,

	Cenableallmc,
	Cdisableallmc,

	CSjoinmc,
	CSleavemc,
	Cleaveallmc,

	CSstatsdma2,
	Cdmatestu,
	Custatus,		/* unaligned status */
};

typedef	union	Cmd	Cmd;
typedef	struct	Slot	Slot;
typedef	struct	Send	Send;
typedef	struct	Tx	Tx;
typedef	struct	Bpool	Bpool;
typedef	struct	Rx	Rx;
typedef	struct	Stats	Stats;
typedef	struct	Done	Done;
typedef	struct	Ctlr	Ctlr;

union Cmd {
	u32int	i[2];
	uchar	c[8];
};

struct Slot {
	u16int	cksum;
	u16int	len;
};

enum {
	SFsmall	= 1,
	SFfirst	= 2,
	SFalign	= 4,
	SFnotso	= 16,
};

#define Fword(s)		*(u32int*)(((uchar*)(s) + 12))
struct Send {
	uchar	busaddr[8];
	uchar	hdroff[2];
	uchar	len[2];
	uchar	pad;
	uchar	nrdma;
	uchar	chkoff;
	uchar	flags;
};

/* not usuable due to alignment & padding issues */
struct xSend {
	u32int	high;
	u32int	low;
	u16int	hdroff;
	u16int	len;
	union{
		struct {
			uchar	pad;
			uchar	nrdma;
			uchar	chkoff;
			uchar	flags;
		};
		u32int	fword;	/* ha! */
	};
};

struct Tx {
	QLock;
	Send	*lanai;		/* tx ring (cksum + len in lanai memory) */
	Send	*host;		/* tx ring (data in our memory). */
	Block	**bring;
	int	size;		/* how big are the buffers in the z8's memory */
	uint	segsz;
	uint	n;		/* txslots */
	uint	m;		/* mask */
	uint	i;		/* number of segments (not frames) queued */
	uint	cnt;		/* number of segments sent by the card */
	uint	starve;
	uint	starvei;		/* starve pt */
	uint	submit;

	uint	npkt;
	uvlong	nbytes;
};

enum {
	Pstarve	= 1<<0,
};

struct Bpool {
	Lock;
	Block	*head;
	uint	size;		/* buffer size of each block */
	uint	n;		/* n free buffers. */
	uint	cnt;
	uint	flags;
};

struct Rx {
	Bpool	*pool;		/* free buffers */
	uint	*lanai;		/* rx ring; we have no perminant host shadow. */
	Block	**host;		/* called "info" in myricom driver */
	uint	m;
	uint	n;		/* rxslots */
	uint	i;
	uint	cnt;		/* number of buffers allocated (lifetime). */
};

/* dma mapped.  unix network byte order. */
struct Stats {
	uchar	unused[4];
	uchar	dpause[4];
	uchar	dufilt[4];
	uchar	dcrc32[4];
	uchar	dphy[4];
	uchar	dmcast[4];
	uchar	txcnt[4];
	uchar	linkstat[4];
	uchar	dlinkef[4];
	uchar	derror[4];
	uchar	drunt[4];
	uchar	doverrun[4];
	uchar	dnosm[4];
	uchar	dnobg[4];
	uchar	nrdma[4];
	uchar	txstopped;
	uchar	down;
	uchar	updated;
	uchar	valid;
};

enum {
	Detached,
	Attached,
	Runed,
};

struct Done {
	u32int	*entry;
	uintmem	busaddr;
	uint	m;
	uint	n;
	uint	i;
};

struct Ctlr {
	QLock;
	int	state;
	int	kprocs;
	uintmem	port;
	Pcidev*	pcidev;
	Ctlr*	next;
	int	active;

	uchar	ra[Eaddrlen];

	int	ramsz;
	uchar	*ram;

	u32int	*irqack;
	u32int	*irqdeass;
	u32int	*coal;

	char	eprom[Epromsz];
	uint	serial;			/* unit serial number */

	QLock	cmdl;
	Cmd	*cmd;			/* address of command return */
	uintmem	cprt;			/* bus address of command */

	int	boot;			/* boot address */

	Done	done;
	Tx	tx;
	Rx	sm;
	Rx	bg;
	Stats	*stats;
	uintmem	statsprt;
	uint	speed[2];

	Rendez	rxrendez;
	Rendez	txrendez;

	int	msi;
	uint	linkstat;
	uint	nrdma;
};

enum {
	PcieAERC	= 1,
	PcieVC,
	PcieSNC,
	PciePBC,
};

enum {
	AercCCR	= 0x18,		/* control register */
};

enum {
	PcieCTL		= 8,
	PcieLCR		= 12,
	PcieMRD	= 0x7000,	/* maximum read size */
};

static	char	Etimeout[]	= "timeout";
static	int 	debug;
static	Bpool	smpool 	= {.size	= 128, };
static	Bpool	bgpool	= {.size = 9000,};
static	Ctlr 	*ctlrs;

/*
 * this function doesn't work because pcicgr32 doesn't have access
 * to the pcie extended configuration space.
 */
static int
pciecap(Pcidev *p, int cap)
{
	uint off, i;

	off = 0x100;
	while(((i = pcicfgr32(p, off))&0xffff) != cap){
		off = i>>20;
		dprint("pciecap offset = %ud\n",  off);
		if(off < 0x100 || off >= 4 K - 1)
			return 0;
	}
	dprint("pciecap found = %ud\n",  off);
	return off;
}

static int
setpcie(Pcidev *p)
{
	int off;

	/* set 4k writes. */
	off = pcicap(p, PciCapPCIe);
	if(off == -1)
		return -1;
	off += PcieCTL;
	pcicfgw16(p, off, (pcicfgr16(p, off) & ~PcieMRD) | 5<<12);
	return 0;
}

static int
whichfw(Pcidev *p)
{
	char *s;
	int i, off, lanes, gen, ecrc;
	uint cap;

	/* check the number of configured lanes */
	off = pcicap(p, PciCapPCIe);
	if(off == -1)
		return -1;
	off += PcieLCR;
	cap = pcicfgr16(p, off);
	lanes = cap>>4 & 0x3f;
	gen = cap&7;

	/* check AERC register.  we need it on */
	off = pciecap(p, PcieAERC);
	dprint("%d offset\n", off);
	cap = 0;
	if(off != 0){
		off += AercCCR;
		cap = pcicfgr32(p, off);
		dprint("%ud cap\n", cap);
	}
	ecrc = cap>>4 & 0xf;
	/* if we don't like the aerc, kick it here */

	print("m10g %d lanes gen %d; ecrc=%d; ", lanes, gen, ecrc);
	if(s = getconf("myriforce")){
		i = atoi(s);
		if(i != 4 K || i != 2 K)
			i = 2 K;
		print("fw=%d [forced]\n", i);
		return i;
	}
	if(lanes <= 4){
		print("fw=4096 [lanes]\n");
		return 4 K;
	}
	if(ecrc & 10){
		print("fw=4096 [ecrc set]\n");
		return 4K;
	}
	print("fw=4096 [default]\n");
	return 4 K;
}

static int
parseeprom(Ctlr *c)
{
	int i, j, k, l, bits;
	char *s;

	dprint("m10g eprom:\n");
	s = c->eprom;
	bits = 3;
	for(i = 0; s[i] && i < Epromsz; i++){
		l = strlen(s + i);
		dprint("\t%s\n", s + i);
		if(strncmp(s + i, "MAC=", 4) == 0 && l == 21){
			bits ^= 1;
			j = i + 4;
			for(k = 0; k < 6; k++)
				c->ra[k] = strtoul(s + j + 3*k, 0, 16);
		}else if(strncmp(s + i, "SN=", 3) == 0){
			bits ^= 2;
			c->serial = atoi(s + i + 3);
		}
		i += l;
	}
	if(bits)
		return -1;
	return 0;
}

static u16int
pbit16(ushort i)
{
	u16int j;
	uchar *p;

	p = (uchar*)&j;
	p[1] = i;
	p[0] = i>>8;
	return j;
}

static u16int
gbit16(uchar i[2])
{
	u16int j;

	j = i[1];
	j |= i[0]<<8;
	return j;
}

static u32int
pbit32(uint i)
{
	u32int j;
	uchar *p;

	p = (uchar*)&j;
	p[3] = i;
	p[2] = i>>8;
	p[1] = i>>16;
	p[0] = i>>24;
	return j;
}

static u32int
gbit32(uchar i[4])
{
	u32int j;

	j = i[3];
	j |= i[2]<<8;
	j |= i[1]<<16;
	j |= i[0]<<24;
	return j;
}

static void
prepcmd(u32int *cmd, int i)
{
	while(i-- > 0)
		cmd[i] = pbit32(cmd[i]);
}

/*
 * the command looks like this (int 32bit integers)
 * cmd type
 * data0 (or, addr low; endian backwards)
 * data1 (addr high)
 * data2
 * response (high)
 * response (low)
 * 40 byte = 5 int pad.
 */

static uint
cmd(Ctlr *c, int type, int sz, uvlong data)
{
	u32int buf[16], i;
	Cmd *cmd;

	qlock(&c->cmdl);
	cmd = c->cmd;
	cmd->i[1] = Noconf;
	memset(buf, 0, sizeof buf);
	buf[0] = type;
	buf[1] = data;
	buf[2] = data>>32;
	buf[3] = sz;
	buf[4] = (uvlong)c->cprt>>32;
	buf[5] = c->cprt;
	prepcmd(buf, 6);
	coherence();
	memmove(c->ram + Cmdoff, buf, sizeof buf);

	if(waserror())
		nexterror();
	for(i = 0; i < 15; i++){
		if(cmd->i[1] != Noconf){
			poperror();
			i = gbit32(cmd->c);
			qunlock(&c->cmdl);
			if(cmd->i[1] != 0)
				dprint("[%ux]", i);
			return i;
		}
		tsleep(&up->sleep, return0, 0, 1);
	}
	qunlock(&c->cmdl);
	print("m10g: cmd timeout [%ux %ux] cmd=%d\n", cmd->i[0], cmd->i[1], type);
	error(Etimeout);
	return ~0;
}

static uint
maccmd(Ctlr *c, int type, uchar *m)
{
	u32int buf[16], i;
	Cmd * cmd;

	qlock(&c->cmdl);
	cmd = c->cmd;
	cmd->i[1] = Noconf;
	memset(buf, 0, sizeof buf);
	buf[0] = type;
	buf[1] = m[0]<<24 | m[1]<<16 | m[2]<<8 | m[3];
	buf[2] = m[4]<<8 | m[5];
	buf[4] = (uvlong)c->cprt>>32;
	buf[5] = c->cprt;
	prepcmd(buf, 6);
	coherence();
	memmove(c->ram + Cmdoff, buf, sizeof buf);

	if(waserror())
		nexterror();
	for(i = 0; i < 15; i++){
		if(cmd->i[1] != Noconf){
			poperror();
			i = gbit32(cmd->c);
			qunlock(&c->cmdl);
			if(cmd->i[1] != 0)
				dprint("[%ux]", i);
			return i;
		}
		tsleep(&up->sleep, return0, 0, 1);
	}
	qunlock(&c->cmdl);
	print("m10g: maccmd timeout [%ux %ux] cmd=%d\n", cmd->i[0], cmd->i[1], type);
	error(Etimeout);
	return ~0;
}

/* remove this garbage after testing */
enum{
	DMAread	= 0x10000,
	DMAwrite	= 0x1
};

static uint
dmatestcmd(Ctlr *c, int type, uvlong addr, int len)
{
	u32int buf[16], i;

	memset(buf, 0, sizeof buf);
	memset(c->cmd, Noconf, sizeof *c->cmd);
	buf[0] = Cdmatest;
	buf[1] = addr;
	buf[2] = addr>>32;
	buf[3] = len*type;
	buf[4] = (uvlong)c->cprt>>32;
	buf[5] = c->cprt;
	prepcmd(buf, 6);
	coherence();
	memmove(c->ram + Cmdoff, buf, sizeof buf);

	if(waserror())
		nexterror();
	for(i = 0; i < 15; i++){
		if(c->cmd->i[1] != Noconf){
			i = gbit32(c->cmd->c);
			if(i == 0)
				error(Eio);
			poperror();
			return i;
		}
		tsleep(&up->sleep, return0, 0, 5);
	}
	error(Etimeout);
	return ~0;
}

static uint
rdmacmd(Ctlr *c, int on)
{
	u32int buf[16], i;

	memset(buf, 0, sizeof buf);
	c->cmd->i[0] = 0;
	coherence();
	buf[0] = (uvlong)c->cprt>>32;
	buf[1] = c->cprt;
	buf[2] = Noconf;
	buf[3] = (uvlong)c->cprt>>32;
	buf[4] = c->cprt;
	buf[5] = on;
	prepcmd(buf, 6);
	memmove(c->ram + Rdmaoff, buf, sizeof buf);

	if(waserror())
		nexterror();
	for(i = 0; i < 20; i++){
		if(c->cmd->i[0] == Noconf){
			poperror();
			return gbit32(c->cmd->c);
		}
		tsleep(&up->sleep, return0, 0, 1);
	}
	error(Etimeout);
	print("m10g: rdmacmd timeout\n");
	return ~0;
}

static int
loadfw(Ctlr *c, int *align)
{
	u32int *f, *s, sz;
	int i;

	if((*align = whichfw(c->pcidev)) == 4 K){
		f = (u32int*)fw4k;
		sz = sizeof fw4k;
	}else{
		f = (u32int*)fw2k;
		sz = sizeof fw2k;
	}

	s = (u32int*)(c->ram + Fwoffset);
	for(i = 0; i < sz/4; i++)
		s[i] = f[i];
	return sz&~3;
}

static int
bootfw(Ctlr *c)
{
	int i, sz, align;
	u32int buf[16];
	Cmd* cmd;

	if((sz = loadfw(c, &align)) == 0)
		return 0;
	dprint("m10g: bootfw %d bytes ... ", sz);
	cmd = c->cmd;

	memset(buf, 0, sizeof buf);
	c->cmd->i[0] = 0;
	coherence();
	buf[0] = (uvlong)c->cprt>>32;	/* upper 32 bits of dma target address */
	buf[1] = c->cprt;			/* lower */
	buf[2] = Noconf;			/* writeback */
	buf[3] = Fwoffset + 8,
	buf[4] = sz - 8;
	buf[5] = 8;
	buf[6] = 0;
	prepcmd(buf, 7);
	coherence();
	memmove(c->ram + Fwsubmt, buf, sizeof buf);

	for(i = 0; i < 20; i++){
		if(cmd->i[0] == Noconf)
			break;
		delay(1);
	}
	dprint("[%ux %ux]", gbit32(cmd->c), gbit32(cmd->c + 4));
	if(i == 20){
		print("m10g: cannot load fw\n");
		return -1;
	}
	dprint("\n");
	c->tx.segsz = align;
	return 0;
}

static int
kickthebaby(Pcidev *p, Ctlr *c)
{
	/* don't kick the baby! */
	uint code;

	pcicfgw8(p, 0x10 + c->boot, 0x3);
	pcicfgw32(p, 0x18 + c->boot, 0xfffffff0);
	code = pcicfgr32(p, 0x14 + c->boot);

	dprint("m10g: reboot status = %ux\n", code);
	if(code != 0xfffffff0)
		return -1;
	return 0;
}

typedef struct{
	uchar	len[4];
	uchar	type[4];
	char	version[128];
	uchar	globals[4];
	uchar	ramsz[4];
	uchar	specs[4];
	uchar	specssz[4];
	uchar	idx;
	uchar	norabbit;
	uchar	unaligntlp;
	uchar	pcilinkalg;
	uchar	cntaddr[4];
	uchar	cbinfo[4];
	uchar	handoid[2];
	uchar	handocap[2];
	uchar	msixtab[4];
	uchar	bss[4];
	uchar	features[4];
	uchar	eehdr[4];
} Fwhdr;

enum{
	Tmx	= 0x4d582020,
	Tpcie	= 0x70636965,
	Teth	= 0x45544820,
	Tmcp0	= 0x4d435030,
};

static char*
fwtype(uint type)
{
	switch(type){
	case Tmx:
		return "mx";
	case Tpcie:
		return "PCIe";
	case Teth:
		return "eth";
	case Tmcp0:
		return "mcp0";
	}
	return "*GOK*";
}

static int
chkfw(Ctlr *c)
{
	uint off, type;
	Fwhdr *h;

	off = gbit32(c->ram + Hdroff);
	dprint("m10g: firmware %ux\n", off);
	if(off == 0 || off&3 || off + sizeof *h >= c->ramsz){
		print("m10g: bad firmware %#ux\n", off);
		return -1;
	}
	h = (Fwhdr*)(c->ram + off);
	type = gbit32(h->type);
	dprint("\t" "type	%s\n", fwtype(type));
	dprint("\t" "vers	%s\n", h->version);
	dprint("\t" "ramsz	%ux\n", gbit32(h->ramsz));
	if(type != Teth){
		print("m10g: bad card type %s\n", fwtype(type));
		return -1;
	}

	return bootfw(c) || rdmacmd(c, 0);
}

static int
reset(Ether *e, Ctlr *c)
{
	uint i, sz;

	if(waserror()){
		print("m10g: reset error\n");
		nexterror();
		return -1;
	}

	chkfw(c);
	cmd(c, Creset, 0, 0);
	cmd(c, Cdisablefc, 0, 0);  /* botch: wrensbackup doesn't boot otherwise */

	cmd(c, CSintrqsz, 0, c->done.n*sizeof *c->done.entry);
	cmd(c, CSintrqdma, 0, c->done.busaddr);
	c->irqack = (u32int*)(c->ram + cmd(c, CGirqackoff, 0, 0));
	c->irqdeass = (u32int*)(c->ram + cmd(c, CGirqdeassoff, 0, 0));
	c->coal = (u32int*)(c->ram + cmd(c, CGcoaloff, 0, 0));
	*c->coal = pbit32(20);

	dprint("dma stats:\n");
	rdmacmd(c, 1);
	sz = c->tx.segsz;
	i = dmatestcmd(c, DMAread, c->done.busaddr, sz);
	print("\t" "r %ud ", ((i>>16)*sz*2)/(i&0xffff));
	i = dmatestcmd(c, DMAwrite, c->done.busaddr, sz);
	print("\t" "w %ud ", ((i>>16)*sz*2)/(i&0xffff));
	i = dmatestcmd(c, DMAwrite|DMAread, c->done.busaddr, sz);
	print("\t" "r/w %ud MB/s\n", ((i>>16)*sz*2*2)/(i&0xffff));
	memset(c->done.entry, 0, c->done.n*sizeof *c->done.entry);

	maccmd(c, CSmac, c->ra);
	cmd(c, Cenablefc, 0, 0);
	e->maxmtu = 9000;
	if(e->maxmtu > 9000)
		e->maxmtu = 9000;
	cmd(c, CSmtu, 0, e->maxmtu);

	poperror();
	return 0;
}

static void
ctlrfree(Ctlr *c)
{
	/* free up all the Block*s, too; tricky */
	free(c->tx.host);
	free(c->sm.host);
	free(c->bg.host);
	free(c->cmd);
	free(c->done.entry);
	free(c->stats);
	free(c);
}

static int
setmem(Pcidev *p, Ctlr *c)
{
	uint i;
	uintmem raddr;
	void *mem;
	Done *d;

	c->tx.segsz = 2048;
	c->ramsz = 2 MB - (2*48 K + 32 K) - 0x100;
	if(c->ramsz > p->mem[0].size)
		return -1;

	raddr = p->mem[0].bar & ~0x0F;
	mem = vmappat(raddr, p->mem[0].size, PATWC);
	if(mem == nil){
		print("m10g: can't map %P\n", (uintmem)p->mem[0].bar);
		return -1;
	}
	c->port = raddr;
	c->ram = mem;
	c->cmd = malign(sizeof *c->cmd);
	c->cprt = PCIWADDR(c->cmd);

	d = &c->done;
	d->n = Maxslots;
	d->m = d->n - 1;
	i = d->n*sizeof *d->entry;
	d->entry = malign(i);
	d->busaddr = PCIWADDR(d->entry);

	c->stats = malign(sizeof *c->stats);
	c->statsprt = PCIWADDR(c->stats);

	memmove(c->eprom, c->ram + c->ramsz - Epromsz, Epromsz - 2);
	return setpcie(p) || parseeprom(c);
}

static Rx*
whichrx(Ctlr *c, int sz)
{
	if(sz <= smpool.size)
		return &c->sm;
	return &c->bg;
}

static Block*
balloc(Rx* rx)
{
	Block *b;

	if((b = rx->pool->head) != nil){
		rx->pool->head = b->next;
		b->next = nil;
		rx->pool->n--;
	}
	return b;
}

static void
pkick(Bpool*)
{
	Ctlr *c;

	for(c = ctlrs; c; c = c->next)
		wakeup(&c->rxrendez);
}

static void
smbfree(Block *b)
{
	Bpool *p;

	b->rp = b->wp = (uchar*)ROUNDUP((uintptr)b->base, Rbalign);
	b->flag &= ~(Bipck | Budpck | Btcpck | Bpktck);
	p = &smpool;
	ilock(p);
	b->next = p->head;
	p->head = b;
	p->n++;
	p->cnt++;
	if(p->flags & Pstarve && p->n > 16)
		pkick(p);
	iunlock(p);
}

static void
bgbfree(Block *b)
{
	Bpool *p;

	b->rp = b->wp = (uchar*)ROUNDUP((uintptr)b->base, Rbalign);
	b->flag &= ~(Bipck | Budpck | Btcpck | Bpktck);
	p = &bgpool;
	ilock(p);
	b->next = p->head;
	p->head = b;
	p->n++;
	p->cnt++;
	if(p->flags & Pstarve && p->n > 16)
		pkick(p);
	iunlock(p);
}

/*
 * this is highly optimized to reduce bus cycles with
 * w/c memory while respecting the lanai z model a's
 * limit of 32-bytes writes > 32 bytes must be handled
 * by card f/w.  partial writes are also handled by f/w.
 */

static void
replenish(Rx *rx)
{
	u32int buf[16], i, idx, e, f;
	Bpool *p;
	Block *b;

	p = rx->pool;
	if(p->n < 8){
		ilock(p);
		p->flags |= Pstarve;
		iunlock(p);
		return;
	}
	e = (rx->i - rx->cnt) & ~7;
	e += rx->n;
	ilock(p);
	while(p->n >= 8 && e){
		idx = rx->cnt & rx->m;
		for(i = 0; i < 8; i++){
			b = balloc(rx);
			buf[i*2 + 0] = pbit32h(PCIWADDR(b->wp));
			buf[i*2 + 1] = pbit32(PCIWADDR(b->wp));
			rx->host[idx + i] = b;
		}
		f = buf[1];
		buf[1] = ~0;
		memmove(rx->lanai + 2*idx, buf, sizeof buf / 2);
		sfence();
		memmove(rx->lanai + 2*(idx + 4), buf + 8, sizeof buf / 2);
		rx->lanai[2*idx + 1] = f;
		sfence();
		rx->cnt += 8;
		e -= 8;
		p->flags &= ~Pstarve;
	}
	if(e && p->n > 7 + 1)
		iprint("m10g: should panic? pool->n = %d", p->n);
	iunlock(p);
}

static int
nextpow(int j)
{
	int i;

	for(i = 0; j > 1<<i; i++)
		;
	return 1<<i;
}

static void*
emalign(int sz)
{
	void *v;

	v = malign(sz);
	if(v == 0)
		error(Enomem);
	return v;
}

static void
open0(Ether *e, Ctlr *c)
{
	int i, sz, entries;
	Block *b;

	entries = cmd(c, CGsendrgsz, 0, 0)/sizeof *c->tx.lanai;
	c->tx.lanai = (Send*)(c->ram + cmd(c, CGsendoff, 0, 0));
	c->tx.host = emalign(entries*sizeof *c->tx.host);
	c->tx.bring = emalign(entries*sizeof *c->tx.bring);
	c->tx.n = entries;
	c->tx.m = entries - 1;

	entries = cmd(c, CGrxrgsz, 0, 0)/8;
	c->sm.pool = &smpool;
	cmd(c, CSsmallsz, 0, c->sm.pool->size);
	c->sm.lanai = (u32int*)(c->ram + cmd(c, CGsmallrxoff, 0, 0));
	c->sm.n = entries;
	c->sm.m = entries - 1;
	c->sm.host = emalign(entries*sizeof *c->sm.host);

	c->bg.pool = &bgpool;
	c->bg.pool->size =  nextpow(e->maxmtu + 2);	/* 2 byte alignment pad */
	cmd(c, CSbigsz, 0, c->bg.pool->size);
	c->bg.lanai = (u32int*)(c->ram + cmd(c, CGbigrxoff, 0, 0));
	c->bg.n = entries;
	c->bg.m = entries - 1;
	c->bg.host = emalign(entries*sizeof *c->bg.host);

	sz = c->sm.pool->size + Rbalign;
	for(i = 0; i < c->sm.n*2; i++){
		b = allocb(sz);
		b->free = smbfree;
		freeb(b);
	}
	sz = c->bg.pool->size + Rbalign;
	for(i = 0; i < c->bg.n*2; i++){
		b = allocb(sz);
		b->free = bgbfree;
		freeb(b);
	}

	cmd(c, CSstatsdma2, sizeof *c->stats, c->statsprt);
	c->linkstat = ~0;
	c->nrdma = 15;

	cmd(c, Cetherup, 0, 0);
}

static Block*
nextblock(Ctlr *c)
{
	uint i;
	u16int l, k;
	Slot *s;
	Done *d;
	Block *b;
	Rx *rx;

	d = &c->done;
	i = d->i&d->m;
	s = (Slot*)(d->entry + i);
	l = s->len;
	if(l == 0)
		return 0;
	k = s->cksum;
	s->len = 0;
	d->i++;
	l = gbit16((uchar*)&l);
	rx = whichrx(c, l);
	if(rx->i - rx->cnt <= rx->n){
		print("m10g: overrun\n");
		return 0;
	}
	i = rx->i&rx->m;
	b = rx->host[i];
	rx->host[i] = 0;
	if(b == 0)
		panic("m10g: rx to no block");
	rx->i++;

	b->flag |= Bipck|Btcpck|Budpck;
	b->checksum = k;
	b->rp += 2;
	b->wp += 2 + l;
	b->lim = b->wp;		/* lie like a dog */
	return b;
}

static int
rxcansleep(void *v)
{
	u32int *e;
	Ctlr *c;
	Done *d;
	Slot *s;

	c = v;
	d = &c->done;
	e = d->entry + (d->i&d->m);
	s = (Slot*)e;
	if(s->len != 0)
		return -1;
	c->irqack[0] = pbit32(3);
	if((c->sm.pool->flags | c->bg.pool->flags) & Pstarve)
		return -1;
	return 0;
}

static void
rxproc(void *v)
{
	int i, l;
	Block *b;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;

	l = c->sm.m;
	if(c->bg.m < l)
		l = c->sm.m;
	l *= 2;
	l /= 3;

	for(;;){
		replenish(&c->sm);
		replenish(&c->bg);
		sleep(&c->rxrendez, rxcansleep, c);
		for(i = 0; i < l && (b = nextblock(c)); i++)
			etheriq(e, b, 1);
	}
}

static uint
txstarving(Tx *tx, uint u)
{
	uint d;

	d = tx->n - (tx->i - tx->cnt);
	return d <= u;
}

static int
txcleanup(Tx *tx, uint n)
{
	uint j, l;
	Block *b;

	for(l = 0; l < tx->m; l++){
		if(tx->npkt == n)
			break;
		if(tx->cnt == tx->i){
			dprint("m10g: txcleanup cnt == i %ud\n", tx->i);
			break;
		}
		j = tx->cnt & tx->m;
		if(b = tx->bring[j]){
			tx->bring[j] = 0;
			tx->nbytes += BLEN(b);
			freeb(b);
			tx->npkt++;
		}
		tx->cnt++;
	}
	if(l == 0 && !tx->starve)
		dprint("m10g: spurious cleanup\n");
	if(l >= tx->m)
		print("m10g: tx ovrun: %ud %ud\n", n, tx->npkt);
	if(tx->starve && !txstarving(tx, tx->n/2)){
		tx->starve = 0;
		return 1;
	}
	return 0;
}

static int
txcansleep(void *v)
{
	Ctlr *c;

	c = v;
	if(c->tx.starve == 0)
		return -1;
	return 0;
}

static void
submittx(Tx *tx, int n)
{
	int i0, i, m;
	uint v;
	Send *l, *h;

	m = tx->m;
	i0 = tx->i&m;
	l = tx->lanai;
	h = tx->host;
//	v = h[i0].fword;
	v = Fword(h+i0);
	h[i0].flags = 0;
	for(i = n - 1; i >= 0; i--)
		memmove(l+(i+i0&m), h+(i+i0&m), sizeof *h);
	sfence();
//	l[i0].fword = v;
	Fword(l+i0) = v;
	tx->i += n;
	sfence();
}

static void
txproc(void *v)
{
	uchar flags;
	u16int slen;
	uint nseg, len;
	uintmem bus, end, segsz;
	Ctlr *c;
	Block *b;
	Ether *e;
	Tx *tx;
	Send *s0, *s, *se;

	e = v;
	c = e->ctlr;
	tx = &c->tx;
	segsz = tx->segsz;
	s = tx->host + (tx->i&tx->m);
	se = tx->host + tx->n;
	for(;;){
		if(txstarving(tx, 16)){
			tx->starvei = tx->i;
			tx->starve = 1;
			sleep(&c->txrendez, txcansleep, c);
			continue;
		}
		if((b = qbread(e->oq, 100000)) == nil)
			break;
		flags = SFfirst|SFnotso;
		len = BLEN(b);
		if(len < 1520)
			flags |= SFsmall;
		bus = PCIWADDR(b->rp);
		s0 = s;
		nseg = 0;
		for(; len; len -= slen){
			end = bus+segsz & ~(segsz-1);
			slen = end - bus;
			if(slen > len)
				slen = len;
			putbe(s->busaddr, bus, 8);
			putbe(s->len, slen, 2);
			s->flags = flags;
			s->nrdma = 1;

			bus += slen;
			if(++s == se)
				s = tx->host;
			flags &= ~SFfirst;
			nseg++;
		}
		s0->nrdma = nseg;
		tx->bring[tx->i+nseg-1 & tx->m] = b;
		submittx(tx, nseg);
		tx->submit++;
	}
	print("m10g: txproc: queue closed\n");
	pexit("queue closed", 1);
}

static void
checkstats(Ether *e, Ctlr *c, Stats *s)
{
	uint i;

	if(s->updated == 0)
		return;

	i = gbit32(s->linkstat);
	if(c->linkstat != i){
		e->link = i;
		c->speed[i>0]++;
		if(c->linkstat = i){
			dprint("m10g: link up\n");
			c->tx.starve = 0;
			wakeup(&c->txrendez);
		}else
			dprint("m10g: link down\n");
	}
	i = gbit32(s->nrdma);
	if(i != c->nrdma){
		dprint("m10g: rdma timeout %d\n", i);
		c->nrdma = i;
	}
}

static void
waitintx(Ctlr *c)
{
	int i, n;

	for(i = 0; i < 1048576; i++){
		coherence();
		n = gbit32(c->stats->txcnt);
		if(n != c->tx.npkt || c->tx.starve)
			if(txcleanup(&c->tx, n))
				wakeup(&c->txrendez);
		if(c->stats->valid == 0)
			break;
	}
}

static void
m10ginterrupt(Ureg *, void *v)
{
	int valid;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;

	valid = c->stats->valid;
	if(c->state != Runed || valid == 0)
		return;
	if(c->msi == 0)
		*c->irqdeass = 0;
	else
		c->stats->valid = 0;
	waitintx(c);
	checkstats(e, c, c->stats);
	c->irqack[1] = pbit32(3);
	if(valid&1)
		wakeup(&c->rxrendez);
}

static void
m10gattach(Ether *e)
{
	Ctlr *c;
	char name[12];

	dprint("m10g: attach\n");
	qlock(e->ctlr);
	c = e->ctlr;
	if(c->state != Detached){
		qunlock(c);
		return;
	}
	if(waserror()){
		c->state = Detached;
		qunlock(c);
		nexterror();
	}
	reset(e, c);
	c->state = Attached;
	open0(e, c);
	if(c->kprocs == 0){
		c->kprocs++;
		snprint(name, sizeof name, "#l%drxproc", e->ctlrno);
		kproc(name, rxproc, e);
		snprint(name, sizeof name, "#l%dtxproc", e->ctlrno);
		kproc(name, txproc, e);
	}
	c->state = Runed;
	m10ginterrupt(nil, e);		/* botch: racy */
	qunlock(c);
	poperror();
}

static int
m10gdetach(Ctlr *c)
{
	Ctlr *p;

	cmd(c, Creset, 0, 0);
	if(c == ctlrs)
		ctlrs = c->next;
	else{
		for(p = ctlrs; p->next; p = p->next)
			if(p->next == c)
				break;
		p->next = c->next;
	}
	vunmap(c->ram, c->pcidev->mem[0].size);
	ctlrfree(c);
	return -1;
}

static int
lstcount(Block *b)
{
	int i;

	i = 0;
	for(; b; b = b->next)
		i++;
	return i;
}

static long
m10gifstat(Ether *e, void *v, long n, ulong off)
{
	char *p;
	int l, lim;
	Ctlr *c;
	Stats s;

	c = e->ctlr;
	lim = READSTR - 1;
	p = malloc(lim + 1);
	if (p == nil)
		return 0;
	l = 0;
	/* no point in locking this because this is done via dma */
	memmove(&s, c->stats, sizeof s);

	//l +=
	snprint(p + l, lim,
		"txcnt = %ud\n" 		"linkstat = %ud\n" 	"dlink = %ud\n"
		"derror = %ud\n" 	"drunt = %ud\n" 		"doverrun = %ud\n"
		"dnosm = %ud\n" 	"dnobg = %ud\n" 	"nrdma = %ud\n"
		"dpause = %ud\n"	"dufilt = %ud\n"		"dcrc32 = %ud\n"
		"dphy = %ud\n"		"dmcast = %ud\n"
		"txstopped = %ud\n" 	"down = %ud\n" 		"updated = %ud\n"
		"valid = %ud\n\n"
		"tx starve = %ud\n"	"tx starvei = %ud\n"
		"tx pkt = %ud\n"		"tx submit = %ud\n"	"tx bytes = %llud\n"
		"tx n = %ud\t"	"cnt = %ud\t"	"i = %ud\n"
		"sm n = %ud\t"	"cnt = %ud\t"	"i = %ud\t"	"lst = %ud\n"
		"bg n = %ud\t"	"cnt = %ud\t"	"i = %ud\t"	"lst = %ud\n"
		"segsz = %ud\n"		"coal = %ud\n\n"
		"speeds 0:%ud 10000:%ud\n"
		"type: m10g\n",
		gbit32(s.txcnt), gbit32(s.linkstat), gbit32(s.dlinkef),
		gbit32(s.derror), gbit32(s.drunt), gbit32(s.doverrun),
		gbit32(s.dnosm), gbit32(s.dnobg), gbit32(s.nrdma),
		gbit32(s.dpause), gbit32(s.dufilt), gbit32(s.dcrc32),
		gbit32(s.dphy), gbit32(s.dmcast),
		s.txstopped,  s.down, s.updated, s.valid,
		c->tx.starve, c->tx.starvei,
		c->tx.npkt, c->tx.submit, c->tx.nbytes,
		c->tx.n, c->tx.cnt, c->tx.i,
		c->sm.pool->n, c->sm.cnt,  c->sm.i, lstcount(c->sm.pool->head),
		c->bg.pool->n, c->bg.cnt,  c->bg.i, lstcount(c->bg.pool->head),
		c->tx.segsz, gbit32((uchar*)c->coal),
		c->speed[0], c->speed[1]);

	n = readstr(off, v, n, p);
	free(p);
	return n;
}

enum {
	CMdebug,
	CMcoal,
//	CMcount,
	CMpause,
	CMunpause,
	CMwakeup,
	CMtxwakeup,
};

static Cmdtab ctab[] = {
	CMdebug,	"debug",		2,
	CMcoal,		"coal",		2,
//	CMcount,	"count",		1,
	CMpause,	"pause",		1,
	CMunpause,	"unpause",	1,
	CMwakeup,	"wakeup",	1,
	CMtxwakeup,	"txwakeup",	1,
};

//#include "mdbg.c"

static long
m10gctl(Ether *e, void *v, long n)
{
	int i;
	Ctlr *x;
	Cmdbuf *c;
	Cmdtab *t;

	if((x = e->ctlr) == nil)
		error(Enonexist);

	c = parsecmd(v, n);
	if(waserror()){
		free(c);
		nexterror();
	}
	t = lookupcmd(c, ctab, nelem(ctab));
	switch(t->index){
	case CMdebug:
		debug = strcmp(c->f[1], "on") == 0;
		break;
	case CMcoal:
		i = atoi(c->f[1]);
		if(i<0 || i>1000)
			error(Ebadarg);
		*x->coal = pbit32(i);
		break;
//	case CMcount:
//		readcntr(x);
//		break;
	case CMpause:
		cmd(x, Cenablefc, 0, 0);
		break;
	case CMunpause:
		cmd(x, Cdisablefc, 0, 0);
		break;
	case CMwakeup:
		wakeup(&x->rxrendez);		/* you're kidding, right? */
		break;
	case CMtxwakeup:
		x->tx.starve = 0;
		wakeup(&x->txrendez);		/* you're kidding, right? */
		break;
	default:
		error(Ebadarg);
	}
	free(c);
	poperror();
	return n;
}

static void
m10gshutdown(Ether *e)
{
	m10gdetach(e->ctlr);
}

static void
m10gpromiscuous(void *v, int on)
{
	int i;
	Ether *e;

	dprint("m10g: promiscuous\n");
	e = v;
	if(on)
		i = Cpromisc;
	else
		i = Cnopromisc;
	cmd(e->ctlr, i, 0, 0);
}

static	int	mcctab[] = {CSleavemc, CSjoinmc};
static	char	*mcntab[] = {"leave", "join"};

static void
m10gmulticast(void *v, uchar *ea, int on)
{
	int i;
	Ether *e;

	e = v;
	if((i = maccmd(e->ctlr, mcctab[on], ea)) != 0)
		print("m10g: can't %s %E: %d\n", mcntab[on], ea, i);
}

static void
m10gpci(void)
{
	Ctlr **t, *c;
	Pcidev *p;

	t = &ctlrs;
	for(p = nil; p = pcimatch(p, 0x14c1, 0x0008); ){
		c = malloc(sizeof *c);
		if(c == nil)
			continue;
		c->pcidev = p;
		pcisetbme(p);
		c->boot = pcicap(p, PciCapVND);
//		kickthebaby(p, c);
		if(c->boot == -1 || setmem(p, c) == -1){
			print("m10g: init failed\n");
			free(c);
			continue;
		}
		*t = c;
		t = &c->next;
	}
}

static int
m10gpnp(Ether *e)
{
	Ctlr *c;

	if(ctlrs == nil)
		m10gpci();
	for(c = ctlrs; c != nil; c = c->next)
		if(c->active)
			continue;
		else if(ethercfgmatch(e, c->pcidev, c->port) == 0)
			break;
	if(c == nil)
		return -1;
	c->active = 1;

	e->ctlr = c;
	e->port = c->port;
	e->irq = c->pcidev->intl;
	e->tbdf = c->pcidev->tbdf;
	e->mbps = 10000;
	memmove(e->ea, c->ra, Eaddrlen);

	e->attach = m10gattach;
	e->detach = m10gshutdown;
	e->transmit = nil;
	e->interrupt = m10ginterrupt;
	e->ifstat = m10gifstat;
	e->ctl = m10gctl;
//	e->power = m10gpower;
	e->shutdown = m10gshutdown;

	e->arg = e;
	e->promiscuous = m10gpromiscuous;
	e->multicast = m10gmulticast;

	return 0;
}

void
etherm10glink(void)
{
	addethercard("m10g", m10gpnp);
}
