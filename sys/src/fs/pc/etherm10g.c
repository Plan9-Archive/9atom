/*
 *	myricom 10 gbit ethernet
 *	© 2007—9 erik quanstrom, coraid, inc.
 */

#include "all.h"
#include "io.h"
#include "../ip/ip.h"
#include "etherif.h"
#include "mem.h"

#undef		MB
#define		K	* 1024
#define		MB	* 1024 K

#define	dprint(...)	if(debug) print(__VA_ARGS__); else {}
#define	pcicapdbg(...)
#define malign(n)	ialloc(n, 4 K)
#define if64(...)		(sizeof(uintptr) == 8? (__VA_ARGS__): 0)
#define pbit32h(x)	if64(pbit32((uvlong)x >> 32))

#include "etherm10g2k.i"
#include "etherm10g4k.i"

enum {
	Epromsz	= 256,
	Maxslots	= 1024,
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

typedef union {
	uint	i[2];
	uchar	c[8];
} Cmd;

typedef struct {
	ushort	cksum;
	ushort	len;
} Slot;

enum {
	SFsmall	= 1,
	SFfirst	= 2,
	SFalign	= 4,
	SFnotso	= 16,
};

typedef struct {
	uint	high;
	uint	low;
	ushort	hdroff;
	ushort	len;
	union{
		struct {
			uchar	pad;
			uchar	nrdma;
			uchar	chkoff;
			uchar	flags;
		};
		uint	fword;	/* ha! */
	};
} Send;

typedef struct {
	QLock;
	Send	*lanai;		/* tx ring (cksum + len in lanai memory) */
	Send	*host;		/* tx ring (data in our memory). */
	Msgbuf	**bring;
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
	vlong	nbytes;
} Tx;

enum {
	Pstarve	= 1<<0,
};

typedef struct {
	Lock;
	Msgbuf	*head;
	uint	size;		/* buffer size of each block */
	uint	n;		/* n free buffers. */
	uint	cnt;
	uint	flags;
} Bpool;

typedef struct {
	Bpool	*pool;		/* free buffers */
	uint	*lanai;		/* rx ring; we have no perminant host shadow. */
	Msgbuf	**host;		/* called "info" in myricom driver */
	uint	m;
	uint	n;		/* rxslots */
	uint	i;
	uint	cnt;		/* number of buffers allocated (lifetime). */
} Rx;

/* dma mapped.  unix network byte order. */
typedef struct {
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
} Stats;

enum {
	Detached,
	Attached,
	Runed,
};

typedef struct {
	uint	*entry;
	uintptr	busaddr;
	uint	m;
	uint	n;
	uint	i;
} Done;

typedef struct Ctlr Ctlr;
typedef struct Ctlr {
	QLock;
	int	state;
	int	kprocs;
	uintptr	port;
	Pcidev*	pcidev;
	Ctlr*	next;
	int	active;

	uchar	ra[Easize];

	int	ramsz;
	uchar	*ram;

	uint	*irqack;
	uint	*irqdeass;
	uint	*coal;

	char	eprom[Epromsz];
	uint	serial;			/* unit serial number */

	QLock	cmdl;
	Cmd	*cmd;			/* address of command return */
	uintptr	cprt;			/* bus address of command */

	uintptr	boot;			/* boot address */

	Done	done;
	Tx	tx;
	Rx	sm;
	Rx	bg;
	Stats	*stats;
	uintptr	statsprt;
	uint	speed[2];

	Rendez	rxrendez;
	Rendez	txrendez;

	int	msi;
	uint	linkstat;
	uint	nrdma;

	char	rname[12];
	char	tname[12];
} Ctlr;

enum {
	PciCapPMG	= 0x01,		/* power management */
	PciCapAGP	= 0x02,
	PciCapVPD	= 0x03,		/* vital product data */
	PciCapSID	= 0x04,		/* slot id */
	PciCapMSI	= 0x05,
	PciCapCHS	= 0x06,		/* compact pci hot swap */
	PciCapPCIX	= 0x07,
	PciCapHTC	= 0x08,		/* hypertransport irq conf */
	PciCapVND	= 0x09,		/* vendor specific information */
	PciCapPCIe	= 0x10,
	PciCapMSIX	= 0x11,
	PciCapSATA	= 0x12,
	PciCapHSW	= 0x0C,		/* hot swap */
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

static	int 	debug		= 0;
static	char	Etimeout[]	= "timeout";
static	char	Enomem[]	= "no memory";
static	char	Enonexist[]	= "controler lost";
static	char	Ebadarg[]	= "bad argument";
static	Bpool	smpool 	= {.size	= 128, };
static	Bpool	bgpool	= {.size = 9000,};
static	Ctlr 	ctlrs[3];
static	int	nctlr;

static int
pcicap(Pcidev *p, int cap)
{
	int i, c, off;

	pcicapdbg("pcicap: %x:%d\n", p->vid, p->did);
	off = 0x34;	/* 0x14 for cardbus. */
	for(i = 48; i--;){
		pcicapdbg("\t" "loop %x\n", off);
		off = pcicfgr8(p, off);
		pcicapdbg("\t" "pcicfgr8 %x\n", off);
		if(off < 0x40)
			break;
		off &= ~3;
		c = pcicfgr8(p, off);
		pcicapdbg("\t" "pcicfgr8 %x\n", c);
		if(c == 0xff)
			break;
		if(c == cap)
			return off;
		off++;
	}
	return 0;
}

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
		print("pciecap offset = %ud\n",  off);
		if(off < 0x100 || off >= 4 K - 1)
			return 0;
	}
	print("pciecap found = %ud\n",  off);
	return off;
}

static int
setpcie(Pcidev *p)
{
	int off;

	/* set 4k writes. */
	off = pcicap(p, PciCapPCIe);
	if(off < 64)
		return -1;
	off += PcieCTL;
	pcicfgw16(p, off, (pcicfgr16(p, off) & ~PcieMRD) | 5<<12);
	return 0;
}

static void
namelock(QLock *q, char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	vseprint(q->namebuf, q->namebuf+sizeof q->namebuf, fmt, arg);
	va_end(arg);
	q->name = q->namebuf;
}

static int
whichfw(Pcidev *p)
{
	char *s;
	int i, off, lanes, ecrc;
	uint cap;

	/* check the number of configured lanes */
	off = pcicap(p, PciCapPCIe);
	if(off < 64)
		return -1;
	off += PcieLCR;
	cap = pcicfgr16(p, off);
	lanes = cap>>4 & 0x3f;

	/* check AERC register.  we need it on */
	off = pciecap(p, PcieAERC);
//	print("%d offset\n", off);
	cap = 0;
	if(off != 0){
		off += AercCCR;
		cap = pcicfgr32(p, off);
		print("%ud cap\n", cap);
	}
	ecrc = cap>>4 & 0xf;
	/* if we don't like the aerc, kick it here */

	print("m10g %d lanes; ecrc=%d; ", lanes, ecrc);
	if(s = getconf("myriforce")){
		i = strtoul(s, 0, 0);
		if(i != 4 K || i != 2 K)
			i = 2 K;
		print("fw=%d [forced]\n", i);
		return i;
	}
	if(lanes <= 4){
		print("fw = 4096 [lanes]\n");
		return 4 K;
	}
	if(ecrc & 10){
		print("fw = 4096 [ecrc set]\n");
		return 4K;
	}
	print("fw = 4096 [default]\n");
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
			c->serial = strtoul(s + i + 3, 0, 0);
		}
		i += l;
	}
	if(bits)
		return -1;
	return 0;
}

static ushort
pbit16(ushort i)
{
	ushort j;
	uchar *p;

	p = (uchar*)&j;
	p[1] = i;
	p[0] = i>>8;
	return j;
}

static ushort
gbit16(uchar i[2])
{
	ushort j;

	j = i[1];
	j |= i[0]<<8;
	return j;
}

static uint
pbit32(uint i)
{
	uint j;
	uchar *p;

	p = (uchar*)&j;
	p[3] = i;
	p[2] = i>>8;
	p[1] = i>>16;
	p[0] = i>>24;
	return j;
}

static uint
gbit32(uchar i[4])
{
	uint j;

	j = i[3];
	j |= i[2]<<8;
	j |= i[1]<<16;
	j |= i[0]<<24;
	return j;
}

static void
prepcmd(uint *cmd, int i)
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
	uint buf[16], i;
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

	for(i = 0; i < 15; i++){
		if(cmd->i[1] != Noconf){
			i = gbit32(cmd->c);
			qunlock(&c->cmdl);
			if(cmd->i[1] != 0)
				dprint("[%ux]", i);
			return i;
		}
		delay(1);
	}
	qunlock(&c->cmdl);
	panic("m10g: cmd timeout [%ux %ux] cmd=%d\n", cmd->i[0], cmd->i[1], type);
	return ~0;
}

static uint
maccmd(Ctlr *c, int type, uchar *m)
{
	uint buf[16], i;
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

	for(i = 0; i < 15; i++){
		if(cmd->i[1] != Noconf){
			i = gbit32(cmd->c);
			qunlock(&c->cmdl);
			if(cmd->i[1] != 0)
				dprint("[%ux]", i);
			return i;
		}
		delay(1);
	}
	qunlock(&c->cmdl);
	print("m10g: maccmd timeout [%ux %ux] cmd=%d\n", cmd->i[0], cmd->i[1], type);
	panic(Etimeout);
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
	uint buf[16], i;

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

	for(i = 0; i < 15; i++){
		if(c->cmd->i[1] != Noconf){
			i = gbit32(c->cmd->c);
			if(i == 0)
				return 0;
			return i;
		}
		delay(5);
	}
	panic(Etimeout);
	return ~0;
}

static uint
rdmacmd(Ctlr *c, int on)
{
	uint buf[16], i;

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

	for(i = 0; i < 20; i++){
		if(c->cmd->i[0] == Noconf)
			return gbit32(c->cmd->c);
		delay(1);
	}
	panic(Etimeout);
	return ~0;
}

static int
loadfw(Ctlr *c, int *align)
{
	uint *f, *s, sz;
	int i;

	if((*align = whichfw(c->pcidev)) == 4 K){
		f = (uint*)fw4k;
		sz = sizeof fw4k;
	}else{
		f = (uint*)fw2k;
		sz = sizeof fw2k;
	}

	s = (uint*)(c->ram + Fwoffset);
	for(i = 0; i < sz/4; i++)
		s[i] = f[i];
	return sz&~3;
}

static int
bootfw(Ctlr *c)
{
	int i, sz, align;
	uint buf[16];
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

	chkfw(c);
	cmd(c, Creset, 0, 0);

	cmd(c, CSintrqsz, 0, c->done.n*sizeof *c->done.entry);
	cmd(c, CSintrqdma, 0, c->done.busaddr);
	c->irqack = (uint*)(c->ram + cmd(c, CGirqackoff, 0, 0));
	c->irqdeass = (uint*)(c->ram + cmd(c, CGirqdeassoff, 0, 0));
	c->coal = (uint*)(c->ram + cmd(c, CGcoaloff, 0, 0));
	*c->coal = pbit32(25);

	dprint("dma stats:\n");
	rdmacmd(c, 1);
	sz = c->tx.segsz;
	i = dmatestcmd(c, DMAread, c->done.busaddr, sz);
	print("\t" "read: %ud MB/s\n", ((i>>16)*sz*2)/(i&0xffff));
	i = dmatestcmd(c, DMAwrite, c->done.busaddr, sz);
	print("\t" "write: %ud MB/s\n", ((i>>16)*sz*2)/(i&0xffff));
	i = dmatestcmd(c, DMAwrite|DMAread, c->done.busaddr, sz);
	print("\t" "r/w: %ud MB/s\n", ((i>>16)*sz*2*2)/(i&0xffff));
	memset(c->done.entry, 0, c->done.n*sizeof *c->done.entry);

	maccmd(c, CSmac, c->ra);
	cmd(c, Cenablefc, 0, 0);
	if(e->ifc.maxmtu > 9000)
		e->ifc.maxmtu = 9000;
	cmd(c, CSmtu, 0, e->ifc.maxmtu);

	return 0;
}

static int
setmem(Pcidev *p, Ctlr *c)
{
	uint i, raddr;
	Done *d;
	void *mem;

	c->tx.segsz = 2048;
	c->ramsz = 2 MB - (2*48 K + 32 K) - 0x100;
	if(c->ramsz > p->mem[0].size)
		return -1;

	raddr = p->mem[0].bar & ~0x0F;
	mem = (void*)vmap(raddr, p->mem[0].size);
	if(mem == nil){
		print("m10g: can't map %p\n", p->mem[0].bar);
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
	memset(d->entry, 0, i);
	d->busaddr = PCIWADDR(d->entry);

	c->stats = malign(sizeof *c->stats);
	memset(c->stats, 0, sizeof *c->stats);
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

static void
pkick(Bpool*)
{
	int i;

	for(i = 0; i < nctlr; i++)
		wakeup(&ctlrs[i].rxrendez);
}

static Msgbuf*
balloc(Rx* rx)
{
	Msgbuf *m;

	if((m = rx->pool->head) != nil){
		rx->pool->head = m->next;
		m->next = nil;
		rx->pool->n--;
		m->flags &= ~FREE;
	}
	return m;
}

static void
smbfree(Msgbuf *m)
{
	Bpool *p;

	m->data = (uchar*)PGROUND((uintptr)m->xdata);
	m->count = 0;
	m->flags = FREE;
	p = &smpool;
	ilock(p);
	m->next = p->head;
	p->head = m;
	p->n++;
	p->cnt++;
	if(p->flags & Pstarve && p->n > 16)
		pkick(p);
	iunlock(p);
}

static void
bgbfree(Msgbuf *m)
{
	Bpool *p;

	m->data = (uchar*)PGROUND((uintptr)m->xdata);
	m->count = 0;
	m->flags = FREE;
	p = &bgpool;
	ilock(p);
	m->next = p->head;
	p->head = m;
	p->n++;
	p->cnt++;
	if(p->flags & Pstarve && p->n > 16)
		pkick(p);
	iunlock(p);
}

extern void sfence(void);

/*
 * this is highly optimized to reduce bus cycles with
 * w/c memory while respecting the lanai z model a's
 * limit of 32-bytes writes > 32 bytes must be handled
 * by card f/w.  partial writes are also handled by f/w.
 */

static void
replenish(Rx *rx)
{
	uint buf[16], i, idx, e, f;
	Bpool *p;
	Msgbuf *m;

	p = rx->pool;
	e = (rx->i - rx->cnt) & ~7;
	e += rx->n;
	if(e < 16)
		return;
	ilock(rx->pool);
	while(p->n >= 8 && e){
		idx = rx->cnt & rx->m;
		for(i = 0; i < 8; i++){
			m = balloc(rx);
			buf[i*2 + 0] = pbit32h(PCIWADDR(m->data));
			buf[i*2 + 1] = pbit32(PCIWADDR(m->data));
			rx->host[idx + i] = m;
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
	if(e){
		if(p->n > 7 + 1)
			print("m10g: should panic? pool->n = %d\n", p->n);
		if(e > rx->n/2)
			p->flags |= Pstarve;
	}
	iunlock(rx->pool);
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
		panic(Enomem);
	memset(v, 0, sz);
	return v;
}

static void
open0(Ctlr *c)
{
	int i, sz, entries;
	Msgbuf *m;

	entries = cmd(c, CGsendrgsz, 0, 0)/sizeof *c->tx.lanai;
	c->tx.lanai = (Send*)(c->ram + cmd(c, CGsendoff, 0, 0));
	c->tx.host = emalign(entries*sizeof *c->tx.host);
	c->tx.bring = emalign(entries*sizeof *c->tx.bring);
	c->tx.n = entries;
	c->tx.m = entries - 1;

	entries = cmd(c, CGrxrgsz, 0, 0)/8;
	c->sm.pool = &smpool;
	cmd(c, CSsmallsz, 0, c->sm.pool->size);
	c->sm.lanai = (uint*)(c->ram + cmd(c, CGsmallrxoff, 0, 0));
	c->sm.n = entries;
	c->sm.m = entries - 1;
	c->sm.host = emalign(entries*sizeof *c->sm.host);

	c->bg.pool = &bgpool;
	c->bg.pool->size =  nextpow( /*e->maxmtu*/9000 + 2);	/* 2 byte alignment pad */
	cmd(c, CSbigsz, 0, c->bg.pool->size);
	c->bg.lanai = (uint*)(c->ram + cmd(c, CGbigrxoff, 0, 0));
	c->bg.n = entries;
	c->bg.m = entries - 1;
	c->bg.host = emalign(entries*sizeof *c->bg.host);

	sz = c->sm.pool->size + BY2PG;
	for(i = 0; i < c->sm.n; i++){
		m = mballoc(sz, 0, Mbeth10gbesm);
		m->free = smbfree;
		mbfree(m);
	}
	mballocpool(c->bg.n, c->bg.pool->size, BY2PG, Mbeth10gbebg, bgbfree);

	cmd(c, CSstatsdma2, sizeof *c->stats, c->statsprt);
	c->linkstat = ~0;
	c->nrdma = 15;

	cmd(c, Cetherup, 0, 0);
}

static Msgbuf*
nextbuf(Ctlr *c)
{
	uint i;
	ushort l;
	Slot *s;
	Done *d;
	Msgbuf *m;
	Rx *rx;

	d = &c->done;
	i = d->i&d->m;
	s = (Slot*)(d->entry + i);
	l = s->len;
	if(l == 0)
		return 0;
//	k = s->cksum;
	s->len = 0;
	d->i++;
	l = gbit16((uchar*)&l);
	rx = whichrx(c, l);
	if(rx->i - rx->cnt <= rx->n){
		print("m10g: overrun\n");
		return 0;
	}
	i = rx->i&rx->m;
	m = rx->host[i];
	rx->host[i] = 0;
	if(m == 0){
		print("m10g: rx to no block\n");
		return 0;
	}
	rx->i++;
	m->flags |= Bipck|Btcpck|Budpck;
	m->data += 2;
	m->count = l;
	return m;
}

static int
rxcansleep(void *v)
{
	uint *e;
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
m10rx(void)
{
	int i, l;
	Msgbuf *m;
	Ctlr *c;
	Ether *e;

	e = u->arg;
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
		for(i = 0; i < l && (m = nextbuf(c)); i++)
			etheriq(e, m);
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
	Msgbuf *m;

	for(l = 0; l < tx->m; l++){
		if(tx->npkt == n)
			break;
		if(tx->cnt == tx->i){
			dprint("m10g: txcleanup cnt == i %ud\n", tx->i);
			break;
		}
		j = tx->cnt & tx->m;
		if(m = tx->bring[j]){
			tx->bring[j] = 0;
			tx->nbytes += m->count;
			mbfree(m);
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
	v = h[i0].fword;
	h[i0].flags = 0;
	for(i = n - 1; i >= 0; i--)
		memmove(l+(i+i0&m), h+(i+i0&m), sizeof *h);
	sfence();
	l[i0].fword = v;
	tx->i += n;
	sfence();
}

static void
m10gtransmit(Ether *e)
{
	uchar flags;
	ushort slen;
	uint nseg, end, bus, len, segsz;
	Ctlr *c;
	Msgbuf *m;
	Tx *tx;
	Send *s0, *s, *se;

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
		if((m = etheroq(e)) == nil){
			recv(e->ifc.reply, 0);
			continue;
		}
		flags = SFfirst|SFnotso;
		if((len = m->count) < 1520)
			flags |= SFsmall;
		bus = PCIWADDR(m->data);
		s0 = s;
		nseg = 0;
		for(; len; len -= slen){
			end = bus+segsz & ~(segsz-1);
			slen = end - bus;
			if(slen > len)
				slen = len;
			s->low = pbit32(bus);
			s->high = pbit32h(bus);
			s->len = pbit16(slen);
			s->flags = flags;
			s->nrdma = 1;

			bus += slen;
			if(++s == se)
				s = tx->host;
			flags &= ~SFfirst;
			nseg++;
		}
		s0->nrdma = nseg;
		tx->bring[tx->i+nseg-1 & tx->m] = m;
		submittx(tx, nseg);
		tx->submit++;
	}
}

static void
checkstats(Ether *, Ctlr *c, Stats *s)
{
	uint i;

	if(s->updated == 0)
		return;

	i = gbit32(s->linkstat);
	if(c->linkstat != i){
//		e->link = i;
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
	if(valid == 0)
		return;
	if(c->msi == 0){
		*c->irqdeass = 0;
		mfence();
	}
//	else
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

	qlock(e->ctlr);
	c = e->ctlr;
	if(c->state != Attached){
		qunlock(c);
		return;
	}
	if(c->kprocs == 0){
		c->kprocs++;
		snprint(c->rname, sizeof c->rname, "#l%drx", e->ctlrno);
		userinit(m10rx, e, c->rname);
	}
	c->state = Runed;
	qunlock(c);
}

static int
lstcount(Msgbuf *m)
{
	int i;

	i = 0;
	for(; m; m = m->next)
		i++;
	return i;
}

static char ifstatbuf[2 K];

static void
cifstat(Ctlr *c, int, char **)
{
	Stats s;
	int i, n;

	/* no point in locking this because this is done via dma */
	memmove(&s, c->stats, sizeof s);
	snprint(ifstatbuf, sizeof ifstatbuf,
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
		"speeds 0:%ud 10000:%ud\n",
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

	/* HACK */
	n = strlen(ifstatbuf);
	for(i = 0; n-i > 0; i += PRINTSIZE)
		print("%s", ifstatbuf+i);
}

static void
cdebug(Ctlr *, int, char**)
{
	debug ^= debug;
	print("debug %d\n", debug);
}

static void
ccoal(Ctlr *c, int n, char **v)
{
	int i;

	if(n == 1){
		i = strtoul(*v, 0, 0);
		*c->coal = pbit32(i);
		coherence();
	}
	print("%d\n", gbit32((uchar*)c->coal));
}

static void
chelp(Ctlr*, int, char **)
{
	print("coal ctlr n	-- get/set interrupt colesing delay\n");
	print("debug	-- set debug (all ctlrs)\n");
	print("ifstat ctlr	-- print statistics\n");
}

typedef struct{
	void	(*f)(Ctlr *, int, char**);
	char*	name;
	int	minarg;
	int	maxarg;
}Cmdtab;

static void
docmd(Cmdtab *t, int n, int c, char **v)
{
	int i;

	i = n;
	if(c > 0){
		for(i = 0; i < n; i++)
			if(strcmp(*v, t[i].name) == 0)
				break;
		c--;
		v++;
	}
	if(i >= n){
		i = n-1;
		c = 0;
	}
	t += i;
	if(c < t->minarg)
		print("too few args, need %d\n", t->minarg);
	else if(c > t->maxarg)
		print("too many args, max %d\n", t->maxarg);
	else{
		i = 0;
		if(t->minarg > 0){
			i = strtoul(*v++, 0, 0);
			c--;
		}
		if(i < 0 || i >= nctlr)
			print("bad controller %d\n", i);
		else
			t->f(ctlrs+i, c, v);
	}
}

static Cmdtab ctab[] = {
	cdebug,	"debug",		0,	0,
	ccoal,	"coal",		1,	2,
	cifstat,	"ifstat",		1,	1,
	chelp,	"help",		0,	100,
};

static void
m10gctl(int c, char **v)
{
	docmd(ctab, nelem(ctab), c-1, v+1);
}

static void
m10gpci(void)
{
	Ctlr *c;
	Pcidev *p;

	for(p = 0; p = pcimatch(p, 0x14c1, 0x0008); ){
		c = ctlrs+nctlr;
		memset(c, 0, sizeof c);
		c->pcidev = p;
		c->boot = pcicap(p, PciCapVND);
//		kickthebaby(p, c);
		pcisetbme(p);
		if(setmem(p, c) == -1){
			print("m10g: init failed\n");
			continue;
		}
		namelock(c, "my%d", nctlr);
		namelock(&c->cmdl, "my%d.cmd", nctlr);
		namelock(&c->tx, "my%d.tx", nctlr);
		if(++nctlr == nelem(ctlrs))
			break;
	}
}

int
m10gpnp(Ether *e)
{
	Ctlr *c;
	static int once, cmd;

	if(once++ == 0)
		m10gpci();
	for(c = ctlrs; c < ctlrs+nctlr; c++)
		if(c->active)
			continue;
		else if(e->port == 0 || e->port == c->port)
			break;
	if(c == ctlrs+nctlr)
		return -1;
	c->active = 1;

	e->ctlr = c;
	e->port = c->port;
	e->irq = c->pcidev->intl;
	e->tbdf = c->pcidev->tbdf;
	e->mbps = 10000;
	e->ifc.maxmtu = 9000;
	memmove(e->ea, c->ra, Easize);

	reset(e, c);
	open0(c);
	c->state = Attached;

	e->attach = m10gattach;
	e->transmit = m10gtransmit;
	e->interrupt = m10ginterrupt;
	if(cmd++ == 0)
		cmd_install("myrictl", "tweak myri parameters", m10gctl);

	return 0;
}
