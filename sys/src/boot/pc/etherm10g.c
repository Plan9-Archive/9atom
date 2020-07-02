/*
 *	myricom 10 gbit ethernet
 *	© 2010 erik quanstrom, coraid, inc.
 */

#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "etherif.h"

#define		qlock(i)		
#define		qunlock(i)	
#define		wakeup(i)	while(0)

#define		K	* 1024
#define		MB	* 1024 K

#define	dprint(...)	if(0) print(__VA_ARGS__); else {}
#define malign(n)	xspanalloc(n, 4 K, 0)
#define if64(...)		(sizeof(uintptr) == 8? (__VA_ARGS__): 0)
#define pbit32h(x)	if64(pbit32((uvlong)x >> 32))

enum {
	Epromsz	= 256,
	Maxslots	= 128,			/* 1024? */
	Rbalign	= BY2PG,
	Noconf	= 0xffffffff,
	Fwoffset	= 1 MB,
	Hdroff	= 0x00003c,
	Cmdoff	= 0xf80000,		/* offset of command port */
	Fwsubmt	= 0xfc0000,		/* offset of firmware submission command port */
	Rdmaoff	= 0xfc01c0,		/* offset of rdma command port */
	Cmderr	= 0x01000001,
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
} Send;

typedef struct {
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
	vlong	nbytes;
} Tx;

enum {
	Pstarve	= 1<<0,
};

typedef struct {
	Lock;
	uint	size;		/* buffer size of each block */
	uint	n;		/* n free buffers. */
	uint	cnt;
//	uint	flags;
} Bpool;

typedef struct {
	Bpool	*pool;		/* free buffers */
	uint	*lanai;		/* rx ring; we have no perminant host shadow. */
	Block	**host;		/* called "info" in myricom driver */
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
	int	state;
	uintptr	port;
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

	void	*cmdl;
	Cmd	*cmd;			/* address of command return */
	uintptr	cprt;			/* bus address of command */

	Done	done;
	Tx	tx;
	Rx	sm;
	Rx	bg;
	Stats	*stats;
	uintptr	statsprt;
	uint	speed[2];

//	Rendez	txrendez;
	int	txrendez;

	int	msi;
	uint	linkstat;
	uint	nrdma;
} Ctlr;

static	Bpool	smpool 	= {.size	= 2048, };
static	Bpool	bgpool	= {.size = 2048,};
static	Ctlr 	*ctlrs;

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

static ushort
pbit16(u16int i)
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
pbit32(u32int i)
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
	print("m10g: cmd timeout [%ux %ux] cmd=%d\n", cmd->i[0], cmd->i[1], type);
	return Cmderr;	/* hopefully a bogus value! */
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
	print("m10g: maccmd timeout\n");
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

	for(i = 0; i < 20; i++){
		if(c->cmd->i[0] == Noconf)
			return 0;
		delay(1);
	}
	print("m10g: rdmacmd timeout\n");
	return Cmderr;
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
	dprint("\t" "type	%.8ux\n", type);
	dprint("\t" "vers	%s\n", h->version);
	dprint("\t" "ramsz	%ux\n", gbit32(h->ramsz));
	if(type != Teth){
		print("m10g: bad card type %.8ux\n", type);
		return -1;
	}
	return rdmacmd(c, 0);
}

static int
reset(Ether*, Ctlr *c)
{
	if(chkfw(c) == -1){
err:
		print("m10g: reset error\n");
		return -1;
	}
	if(cmd(c, Creset, 0, 0) == Cmderr){
		print("reset fails\n");
		goto err;
	}
	if(cmd(c, CSintrqsz, 0, c->done.n*sizeof *c->done.entry) == Cmderr)
		goto err;
	if(cmd(c, CSintrqdma, 0, c->done.busaddr) == Cmderr)
		goto err;
	c->irqack = (u32int*)(c->ram + cmd(c, CGirqackoff, 0, 0));
	c->irqdeass = (u32int*)(c->ram + cmd(c, CGirqdeassoff, 0, 0));
	c->coal = (u32int*)(c->ram + cmd(c, CGcoaloff, 0, 0));
	*c->coal = pbit32(80);

	if(rdmacmd(c, 1) == Cmderr)
		goto err;
	memset(c->done.entry, 0, c->done.n*sizeof *c->done.entry);

	if(maccmd(c, CSmac, c->ra) == Cmderr)
		goto err;
	if(cmd(c, Cenablefc, 0, 0) == Cmderr)
		goto err;
	if(cmd(c, CSmtu, 0, 1520) == Cmderr)
		goto err;
	return 0;
}

static int
setmem(Pcidev *p, Ctlr *c)
{
	uint i;
	uintptr raddr;
	void *mem;
	Done *d;

	c->tx.segsz = 2048;
	c->ramsz = 2 MB - (2*48 K + 32 K) - 0x100;
	if(c->ramsz > p->mem[0].size)
		return -1;

	raddr = p->mem[0].bar & ~0x0F;
	mem = (void*)upamalloc(raddr, p->mem[0].size, 0);
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
	return parseeprom(c);
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
	Block *b;

	e = (rx->i - rx->cnt) & ~7;
	e += 16;	/* was: rx->n; only allocate a few entries */
	if(e > rx->n)
		return;
	ilock(rx->pool);
	while(e){
		idx = rx->cnt & rx->m;
		for(i = 0; i < 8; i++){
			b = allocb(2048 + BY2PG);
			b->wp = (uchar*)ROUNDUP((uintptr)b->base, BY2PG);
			b->rp = b->wp;
			buf[i*2 + 0] = pbit32h(PCIWADDR(b->wp));
			buf[i*2 + 1] = pbit32(PCIWADDR(b->wp));
			rx->host[idx + i] = b;
		}
		f = buf[1];
		buf[1] = ~0;
		memmove(rx->lanai + 2*idx, buf, sizeof buf / 2);
		coherence();
		memmove(rx->lanai + 2*(idx + 4), buf + 8, sizeof buf / 2);
		rx->lanai[2*idx + 1] = f;
		coherence();
		rx->cnt += 8;
		e -= 8;
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
		panic("m10g: no memory");
	memset(v, 0, sz);
	return v;
}

static void
open0(Ether *, Ctlr *c)
{
	int entries;

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
	c->bg.pool->size =  nextpow(1500 + 2);	/* 2 byte alignment pad */
	cmd(c, CSbigsz, 0, c->bg.pool->size);
	c->bg.lanai = (u32int*)(c->ram + cmd(c, CGbigrxoff, 0, 0));
	c->bg.n = entries;
	c->bg.m = entries - 1;
	c->bg.host = emalign(entries*sizeof *c->bg.host);

	cmd(c, CSstatsdma2, sizeof *c->stats, c->statsprt);
	c->linkstat = ~0;
	c->nrdma = 15;

	replenish(&c->sm);

	cmd(c, Cetherup, 0, 0);
}

static Rx*
whichrx(Ctlr *c, int sz)
{
	if(sz <= smpool.size)
		return &c->sm;
	return &c->bg;
}

static Block*
nextblock(Ctlr *c)
{
	uint i;
	u16int l;
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

//	b->flag |= Bipck|Btcpck|Budpck;
	b->rp += 2;
	b->wp += 2 + l;
	return b;
}

static void
etheriq(Ether *e, Block *b, int)
{
	toringbuf(e, b->rp, BLEN(b));
	freeb(b);
}

static void
irqrx(Ether *e)
{
	int n;
	Block *b;
	Ctlr *c;

	c = e->ctlr;

	for(;;){
		for(n = 0; n < 7; n++){
			b = nextblock(c);
			if(b == nil)
				break;
			etheriq(e, b, 1);
		}
		replenish(&c->sm);
		if(n == 0)
			break;
	}
	c->irqack[0] = pbit32(3);
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
	v = h[i0].fword;
	h[i0].flags = 0;
	for(i = n - 1; i >= 0; i--)
		memmove(l+(i+i0&m), h+(i+i0&m), sizeof *h);
	coherence();
	l[i0].fword = v;
	tx->i += n;
	coherence();
}

static Block*
rbget(Ether *e)
{
	RingBuf *r;
	Block *b;

	r = e->tb + e->ti;
	if(r->owner != Interface)
		return nil;
	b = fromringbuf(e);
	r->owner = Host;
	e->ti = NEXT(e->ti, e->ntb);
	return b;
}

static void
m10gtransmit(Ether *e)
{
	uchar flags;
	u16int slen;
	uint nseg, len, segsz;
	uintptr bus, end;
	Ctlr *c;
	Block *b;
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
			continue;
		}
		if((b = rbget(e)) == nil)
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
		tx->bring[tx->i+nseg-1 & tx->m] = b;
		submittx(tx, nseg);
		tx->submit++;
	}
}

static void
checkstats(Ether *e, Ctlr *c, Stats *s)
{
	uint i;

	if(s->updated == 0)
		return;

	i = gbit32(s->linkstat);
	if(c->linkstat != i){
		c->speed[i>0]++;
		if(c->linkstat = i){
			dprint("m10g: %d: link up\n", e->ctlrno);
			c->tx.starve = 0;
			wakeup(&c->txrendez);
		}else
			dprint("m10g: %d: link down\n", e->ctlrno);
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
	if(/*c->state != Runed ||*/ valid == 0)
		return;
	if(c->msi == 0)
		*c->irqdeass = 0;
	else
		c->stats->valid = 0;
	waitintx(c);
	checkstats(e, c, c->stats);
	if(valid&1)
		irqrx(e);
	c->irqack[1] = pbit32(3);
}

static void
m10gattach(Ether *e)
{
	Ctlr *c;

	dprint("m10g: attach\n");
	qlock(e->ctlr);
	c = e->ctlr;
	if(c->state != Detached){
		qunlock(c);
		return;
	}
//	if(reset(e, c) == -1){
//		c->state = Detached;
//		return;
//	}
	c->state = Attached;
	open0(e, c);
	c->state = Runed;
	qunlock(c);
	m10ginterrupt(nil, e);		/* botch: racy */
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
//	vunmap(c->ram, c->pcidev->mem[0].size);
//	ctlrfree(c);
	return -1;
}

static void
m10gshutdown(Ether *e)
{
	m10gdetach(e->ctlr);
}

static void
m10gpci(void)
{
	Ctlr **t, *c;
	Pcidev *p;

	t = &ctlrs;
	for(p = 0; p = pcimatch(p, 0x14c1, 0x0008); ){
		c = malloc(sizeof *c);
		if(c == nil)
			continue;
		c->pcidev = p;
		pcisetbme(p);
		if(setmem(p, c) == -1 || reset(nil, c) == -1){
			print("m10g: init failed\n");
			free(c);
			continue;
		}
		pcisetirqen(p);		/* move to setvec? */
		*t = c;
		t = &c->next;
	}
}

/*static*/ int
m10gpnp(Ether *e)
{
	Ctlr *c;
	static int once;

	if(once == 0){
		once++;
		m10gpci();
	}
	for(c = ctlrs; c != nil; c = c->next)
		if(c->active)
			continue;
		else if(e->port == 0 || e->port == c->port)
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
	e->transmit = m10gtransmit;
	e->interrupt = m10ginterrupt;
	e->detach = m10gshutdown;

	return 0;
}

//void
//etherm10glink(void)
//{
//	addethercard("m10g", m10gpnp);
//}
