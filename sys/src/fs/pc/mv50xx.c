/*
 * fileserver mv50xx driver.
 */

#include "all.h"
#include "io.h"
#include "mem.h"

enum {
	NCtlr		= 2,
	NCtlrdrv		= 8,
	NDrive		= NCtlr*NCtlrdrv,

	Read 		= 0,
	Write,
};

#define dprint(...)	//print(__VA_ARGS__)
#define idprint(...)	
#define ioprint(...)

enum {
	Rbufps		= RBUFSIZE/512,

	/* Addresses of ATA register */
	ARcmd		= 027,
	ARdev		= 026,
	ARerr		= 021,
	ARfea		= 021,
	ARlba2		= 025,
	ARlba1		= 024,
	ARlba0		= 023,
	ARseccnt	= 022,
	ARstat		= 027,

	ATAerr		= 1<<0,
	ATAdrq		= 1<<3,
	ATAdf 		= 1<<5,
	ATAdrdy 	= 1<<6,
	ATAbusy 	= 1<<7,
	ATAabort	= 1<<2,
	ATAobs		= 1<<1 | 1<<2 | 1<<4,
	ATAeIEN	= 1<<1,
	ATAsrst		= 1<<2,
	ATAhob		= 1<<7,
	ATAbad		= ATAbusy|ATAdf|ATAdrq|ATAerr,

	SFrun		= 1<<0,
	SFdone 		= 1<<1,
	SFerror 		= 1<<2,
	SFretry		= 1<<3,

	PRDeot		= 1<<15,

	/* EDMA interrupt error cause register */

	ePrtDataErr	= 1<<0,
	ePrtPRDErr	= 1<<1,
	eDevErr		= 1<<2,
	eDevDis		= 1<<3,	
	eDevCon	= 1<<4,
	eOverrun	= 1<<5,
	eUnderrun	= 1<<6,
	eSelfDis		= 1<<8,
	ePrtCRQBErr	= 1<<9,
	ePrtCRPBErr	= 1<<10,
	ePrtIntErr	= 1<<11,
	eIORdyErr	= 1<<12,

	// flags for sata 2 version
	eSelfDis2	= 1<<7,
	SerrInt		= 1<<5,

	/* EDMA Command Register */

	eEnEDMA	= 1<<0,
	eDsEDMA 	= 1<<1,
	eAtaRst 		= 1<<2,

	/* Interrupt mask for errors we care about */
	IEM		= (eDevDis | eDevCon | eSelfDis),
	IEM2		= (eDevDis | eDevCon | eSelfDis2),

	/* drive states */
	Dnull 		= 0,
	Dnew,
	Dready,
	Derror,
	Dmissing,
	Dreset,
	Dlast,

	/* drive flags */
	Dllba	 	= 1<<0,

	// phyerrata magic crap
	Mpreamp	= 0x7e0,
	Dpreamp	= 0x720,

	REV60X1B2	= 0x7,
	REV60X1C0	= 0x9,

};

static char* diskstates[Dlast] = {
	"null",
	"new",
	"ready",
	"error",
	"missing",
};

typedef struct Ctlr Ctlr;

typedef struct{
	ulong	status;
	ulong	serror;
	ulong	sctrl;
	ulong	phyctrl;
	ulong	phymode3;
	ulong	phymode4;
	uchar	fill0[0x14];
	ulong	phymode1;
	ulong	phymode2;
	char	fill1[8];
	ulong	ctrl;
	char	fill2[0x34];
	ulong	phymode;
	char	fill3[0x88];
}Bridge;				// must be 0x100 hex in length

typedef struct{
	ulong	config;		/* satahc configuration register (sata2 only) */
	ulong	rqop;		/* request queue out-pointer */
	ulong	rqip;		/* response queue in pointer */
	ulong	ict;		/* inerrupt caolescing threshold */
	ulong	itt;		/* interrupt timer threshold */
	ulong	ic;		/* interrupt cause */
	ulong	btc;		/* bridges test control */
	ulong	bts;		/* bridges test status */
	ulong	bpc;		/* bridges pin configuration */
	char	fill1[0xdc];
	Bridge	bridge[4];
}Arb;

typedef struct{
	ulong	config;		/* configuration register */
	ulong	timer;
	ulong	iec;		/* interrupt error cause */
	ulong	iem;		/* interrupt error mask */

	ulong	txbasehi;		/* request queue base address high */
	ulong	txi;		/* request queue in pointer */
	ulong	txo;		/* request queue out pointer */

	ulong	rxbasehi;		/* response queue base address high */
	ulong	rxi;		/* response queue in pointer */
	ulong	rxo;		/* response queue out pointer */

	ulong	ctl;		/* command register */
	ulong	testctl;		/* test control */
	ulong	status;
	ulong	iordyto;		/* IORDY timeout */
	char	fill[0x18];
	ulong	sataconfig;	/* sata 2 */
	char	fill[0xac];
	ushort	pio;		/* data register */
	char	pad0[2];
	uchar	err;		/* features and error */
	char	pad1[3];
	uchar	seccnt;		/* sector count */
	char	pad2[3];
	uchar	lba0;
	char	pad3[3];
	uchar	lba1;
	char	pad4[3];
	uchar	lba2;
	char	pad5[3];
	uchar	lba3;
	char	pad6[3];
	uchar	cmdstat;		/* cmd/status */
	char	pad7[3];
	uchar	altstat;		/* alternate status */
	uchar	fill2[0x1df];
	Bridge	port;
	char	fill3[0x1c00];	/* pad to 0x2000 bytes */
}Edma;

// there are 4 drives per chip.  thus an 8-port
// card has two chips.
typedef struct{
	Arb	*arb;
	Edma	*edma;
}Chip;

typedef struct{
	ulong	pa;		/* byte address of physical memory */
	ushort	count;		/* byte count (bit0 must be 0) */
	ushort	flag;
	ulong	zero;		/* high long of 64 bit address */
	ulong	reserved;
}Prd;

typedef struct{
	ulong	prdpa;		/* physical region descriptor table structures */
	ulong	zero;		/* must be zero (high long of prd address) */
	ushort	flag;		/* control flags */
	ushort	regs[11];
}Tx;

typedef struct{
	ushort	cid;		/* cID of response */
	uchar	cEdmaSts;	/* EDMA status */
	uchar	cDevSts;		/* status from disk */
	ulong	ts;		/* time stamp */
}Rx;

enum{
	DMautoneg,
	DMsatai,
	DMsataii,
};

typedef struct{
	Lock;

	Ctlr	*ctlr;
	ulong	magic;

	Bridge	*bridge;
	Edma	*edma;
	Chip	*chip;
	int	chipx;

	int	state;
	int	flag;
	uvlong	sectors;
	ulong	pm2;		// phymode 2 init state
	ulong	intick;		// check for hung westerdigital drives.
	int	wait;
	int	mode;		// DMautoneg, satai or sataii.

	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];

	ushort	info[256];

	Prd	*prd;
	Tx	*tx;
	Rx	*rx;

	QLock;
	Rendez;
	int	cmdflag;

	int	driveno;		/* ctlr*NCtlrdrv + unit */
	int	fflag;
	Filter	rate[2];
	int	init;
}Drive;
#pragma	varargck	type	"m"	Drive*

struct Ctlr{
	Lock;

	int	irq;
	int	tbdf;
	int	rid;
	int	type;
	Pcidev	*pcidev;

	uchar	*mmio;
	ulong	*lmmio;
	Chip	chip[2];
	int	nchip;
	Drive	drive[NCtlrdrv];
	int	ndrive;
};

static Ctlr	mvctlr[NCtlr];
static int		nmvctlr;


static ushort
gbit16(void *a)
{
	uchar *i;
	ushort j;

	i = a;
	j = i[1]<<8;
	j |= i[0];
	return j;
}

static u32int
gbit32(void *a)
{
	uchar *i;
	u32int j;

	i = a;
	j = i[3]<<24;
	j |= i[2]<<16;
	j |= i[1]<<8;
	j |= i[0];
	return j;
}

static uvlong
gbit64(void *a)
{
	uchar *i;

	i = a;
	return (uvlong) gbit32(i+4)<<32|gbit32(a);
}


static void
idmove(char *p, ushort *a, int n)
{
	char *op, *e;
	int i;

	op = p;
	for(i = 0; i < n/2; i++){
		*p++ = a[i]>>8;
		*p++ = a[i];
	}
	*p = 0;
	while(p > op && *--p == ' ')
		*p = 0;
	e = p;
	p = op;
	while(*p == ' ')
		p++;
	memmove(op, p, n-(e-p));
}

/*
 * Wait for a byte to be a particular value.
 */
static int
satawait(volatile uchar *p, uchar mask, uchar v, int ms)
{
	int i;

	for(i=0; i<ms && (*p & mask) != v; i++)
		microdelay(1000);
	return (*p & mask) == v;
}

/*
 * Drive initialization
 */

// unmask in the pci registers err done
static void
unmask(ulong *mmio, int port, int coal)
{
	port &= 7;
	if(coal)
		coal = 1;
	if (port < 4)
		mmio[0x1d64/4] |= (3 << (((port&3)*2)) | (coal<<8));
	else
		mmio[0x1d64/4] |= (3 << (((port&3)*2+9)) | (coal<<17));
}

static void
mask(ulong *mmio, int port, int coal)
{
	port &= 7;
	if(coal)
		coal = 1;
	if (port < 4)
		mmio[0x1d64/4] &= ~(3 << (((port&3)*2)) | (coal<<8));
	else
		mmio[0x1d64/4] &= ~(3 << (((port&3)*2+9)) | (coal<<17));
}

/* I give up, marvell.  You win. */
static void
phyerrata(Drive *d)
{
	ulong n, m;
	enum { BadAutoCal = 0xf << 26, };

	if (d->ctlr->type == 1)
		return;
	microdelay(200);
	n = d->bridge->phymode2;
	while ((n & BadAutoCal) == BadAutoCal) {
		dprint("%m: badautocal\n", d);
		n &= ~(1<<16);
		n |= (1<<31);
		d->bridge->phymode2 = n;
		microdelay(200);
		d->bridge->phymode2 &= ~((1<<16) | (1<<31));
		microdelay(200);
		n = d->bridge->phymode2;
	}
	n &= ~(1<<31);
	d->bridge->phymode2 = n;
	microdelay(200);

	/* abra cadabra!  (random magic) */
	m = d->bridge->phymode3;
	m &= ~0x7f800000;
	m |= 0x2a800000;
	d->bridge->phymode3 = m;

	/* fix phy mode 4 */
	m = d->bridge->phymode3;
	n = d->bridge->phymode4;
	n &= ~(1<<1);
	n |= 1;
	switch(d->ctlr->rid){
	case REV60X1B2:
	default:
		d->bridge->phymode4 = n;
		d->bridge->phymode3 = m;
		break;
	case REV60X1C0:
		d->bridge->phymode4 = n;
		break;
	}

	/* revert values of pre-emphasis and signal amps to the saved ones */
	n = d->bridge->phymode2;
	n &= ~Mpreamp;
	n |= d->pm2;
	n &= ~(1<<16);
	d->bridge->phymode2 = n;
}

static void
edmacleanout(Drive *d)
{
	if(d->cmdflag&SFrun){
		d->cmdflag |= SFerror|SFdone;
		wakeup(d);
	}
}

static void
resetdisk(Drive *d)
{
	ulong n;

	dprint("%m: resetdisk\n", d);
	d->sectors = 0;
	if (d->ctlr->type == 2) {
		// without bit 8 we can boot without disks, but
		// inserted disks will never appear.  :-X
		n = d->edma->sataconfig;
		n &= 0xff;
		n |= 0x9b1100;
		d->edma->sataconfig = n;
		n = d->edma->sataconfig;	//flush
		USED(n);
	}
	d->edma->ctl = eDsEDMA;
	microdelay(1);
	d->edma->ctl = eAtaRst;
	microdelay(25);
	d->edma->ctl = 0;
	if (satawait((uchar *)&d->edma->ctl, eEnEDMA, 0, 3*1000) == 0)
		print("%m: eEnEDMA never cleared on reset\n", d);
	edmacleanout(d);
	phyerrata(d);
	d->bridge->sctrl = 0x301 | (d->mode << 4);
	d->state = Dmissing;
}

static void
edmainit(Drive *d)
{
	if(d->tx != nil)
		return;

	d->tx = ialloc(32*sizeof(Tx), 1024);
	d->rx = ialloc(32*sizeof(Rx), 256);
	d->prd = ialloc(32*sizeof(Prd), 32);
	for(int i = 0; i < 32; i++)
		d->tx[i].prdpa = PADDR(d->prd+i);
	coherence();
}

static int
configdrive(Ctlr *ctlr, Drive *d)
{
	dprint("%m: configdrive\n", d);
	edmainit(d);
	d->mode = DMsatai;
	if(d->ctlr->type == 1){
		d->edma->iem = IEM;
		d->bridge = &d->chip->arb->bridge[d->chipx];
	}else{
		d->edma->iem = IEM2;
		d->bridge = &d->chip->edma[d->chipx].port;
		d->edma->iem = ~(1<<6);
		d->pm2 = Dpreamp;
		if(d->ctlr->lmmio[0x180d8/4] & 1)
			d->pm2 = d->bridge->phymode2 & Mpreamp;
	}
	resetdisk(d);
	unmask(ctlr->lmmio, d->driveno, 0);
	delay(100);
	if(d->bridge->status){
		dprint("\t" "bridge %lx\n", d->bridge->status);
		delay(1400);		// don't burn out the power supply.
	}
	return 0;
}

static int
enabledrive(Drive *d)
{
	Edma *edma;

	dprint("%m: enabledrive..", d);

	if((d->bridge->status & 0xf) != 3){
		dprint("%m: not present\n", d);
		d->state = Dmissing;
		return -1;
	}
	edma = d->edma;
	if(satawait(&edma->cmdstat, ATAbusy, 0, 5*1000) == 0){
		dprint("%m: busy timeout\n", d);
		d->state = Dmissing;
		return -1;
	}
	edma->iec = 0;
	d->chip->arb->ic &= ~(0x101 << d->chipx);
	edma->config = 0x51f;
	if (d->ctlr->type == 2)
		edma->config |= 7<<11;
	edma->txi = PADDR(d->tx);
	edma->txo = (ulong)d->tx & 0x3e0;
	edma->rxi = (ulong)d->rx & 0xf8;
	edma->rxo = PADDR(d->rx);
	edma->ctl |= 1;		/* enable dma */

	if(d->bridge->status == 0x113){
		dprint("%m: new\n", d);
		d->state = Dnew;
	}else
		print("%m: status not forced (should be okay)\n", d);
	return 0;
}

static int
setudmamode(Drive *d, uchar mode)
{
	Edma *edma;

	dprint("%m: setudmamode %d\n", d, mode);

	edma = d->edma;
	if(edma == nil)
		panic("%m: setudamode: zero d->edma\m", d);
	if(satawait(&edma->cmdstat, ~ATAobs, ATAdrdy, 9*1000) == 0){
		print("%m: cmdstat 0x%.2ux ready timeout\n", d, edma->cmdstat);
		return 0;
	}
	edma->altstat = ATAeIEN;
	edma->err = 3;
	edma->seccnt = 0x40 | mode;
	edma->cmdstat = 0xef;
	microdelay(1);
	if(satawait(&edma->cmdstat, ATAbusy, 0, 5*1000) == 0){
		print("%m: cmdstat 0x%.2ux busy timeout\n", d, edma->cmdstat);
		return 0;
	}
	return 1;
}

static int
identifydrive(Drive *d)
{
	int i;
	ushort *id;
	Edma *edma;

	dprint("%m: identifydrive\n", d);

	if(setudmamode(d, 5) == 0)
		goto Error;

	id = d->info;
	memset(d->info, 0, sizeof d->info);
	edma = d->edma;
	if(satawait(&edma->cmdstat, ~ATAobs, ATAdrdy, 5*1000) == 0)
		goto Error;

	edma->altstat = ATAeIEN;	/* no interrupts */
	edma->cmdstat = 0xec;
	microdelay(1);
	if(satawait(&edma->cmdstat, ATAbusy, 0, 5*1000) == 0)
		goto Error;
	for(i = 0; i < 256; i++)
		id[i] = edma->pio;
	if(edma->cmdstat & ATAbad)
		goto Error;
	i = gbit16(id+83) | gbit16(id+86);
	if(i & (1<<10)){
		d->flag |= Dllba;
		d->sectors = gbit64(id+100);
	}else{
		d->flag &= ~Dllba;
		d->sectors = gbit32(id+60);
	}
	idmove(d->serial, id+10, 20);
	idmove(d->firmware, id+23, 8);
	idmove(d->model, id+27, 40);

	if(enabledrive(d) == 0) {
		d->state = Dready;
		print("%m: LLBA %lld sectors\n", d, d->sectors);
		return 0;
	}
	d->state = Derror;
	return -1;
Error:
	dprint("error...");
	d->state = Derror;
	return -1;
}

/* p. 163:
	M	recovered error
	P	protocol error
	N	PhyRdy change
	W	CommWake
	B	8-to-10 encoding error
	D	disparity error
	C	crc error
	H	handshake error
	S	link sequence error
	T	transport state transition error
	F	unrecognized fis type
	X	device changed
*/

static char stab[] = {
[1]	'M',
[10]	'P',
[16]	'N',
[18]	'W', 'B', 'D', 'C', 'H', 'S', 'T', 'F', 'X'
};
static ulong sbad = (7<<20)|(3<<23);

static void
serrdecode(ulong r, char *s, char *e)
{
	int i;

	e -=3;
	for(i = 0; i < nelem(stab) && s < e; i++){
		if((r&(1<<i)) && stab[i]){
			*s++ = stab[i];
			if(sbad&(1<<i))
				*s++ = '*';
		}
	}
	*s = 0;
}

char *iectab[] = {
	"ePrtDataErr",
	"ePrtPRDErr",
	"eDevErr",
	"eDevDis",
	"eDevCon",
	"SerrInt",
	"eUnderrun",
	"eSelfDis2",
	"eSelfDis",
	"ePrtCRQBErr",
	"ePrtCRPBErr",
	"ePrtIntErr",
	"eIORdyErr",
};

static char*
iecdecode(ulong cause)
{
	int i;

	for(i = 0; i < nelem(iectab); i++)
		if(cause&(1<<i))
			return iectab[i];
	return "";
}

enum{
	Cerror	= ePrtDataErr|ePrtPRDErr|eDevErr|eSelfDis2|ePrtCRPBErr|ePrtIntErr,
};

static void
updatedrive(Drive *d)
{
	ulong cause;
	Edma *edma;
	char buf[32+4+1];

	edma = d->edma;
	if((edma->ctl&eEnEDMA) == 0){
		// FEr SATA#4 50xx
		cause = edma->cmdstat;
		USED(cause);
	}
	cause = edma->iec;
	if(cause == 0)
		return;
	dprint("%m: cause %08ulx [%s]\n", d, cause, iecdecode(cause));
	if(cause & eDevCon)
		d->state = Dnew;
	if(cause&eDevDis && d->state == Dready)
		print("%m: pulled: st=%08ulx\n", d, cause);
	switch(d->ctlr->type){
	case 1:
		if(cause&eSelfDis)
			d->state = Derror;
		break;
	case 2:
		if(cause&Cerror)
			d->state = Derror;
		if(cause&SerrInt){
			serrdecode(d->bridge->serror, buf, buf+sizeof buf);
			dprint("%m: serror %08ulx [%s]\n", d, (ulong)d->bridge->serror, buf);
			d->bridge->serror = d->bridge->serror;
		}
	}
	edma->iec = ~cause;
}

#define Cmd(r, v) (((r)<<8) | ((v)&0xff))

static void
mvsatarequest(ushort *cmd, int atacmd, uvlong lba, int sectors, int llba)
{
	*cmd++ = Cmd(ARseccnt, 0);
	*cmd++ = Cmd(ARseccnt, sectors);
	*cmd++ = Cmd(ARfea, 0);
	if(llba){
		*cmd++ = Cmd(ARlba0, lba>>24);
		*cmd++ = Cmd(ARlba0, lba);
		*cmd++ = Cmd(ARlba1, lba>>32);
		*cmd++ = Cmd(ARlba1, lba>>8);
		*cmd++ = Cmd(ARlba2, lba>>40);
		*cmd++ = Cmd(ARlba2, lba>>16);
		*cmd++ = Cmd(ARdev, 0xe0);
	}else{
		*cmd++ = Cmd(ARlba0, lba);
		*cmd++ = Cmd(ARlba1, lba>>8);
		*cmd++ = Cmd(ARlba2, lba>>16);
		*cmd++ = Cmd(ARdev, lba>>24 | 0xe0);
	}
	*cmd = Cmd(ARcmd, atacmd) | 1<<15;
}

static uintptr
advance(uintptr pa, int shift)
{
	int n, mask;

	mask = 0x1F<<shift;
	n = (pa & mask) + (1<<shift);
	return (pa & ~mask) | (n & mask);
}

static void
start(Drive *d, int write, int atacmd, uchar *data, uvlong lba, int sectors, int ext)
{
	Edma *edma;
	Prd *prd;
	Tx *tx;

	d->intick = Ticks;
	edma = d->edma;
	tx = (Tx*)KADDR(edma->txi);
	tx->flag = (write == 0);
	prd = KADDR(tx->prdpa);
	prd->pa = PADDR(data);
	prd->zero = 0;
	prd->count = sectors*512;
	prd->flag = PRDeot;
	mvsatarequest(tx->regs, atacmd, lba, sectors, ext);
	coherence();
	edma->txi = advance(edma->txi, 5);
}

#define Rpidx(rpreg) (((rpreg)>>3) & 0x1f)

static void
complete(Drive *d)
{
	Edma *edma;
	Rx *rx;

	edma = d->edma;
	if((edma->ctl & eEnEDMA) == 0)
		return;
//	if(Rpidx(edma->rxi) == Rpidx(edma->rxo))
//		return;

	rx = (Rx*)KADDR(edma->rxo);
	if(rx->cDevSts & ATAbad)
		d->cmdflag |= SFerror;
	if (rx->cEdmaSts)
		print("cEdmaSts: %02ux\n", rx->cEdmaSts);
	d->cmdflag |= SFdone;
	edma->rxo = advance(edma->rxo, 3);
	ioprint("%m: complete: wakeup\n", d);
	wakeup(d);
}

static int
done(void *v)
{
	Drive *d;

	d = v;
	return d->cmdflag&SFdone;
}

static void
mv50interrupt(Ureg*, void *a)
{
	int i;
	ulong cause;
	Ctlr *c;
	Drive *d;

	c = a;
	ilock(c);
	cause = c->lmmio[0x1d60/4];
	for(i = 0; i < c->ndrive; i++)
		if(cause & (3<<(i*2+i/4))){
			d = c->drive+i;
//			print("%m: int: %lux\n", d, cause);
			ilock(d);
			updatedrive(d);
			while(c->chip[i/4].arb->ic & (0x0101 << (i%4))){
				c->chip[i/4].arb->ic = ~(0x101 << (i%4));
				complete(d);
			}
			iunlock(d);
		}
	iunlock(c);
}

enum{
	Nms		= 512,
	Midwait		= 16*1024/Nms-1,
	Mphywait	= 512/Nms-1,
};

static void
westerndigitalhung(Drive *d)
{
	if(d->cmdflag&SFrun)
	if(TK2MS(Ticks-d->intick) > 5*1000){
		dprint("%m: westerndigital drive hung; resetting\n", d);
		d->state = Dreset;
	}
}

static void
checkdrive(Drive *d, int i)
{
	static ulong s, olds[NCtlr*NCtlrdrv];

	ilock(d);
	s = d->bridge->status;
	if(s != olds[i]){
		dprint("%m: status: %08lx -> %08lx: %s\n", d, olds[i], s, diskstates[d->state]);
		olds[i] = s;
	}
	// westerndigitalhung(d);
	switch(d->state){
	case Dnew:
	case Dmissing:
		switch(s){
		case 0x000:
		case 0x023:
		case 0x013:
			break;
		default:
			dprint("%m: unknown state %8lx\n", d, s);
		case 0x100:
			if(++d->wait&Mphywait)
				break;
		reset:	d->mode ^= 1;
			dprint("%m: reset; new mode %d\n", d, d->mode);
			resetdisk(d);
			break;
		case 0x123:
		case 0x113:
			s = d->edma->cmdstat;
			if(s == 0x7f || (s&~ATAobs) != ATAdrdy){
				if((++d->wait&Midwait) == 0)
					goto reset;
			}else if(identifydrive(d) == -1)
				goto reset;
		}
		break;
	case Dready:
		if(s != 0)
			break;
		print("%m: pulled: st=%08ulx\n", d, s);
	case Dreset:
	case Derror:
		dprint("%m: reset: mode %d\n", d, d->mode);
		resetdisk(d);
		break;
	}
	iunlock(d);
}

static void
satakproc(void)
{
	int i, j;
	Drive *d;
	Ctlr *c;
	static Rendez r;

	for(;;){
		tsleep(&r, no, 0, Nms);
		for(i = 0; i < nmvctlr; i++){
			c = mvctlr+i;
			for(j = 0; j < c->ndrive; j++){
				d = c->drive+j;
				checkdrive(d, i*NCtlrdrv+j);
			}
		}
	}
}

void
mv50pnp(void)
{
	int i, nunit;
	uchar *base;
	ulong io, n, *mem;
	Ctlr *ctlr;
	Pcidev *p;
	Drive *d;

	dprint("mv50pnp\n");

	p = nil;
	while((p = pcimatch(p, 0x11ab, 0)) != nil){
		switch(p->did){
		case 0x5040:
		case 0x5041:
		case 0x5080:
		case 0x5081:
		case 0x6041:
		case 0x6081:
			break;
		default:
			print("mv50pnp: unknown did %ux ignored\n", (ushort)p->did);
			continue;
		}
		if(nmvctlr == NCtlr){
			print("mv50pnp: too many controllers\n");
			break;
		}
		nunit = (p->did&0xf0) >> 4;
		print("marvell 88sx%ux: %d sata-%s ports with%s flash, irq %d\n",
			(ushort)p->did, nunit,
			((p->did&0xf000)==0x6000? "ii": "i"),
			(p->did&1? "": "out"), p->intl);

		ctlr = mvctlr+nmvctlr;
		memset(ctlr, 0, sizeof *ctlr);

		io = p->mem[0].bar & ~0x0F;
		mem = (ulong*)vmap(io, p->mem[0].size);
		if(mem == 0){
			print("mv50xx: address 0x%luX in use\n", io);
			continue;
		}
		ctlr->rid = p->rid;
		ctlr->type = (p->did >> 12) & 3;

		// avert thine eyes!  (what does this do?)
		mem[0x104f0/4] = 0;

		// disable r/w combining
		if(ctlr->type == 1){
			n = mem[0xc00/4];
			n &= ~(3<<4);
			mem[0xc00/4] = n;
		}
		setvec(p->intl+24, mv50interrupt, ctlr);
		pcisetbme(p);

		ctlr->irq = p->intl;
		ctlr->tbdf = p->tbdf;
		ctlr->pcidev = p;
		ctlr->lmmio = mem;
		ctlr->mmio = (uchar*)mem;
		ctlr->nchip = (nunit+3)/4;
		ctlr->ndrive = nunit;
		for(i = 0; i < ctlr->nchip; i++){
			base = ctlr->mmio+0x20000+0x10000*i;
			ctlr->chip[i].arb = (Arb*)base;
			if(ctlr->type == 2)
				ctlr->chip[i].arb->config |= 0xf<<24;
			ctlr->chip[i].edma = (Edma*)(base + 0x2000);
		}
		for (i = 0; i < nunit; i++) {
			d = &ctlr->drive[i];
			memset(d, 0, sizeof *d);
			d->sectors = 0;
			d->ctlr = ctlr;
			d->driveno = nmvctlr*NCtlrdrv + i;
			d->chipx = i%4;
			d->chip =&ctlr->chip[i/4];
			d->edma = &d->chip->edma[d->chipx];
			configdrive(ctlr, d);
		}
		nmvctlr++;
	}

	userinit(satakproc, 0, "mvsata");
}

static int
waitready(Drive *d)
{
	ulong s, i;
	Rendez r;

	for(i = 0; i < 120; i++){
		ilock(d);
		s = d->bridge->status;
		iunlock(d);
		if(s == 0)
			return  -1;
		if (d->state == Dready)
		if((d->edma->ctl&eEnEDMA) != 0)
			return 0;
		if ((i+1)%60 == 0){
			ilock(d);
			resetdisk(d);
			iunlock(d);
		}
		memset(&r, 0, sizeof r);
		tsleep(&r, no, 0, 1000);
	}
	print("%m: not responding after 2 minutes\n", d);
	return -1;
}

static uint
getatacmd(int write, int e)
{
	static uchar cmd[2][2] = { 0xC8, 0x25, 0xCA, 0x35 };

	return cmd[write][e!=0];
}

static int
rw(Drive *d, int write, uchar *db, long len, uvlong lba)
{
	uchar *data;
	char *msg;
	uint count, try, ext, atacmd;

	ext = d->flag&Dllba;
	atacmd = getatacmd(write, ext);

	data = db;
	if(len > 64*1024)
		len = 64*1024;
	count = len/512;
	msg = "%m: bad disk\n";

	qlock(d);
	for(try = 0; try < 10; try++){
		if(waitready(d) == -1){
			qunlock(d);
			return -1;
		}

		start(d, write, atacmd, data, lba, count, ext);
		d->cmdflag = SFrun;
		sleep(d, done, d);
		d->cmdflag &= ~SFrun;

		if(d->cmdflag&SFretry){
			dprint("%m: retry\n", d);
			tsleep(d, no, 0, 1000);
			continue;
		}
		if(d->cmdflag&SFerror){
			msg = "%m: i/o error\n";
			break;
		}
		qunlock(d);
		return count*512;
	}
	qunlock(d);
	print(msg, d); 
	return -1;
}

static int
fmtm(Fmt *f)
{
	Drive *d;
	char buf[8];

	d = va_arg(f->args, Drive*);
	if(d == nil)
		snprint(buf, sizeof buf, "m[nil]");
	else
		snprint(buf, sizeof buf, "m%d", d->driveno);
	return fmtstrcpy(f, buf);
}

static Drive*
mvdev(Device *d)
{
	int i, j;
	Drive *dr;

	i = d->wren.ctrl;
	j = d->wren.targ;

	for(; i < nmvctlr; i++){
		if(j < mvctlr[i].ndrive){
			dr = mvctlr[i].drive+j;
			if(dr->state&Dready)
				return dr;
			return 0;
		}
		j -= mvctlr[i].ndrive;
	}
	panic("mv: bad drive %Z\n", d);
	return 0;
}

static void
cmd_stat(int, char*[])
{
	Ctlr *c;
	Drive *d;
	int i, j;

	for(i = 0; i < nmvctlr; i++){
		c = mvctlr+i;
		for(j = 0; j < c->ndrive; j++){
			d = c->drive+j;
			if(d->fflag == 0)
				continue;
			print("%m:\n", d);
			print("  r\t%W\n", d->rate+Read);
			print("  w\t%W\n", d->rate+Write);
		}
	}
}

void
mvinit0(void)
{
	fmtinstall('m', fmtm);
	mv50pnp();
	if(nmvctlr > 0){
		cmd_install("statm", "-- marvell sata stats", cmd_stat);
	}
}

void
mvinit(Device *dv)
{
	Drive *d;
	vlong s;
	char *lba;
	static int once;

	if(once++ == 0)
		mvinit0();

top:
	d = mvdev(dv);
	if(d == 0){
		print("\t\t" "%d.%d.%d not ready yet\n", dv->wren.ctrl, dv->wren.targ, dv->wren.lun);
		delay(500);
		goto top;
	}

	if(d->init++== 0){
		dofilter(d->rate+Read);
		dofilter(d->rate+Write);
	}

	s = d->sectors;
	lba = "";
	if(d->flag&Dllba)
		lba = "L";
	print("\t\t" "%lld sectors/%lld blocks %sLBA\n", s, s/Rbufps, lba);
}

Devsize
mvsize(Device *dv)
{
	Drive *d;

	d = mvdev(dv);
	if(d == 0)
		return 0;
	dprint("%m: mvsize %lld\n", d, d->sectors/(RBUFSIZE/512));
	return d->sectors/(RBUFSIZE/512);
}

typedef struct{
	QLock;
	uchar	*buf;
	uvlong	lba;
}Cbuf;

Cbuf cbuftab[NDrive];

enum{
	Cbad = 1LL<<63
};

Cbuf*
getcbuf(int i)
{
	Cbuf *c;

	c = cbuftab+i;
	qlock(c);
	if(c->buf == 0){
		c->buf = ialloc(64*1024, 0);
		c->lba = Cbad;
	}
	return c;
}

void
putcbuf(Cbuf *c)
{
	qunlock(c);
}

static int
crw0(Drive *d, int write, uchar *buf, uvlong lba, Cbuf *c)
{
	uvlong off;
	int rv;

	if(write == 0){
		if(lba >= c->lba && lba < c->lba+128){
			off = lba-c->lba;
			memmove(buf, c->buf+off*512, 8192);
			return 8192;
		}
		if(lba+128 > d->sectors)
			return rw(d, 0, buf, 8192, lba);
		rv = rw(d, 0, c->buf, 64*1024, lba);
		if(rv != 64*1024){
			c->lba = Cbad;
			return rv;
		}
		memmove(buf, c->buf, 8192);
		c->lba = lba;
		return 8192;
	}else{
		if(lba >= c->lba && lba < c->lba+128)
			c->lba = Cbad;
		return rw(d, 1, buf, 8192, lba);
	}
}

static int
crw(Drive *d, int write, uchar *buf, uvlong lba)
{
	Cbuf *c;
	int rv;

	c = getcbuf(d->driveno);
	rv = crw0(d, write, buf, lba, c);
	putcbuf(c);
	return rv;
}

int
mvread(Device *dv, Devsize b, void *c)
{
	Drive *d;
	int rv;

	d = mvdev(dv);
	if(d == 0)
		return 1;

//	rv = rw(d, 0, c, RBUFSIZE, b*Rbufps);
	rv = crw(d, 0, c, b*Rbufps);
	if(rv != RBUFSIZE)
		return 1;
	d->rate[Read].count++;
	d->fflag = 1;
	return 0;
}

int
mvwrite(Device *dv, Devsize b, void *c)
{
	Drive *d;
	int rv;

	d = mvdev(dv);
	if(d == 0)
		return 1;

//	rv = rw(d, 1, c, RBUFSIZE, b*Rbufps);
	rv = crw(d, 1, c, b*Rbufps);
	if(rv != RBUFSIZE)
		return 1;
	d->rate[Write].count++;
	d->fflag = 1;
	return 0;
}
