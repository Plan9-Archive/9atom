/*
 * PCI support code.
 * To do:
 *	initialise bridge mappings if the PCI BIOS didn't.
 */
#include "all.h"
#include "io.h"
#include "mem.h"

#define dprint(...)	print(__VA_ARGS__)

enum {					/* configuration mechanism #1 */
	PciADDR		= 0xCF8,	/* CONFIG_ADDRESS */
	PciDATA		= 0xCFC,	/* CONFIG_DATA */

					/* configuration mechanism #2 */
	PciCSE		= 0xCF8,	/* configuration space enable */
	PciFORWARD	= 0xCFA,	/* which bus */

	MaxFNO		= 7,
	MaxUBN		= 255,
};

enum
{					/* command register */
	IOen		= 1<<0,
	MEMen		= 1<<1,
	MASen		= 1<<2,
	MemWrInv	= 1<<4,
	PErrEn		= 1<<6,
	SErrEn		= 1<<8,
};

static Lock pcicfglock;
static Lock pcicfginitlock;
static int pcicfgmode = -1;
static int pcimaxdno;
static Pcidev* pciroot;
static Pcidev* pcilist;
static Pcidev* pcitail;

static int pcicfgrw32(int, int, int, int);
static int pcicfgrw16(int, int, int, int);
static int pcicfgrw8(int, int, int, int);

ulong
pcibarsize(Pcidev *p, int rno)
{
	ulong v, size;

	v = pcicfgrw32(p->tbdf, rno, 0, 1);
	pcicfgrw32(p->tbdf, rno, 0xFFFFFFF0, 0);
	size = pcicfgrw32(p->tbdf, rno, 0, 1);
	if(v & 1)
		size |= 0xFFFF0000;
	pcicfgrw32(p->tbdf, rno, v, 0);

	return -(size & ~0x0F);
}

static void
cmd_pcihinv(int argc, char *argv[])
{
	int i, flags = 0;

	for (i = 1; i < argc; i++)
		if (strcmp(argv[i], "-v") == 0)
			flags |= 1;
		else {
			print("unknown pcihinv option %s; options are: -v\n", argv[i]);
			return;
		}
	pcihinv(nil, flags);		/* print the whole device tree */
}

static ulong
pcimask(ulong v)
{
	ulong m;

	m = 8*sizeof(v);
	for(m = 1<<(m-1); m != 0; m >>= 1) {
		if(m & v)
			break;
	}

	m--;
	if((v & m) == 0)
		return v;

	v |= m;
	return v+1;
}

static int
pcisizcmp(void *a, void *b)
{
	Pcisiz *aa, *bb;

	aa = a;
	bb = b;
	return aa->siz - bb->siz;
}

static void
pcibusmap(Pcidev *root, ulong *pmema, ulong *pioa, int wrreg)
{
	Pcidev *p;
	int ntb, i, size, rno, hole;
	ulong v, mema, ioa, sioa, smema, base, limit;
	Pcisiz *table, *tptr, *mtb, *itb;

	ioa = *pioa;
	mema = *pmema;

//	dprint("pcibusmap wr=%d %τ mem=%luX io=%luX\n", 
//		wrreg, root->tbdf, mema, ioa);
	dprint("pcibusmap wr=%d %x mem=%lux io=%lux\n", wrreg, root->tbdf, mema, ioa);

	ntb = 0;
	for(p = root; p != nil; p = p->link)
		ntb++;

	ntb *= (PciCIS-PciBAR0)/4;
	table = ialloc(2*ntb*sizeof *table, 0);
	itb = table;
	mtb = table+ntb;

	/*
	 * Build a table of sizes
	 */
	for(p = root; p != nil; p = p->link) {
		if(p->ccrb == 0x06) {
			if(p->ccru != 0x04 || p->bridge == nil) {
				print("pci: ignored bridge p->tbdf\n");
//				DBG("pci: ignored bridge %τ\n", p->tbdf);
				continue;
			}

			sioa = ioa;
			smema = mema;
			pcibusmap(p->bridge, &smema, &sioa, 0);

			hole = pcimask(smema-mema);
			if(hole < (1<<20))
				hole = 1<<20;
			p->mema.size = hole;

			hole = pcimask(sioa-ioa);
			if(hole < (1<<12))
				hole = 1<<12;

			p->ioa.size = hole;

			itb->dev = p;
			itb->bar = -1;
			itb->siz = p->ioa.size;
			itb++;

			mtb->dev = p;
			mtb->bar = -1;
			mtb->siz = p->mema.size;
			mtb++;
			continue;
		}

		for(i = 0; i <= 5; i++) {
			rno = PciBAR0 + i*4;
			v = pcicfgrw32(p->tbdf, rno, 0, 1);
			size = pcibarsize(p, rno);
			if(size == 0)
				continue;

			if(v & 1) {
				itb->dev = p;
				itb->bar = i;
				itb->siz = size;
				itb++;
			}
			else {
				mtb->dev = p;
				mtb->bar = i;
				mtb->siz = size;
				mtb++;
			}

			p->mem[i].size = size;
		}
	}

	/*
	 * Sort both tables IO smallest first, Memory largest
	 */
	qsort(table, itb-table, sizeof(Pcisiz), pcisizcmp);
	tptr = table+ntb;
	qsort(tptr, mtb-tptr, sizeof(Pcisiz), pcisizcmp);

	/*
	 * Allocate IO address space on this bus
	 */
	for(tptr = table; tptr < itb; tptr++) {
		hole = tptr->siz;
		if(tptr->bar == -1)
			hole = 1<<12;
		ioa = (ioa+hole-1) & ~(hole-1);

		p = tptr->dev;
		if(tptr->bar == -1)
			p->ioa.bar = ioa;
		else {
			p->pcr |= IOen;
			p->mem[tptr->bar].bar = ioa|1;
			if(wrreg)
				pcicfgrw32(p->tbdf, PciBAR0+(tptr->bar*4), ioa|1, 0);
		}

		ioa += tptr->siz;
	}

	/*
	 * Allocate Memory address space on this bus
	 */
	for(tptr = table+ntb; tptr < mtb; tptr++) {
		hole = tptr->siz;
		if(tptr->bar == -1)
			hole = 1<<20;
		mema = (mema+hole-1) & ~(hole-1);

		p = tptr->dev;
		if(tptr->bar == -1)
			p->mema.bar = mema;
		else {
			p->pcr |= MEMen;
			p->mem[tptr->bar].bar = mema;
			if(wrreg)
				pcicfgrw32(p->tbdf, PciBAR0+(tptr->bar*4), mema, 0);
		}
		mema += tptr->siz;
	}

	*pmema = mema;
	*pioa = ioa;
//	free(table);

	if(wrreg == 0)
		return;

	/*
	 * Finally set all the bridge addresses & registers
	 */
	for(p = root; p != nil; p = p->link) {
		if(p->bridge == nil) {
			pcicfgrw8(p->tbdf, PciLTR, 64, 0);

			p->pcr |= MASen;
			pcicfgrw16(p->tbdf, PciPCR, p->pcr, 0);
			continue;
		}

		base = p->ioa.bar;
		limit = base+p->ioa.size-1;
		v = pcicfgrw32(p->tbdf, PciIBR, 0, 1);
		v = (v&0xFFFF0000)|(limit & 0xF000)|((base & 0xF000)>>8);
		pcicfgrw32(p->tbdf, PciIBR, v, 0);
		v = (limit & 0xFFFF0000)|(base>>16);
		pcicfgrw32(p->tbdf, PciIUBR, v, 0);

		base = p->mema.bar;
		limit = base+p->mema.size-1;
		v = (limit & 0xFFF00000)|((base & 0xFFF00000)>>16);
		pcicfgrw32(p->tbdf, PciMBR, v, 0);

		/*
		 * Disable memory prefetch
		 */
		pcicfgrw32(p->tbdf, PciPMBR, 0x0000FFFF, 0);
		pcicfgrw8(p->tbdf, PciLTR, 64, 0);

		/*
		 * Enable the bridge
		 */
		p->pcr |= IOen|MEMen|MASen;
		pcicfgrw32(p->tbdf, PciPCR, 0xFFFF0000|p->pcr , 0);

		sioa = p->ioa.bar;
		smema = p->mema.bar;
		pcibusmap(p->bridge, &smema, &sioa, 1);
	}
}

static int
pcilscan(int bno, Pcidev** list)
{
	Pcidev *p, *head, *tail;
	int dno, fno, i, hdt, l, maxfno, maxubn, rno, sbn, tbdf, ubn;

	maxubn = bno;
	head = nil;
	tail = nil;
	for(dno = 0; dno <= pcimaxdno; dno++){
		maxfno = 0;
		for(fno = 0; fno <= maxfno; fno++){
			/*
			 * For this possible device, form the
			 * bus+device+function triplet needed to address it
			 * and try to read the vendor and device ID.
			 * If successful, allocate a device struct and
			 * start to fill it in with some useful information
			 * from the device's configuration space.
			 */
			tbdf = MKBUS(BusPCI, bno, dno, fno);
			l = pcicfgrw32(tbdf, PciVID, 0, 1);
			if(l == 0xFFFFFFFF || l == 0)
				continue;
			p = ialloc(sizeof *p, 0);
			memset(p, 0, sizeof *p);
			p->tbdf = tbdf;
			p->vid = l;
			p->did = l>>16;

			if(pcilist != nil)
				pcitail->list = p;
			else
				pcilist = p;
			pcitail = p;

			p->pcr = pcicfgr16(p, PciPCR);
			p->rid = pcicfgr8(p, PciRID);
			p->ccrp = pcicfgr8(p, PciCCRp);
			p->ccru = pcicfgr16(p, PciCCRu);
			p->ccrb = pcicfgr8(p, PciCCRb);
			p->cls = pcicfgr8(p, PciCLS);
			p->ltr = pcicfgr8(p, PciLTR);

			p->intl = pcicfgr8(p, PciINTL);
			/*
			 * If the device is a multi-function device adjust the
			 * loop count so all possible functions are checked.
			 */
			hdt = pcicfgr8(p, PciHDT);
			if(hdt & 0x80)
				maxfno = MaxFNO;

			/*
			 * If appropriate, read the base address registers
			 * and work out the sizes.
			 */
			switch(p->ccrb) {
			case 0x01:		/* mass storage controller */
			case 0x02:		/* network controller */
			case 0x03:		/* display controller */
			case 0x04:		/* multimedia device */
			case 0x07:		/* simple comm. controllers */
			case 0x08:		/* base system peripherals */
			case 0x09:		/* input devices */
			case 0x0A:		/* docking stations */
			case 0x0B:		/* processors */
			case 0x0C:		/* serial bus controllers */
				if((hdt & 0x7F) != 0)
					break;
				rno = PciBAR0 - 4;
				for(i = 0; i < nelem(p->mem); i++) {
					rno += 4;
					p->mem[i].bar = pcicfgr32(p, rno);
					p->mem[i].size = pcibarsize(p, rno);
				}
				break;

			case 0x00:
			case 0x05:		/* memory controller */
			case 0x06:		/* bridge device */
			default:
				break;
			}

			if(head != nil)
				tail->link = p;
			else
				head = p;
			tail = p;
		}
	}

	*list = head;
	for(p = head; p != nil; p = p->link){
		/*
		 * Find PCI-PCI bridges and recursively descend the tree.
		 */
		if(p->ccrb != 0x06 || p->ccru != 0x04)
			continue;

		/*
		 * If the secondary or subordinate bus number is not
		 * initialised try to do what the PCI BIOS should have
		 * done and fill in the numbers as the tree is descended.
		 * On the way down the subordinate bus number is set to
		 * the maximum as it's not known how many buses are behind
		 * this one; the final value is set on the way back up.
		 */
		sbn = pcicfgr8(p, PciSBN);
		ubn = pcicfgr8(p, PciUBN);

		if(sbn == 0 || ubn == 0 /*|| nobios*/) {
			sbn = maxubn+1;
			/*
			 * Make sure memory, I/O and master enables are
			 * off, set the primary, secondary and subordinate
			 * bus numbers and clear the secondary status before
			 * attempting to scan the secondary bus.
			 *
			 * Initialisation of the bridge should be done here.
			 */
			pcicfgw32(p, PciPCR, 0xFFFF0000);
			l = (MaxUBN<<16)|(sbn<<8)|bno;
			pcicfgw32(p, PciPBN, l);
			pcicfgw16(p, PciSPSR, 0xFFFF);
			maxubn = pcilscan(sbn, &p->bridge);
			l = (maxubn<<16)|(sbn<<8)|bno;

			pcicfgw32(p, PciPBN, l);
		}
		else {
			if(ubn > maxubn)
				maxubn = ubn;
			pcilscan(sbn, &p->bridge);
		}
	}

	return maxubn;
}

int
pciscan(int bno, Pcidev **list)
{
	return pcilscan(bno, list);
}

static uchar
pIIxget(Pcidev *router, uchar link)
{
	uchar pirq;

	/* link should be 0x60, 0x61, 0x62, 0x63 */
	pirq = pcicfgr8(router, link);
	return (pirq < 16)? pirq: 0;
}

static void
pIIxset(Pcidev *router, uchar link, uchar irq)
{
	pcicfgw8(router, link, irq);
}

static uchar
viaget(Pcidev *router, uchar link)
{
	uchar pirq;

	/* link should be 1, 2, 3, 5 */
	pirq = (link < 6)? pcicfgr8(router, 0x55 + (link>>1)): 0;

	return (link & 1)? (pirq >> 4): (pirq & 15);
}

static void
viaset(Pcidev *router, uchar link, uchar irq)
{
	uchar pirq;

	pirq = pcicfgr8(router, 0x55 + (link >> 1));
	pirq &= (link & 1)? 0x0f: 0xf0;
	pirq |= (link & 1)? (irq << 4): (irq & 15);
	pcicfgw8(router, 0x55 + (link>>1), pirq);
}

static uchar
optiget(Pcidev *router, uchar link)
{
	uchar pirq = 0;

	/* link should be 0x02, 0x12, 0x22, 0x32 */
	if ((link & 0xcf) == 0x02)
		pirq = pcicfgr8(router, 0xb8 + (link >> 5));
	return (link & 0x10)? (pirq >> 4): (pirq & 15);
}

static void
optiset(Pcidev *router, uchar link, uchar irq)
{
	uchar pirq;

	pirq = pcicfgr8(router, 0xb8 + (link >> 5));
    	pirq &= (link & 0x10)? 0x0f : 0xf0;
    	pirq |= (link & 0x10)? (irq << 4): (irq & 15);
	pcicfgw8(router, 0xb8 + (link >> 5), pirq);
}

static uchar
aliget(Pcidev *router, uchar link)
{
	/* No, you're not dreaming */
	static const uchar map[] = { 0, 9, 3, 10, 4, 5, 7, 6, 1, 11, 0, 12, 0, 14, 0, 15 };
	uchar pirq;

	/* link should be 0x01..0x08 */
	pirq = pcicfgr8(router, 0x48 + ((link-1)>>1));
	return (link & 1)? map[pirq&15]: map[pirq>>4];
}

static void
aliset(Pcidev *router, uchar link, uchar irq)
{
	/* Inverse of map in aliget */
	static const uchar map[] = { 0, 8, 0, 2, 4, 5, 7, 6, 0, 1, 3, 9, 11, 0, 13, 15 };
	uchar pirq;

	pirq = pcicfgr8(router, 0x48 + ((link-1)>>1));
	pirq &= (link & 1)? 0x0f: 0xf0;
	pirq |= (link & 1)? (map[irq] << 4): (map[irq] & 15);
	pcicfgw8(router, 0x48 + ((link-1)>>1), pirq);
}

static uchar
cyrixget(Pcidev *router, uchar link)
{
	uchar pirq;

	/* link should be 1, 2, 3, 4 */
	pirq = pcicfgr8(router, 0x5c + ((link-1)>>1));
	return ((link & 1)? pirq >> 4: pirq & 15);
}

static void
cyrixset(Pcidev *router, uchar link, uchar irq)
{
	uchar pirq;

	pirq = pcicfgr8(router, 0x5c + (link>>1));
	pirq &= (link & 1)? 0x0f: 0xf0;
	pirq |= (link & 1)? (irq << 4): (irq & 15);
	pcicfgw8(router, 0x5c + (link>>1), pirq);
}

typedef struct Bridge Bridge;
struct Bridge
{
	ushort	vid;
	ushort	did;
	uchar	(*get)(Pcidev *, uchar);
	void	(*set)(Pcidev *, uchar, uchar);
};

static Bridge southbridges[] = {
	{ 0x8086, 0x122e, pIIxget, pIIxset },	/* Intel 82371FB */
	{ 0x8086, 0x1234, pIIxget, pIIxset },	/* Intel 82371MX */
	{ 0x8086, 0x7000, pIIxget, pIIxset },	/* Intel 82371SB */
	{ 0x8086, 0x7110, pIIxget, pIIxset },	/* Intel 82371AB */
	{ 0x8086, 0x7198, pIIxget, pIIxset },	/* Intel 82443MX (fn 1) */
	{ 0x8086, 0x2410, pIIxget, pIIxset },	/* Intel 82801AA */
	{ 0x8086, 0x2420, pIIxget, pIIxset },	/* Intel 82801AB */
	{ 0x8086, 0x2440, pIIxget, pIIxset },	/* Intel 82801BA */
	{ 0x8086, 0x2448, pIIxget, pIIxset },	/* Intel 82801BAM/CAM/DBM */
	{ 0x8086, 0x244c, pIIxget, pIIxset },	/* Intel 82801BAM */
	{ 0x8086, 0x244e, pIIxget, pIIxset },	/* Intel 82801 */
	{ 0x8086, 0x2480, pIIxget, pIIxset },	/* Intel 82801CA */
	{ 0x8086, 0x248c, pIIxget, pIIxset },	/* Intel 82801CAM */
	{ 0x8086, 0x24c0, pIIxget, pIIxset },	/* Intel 82801DBL */
	{ 0x8086, 0x24cc, pIIxget, pIIxset },	/* Intel 82801DBM */
	{ 0x8086, 0x24d0, pIIxget, pIIxset },	/* Intel 82801EB */
	{ 0x8086, 0x25a1, pIIxget, pIIxset },	/* Intel 6300ESB */
	{ 0x8086, 0x2640, pIIxget, pIIxset },	/* Intel 82801FB */
	{ 0x8086, 0x2641, pIIxget, pIIxset },	/* Intel 82801FBM */
	{ 0x8086, 0x2670, pIIxget, pIIxset },	/* Intel 632xesb */
	{ 0x8086, 0x27b8, pIIxget, pIIxset },	/* Intel 82801GB */
	{ 0x8086, 0x27b9, pIIxget, pIIxset },	/* Intel 82801GBM */
	{ 0x8086, 0x2810, pIIxget, pIIxset },	/* Intel 82801HB/HR (ich8/r) */
	{ 0x8086, 0x2812, pIIxget, pIIxset },	/* Intel 82801HH (ich8dh) */
	{ 0x8086, 0x2912, pIIxget, pIIxset },	/* Intel 82801ih ich9dh */
	{ 0x8086, 0x2914, pIIxget, pIIxset },	/* Intel 82801io ich9do */
	{ 0x8086, 0x2916, pIIxget, pIIxset },	/* Intel 82801ibr ich9r */
	{ 0x8086, 0x2917, pIIxget, pIIxset },	/* Intel 82801iem ich9m-e  */
	{ 0x8086, 0x2918, pIIxget, pIIxset },	/* Intel 82801ib ich9 */
	{ 0x8086, 0x2919, pIIxget, pIIxset },	/* Intel 82801? ich9m  */
	{ 0x8086, 0x3a16, pIIxget, pIIxset },	/* Intel 82801jir ich10r */
	{ 0x8086, 0x3a40, pIIxget, pIIxset },	/* Intel 82801ji */
	{ 0x8086, 0x3a42, pIIxget, pIIxset },	/* Intel 82801ji */
	{ 0x8086, 0x3a48, pIIxget, pIIxset },	/* Intel 82801ji */
	{ 0x8086, 0x3b06, pIIxget, pIIxset },	/* Intel 82801? ibex peak */
	{ 0x8086, 0x3b14, pIIxget, pIIxset },	/* Intel 82801? 3420 */
	{ 0x8086, 0x1c52, pIIxget, pIIxset },	/* Intel 82q65 cougar point pch */
	{ 0x8086, 0x1c54, pIIxget, pIIxset },	/* Intel 82q67 cougar point pch */
	{ 0x1106, 0x0586, viaget, viaset },	/* Viatech 82C586 */
	{ 0x1106, 0x0596, viaget, viaset },	/* Viatech 82C596 */
	{ 0x1106, 0x0686, viaget, viaset },	/* Viatech 82C686 */
	{ 0x1106, 0x3227, viaget, viaset },	/* Viatech VT8237 */
	{ 0x1045, 0xc700, optiget, optiset },	/* Opti 82C700 */
	{ 0x10b9, 0x1533, aliget, aliset },	/* Al M1533 */
	{ 0x1039, 0x0008, pIIxget, pIIxset },	/* SI 503 */
	{ 0x1039, 0x0496, pIIxget, pIIxset },	/* SI 496 */
	{ 0x1078, 0x0100, cyrixget, cyrixset },	/* Cyrix 5530 Legacy */

	{ 0x1022, 0x746B, nil, nil },		/* AMD 8111 */
	{ 0x10DE, 0x00D1, nil, nil },		/* NVIDIA nForce 3 */
	{ 0x10DE, 0x00E0, nil, nil },		/* NVIDIA nForce 3 250 Series */
	{ 0x10DE, 0x00E1, nil, nil },		/* NVIDIA nForce 3 250 Series */
	{ 0x1166, 0x0200, nil, nil },		/* ServerWorks ServerSet III LE */
	{ 0x1002, 0x4377, nil, nil },		/* ATI Radeon Xpress 200M */
	{ 0x1002, 0x4372, nil, nil },		/* ATI SB400 */
	{ 0x1002, 0x9601, nil, nil },		/* AMD SB710 */
	{ 0x1002, 0x439d, nil, nil },		/* AMD SB810 */
};

typedef struct Slot Slot;
struct Slot {
	uchar	bus;		/* Pci bus number */
	uchar	dev;		/* Pci device number */
	uchar	maps[12];	/* Avoid structs!  Link and mask. */
	uchar	slot;		/* Add-in/built-in slot */
	uchar	reserved;
};

typedef struct Router Router;
struct Router {
	uchar	signature[4];	/* Routing table signature */
	uchar	version[2];	/* Version number */
	uchar	size[2];	/* Total table size */
	uchar	bus;		/* Interrupt router bus number */
	uchar	devfn;		/* Router's devfunc */
	uchar	pciirqs[2];	/* Exclusive PCI irqs */
	uchar	compat[4];	/* Compatible PCI interrupt router */
	uchar	miniport[4];	/* Miniport data */
	uchar	reserved[11];
	uchar	checksum;
};

#pragma	varargck	type	"τ"	int

static int
τfmt(Fmt* fmt)
{
	char buf[32], *p, *e;
	int type, tbdf;

	p = buf;
	e = buf+sizeof buf;
	tbdf = va_arg(fmt->args, int);
	type = BUSTYPE(tbdf);
	if(type == 12)
		p = seprint(p, e, "pci");
	else
		p = seprint(p, e, "%d", type);
	seprint(p, e, ".%d.%d.%d",
		BUSBNO(tbdf), BUSDNO(tbdf), BUSFNO(tbdf));
	return fmtstrcpy(fmt, buf);
}

static void
pcirouting(void)
{
	Slot *e;
	Router *r;
	int size, i, fn, tbdf;
	Pcidev *sbpci, *pci;
	uchar *p, pin, irq, link, *map;
	static ushort pciirqs;		/* Exclusive PCI irqs */
	static Bridge *southbridge;	/* Which southbridge to use. */

	/* Search for PCI interrupt routing table in BIOS */
	for(p = (uchar*)KADDR(0xf0000); p < (uchar*)KADDR(0xfffff); p += 16)
		if(p[0] == '$' && p[1] == 'P' && p[2] == 'I' && p[3] == 'R')
			break;

	if(p >= (uchar *)KADDR(0xfffff))
		return;

	r = (Router *)p;

	fmtinstall(L'τ', τfmt);
	print("PCI interrupt routing table version %d.%d at %.6lux\n",
		r->version[0], r->version[1], (uintptr)r & 0xfffff);

	tbdf = (BusPCI << 24)|(r->bus << 16)|(r->devfn << 8);
	sbpci = pcimatchtbdf(tbdf);
	if(sbpci == nil) {
		print("pcirouting: Cannot find south bridge %τ\n", tbdf);
		return;
	}

	for(i = 0; i != nelem(southbridges); i++)
		if(sbpci->vid == southbridges[i].vid && sbpci->did == southbridges[i].did)
			break;

	if(i == nelem(southbridges)) {
		print("pcirouting: ignoring south bridge %τ %.4ux/%.4ux\n", tbdf, sbpci->vid, sbpci->did);
		return;
	}
	southbridge = &southbridges[i];
	if(southbridge->get == nil || southbridge->set == nil)
		return;

	pciirqs = (r->pciirqs[1] << 8)|r->pciirqs[0];

	size = (r->size[1] << 8)|r->size[0];
	for(e = (Slot *)&r[1]; (uchar *)e < p + size; e++) {
		if (0) {
			print("%.2uX/%.2uX %.2uX: ", e->bus, e->dev, e->slot);
			for (i = 0; i != 4; i++) {
				uchar *m = &e->maps[i * 3];
				print("[%d] %.2uX %.4uX ",
					i, m[0], (m[2] << 8)|m[1]);
			}
			print("\n");
		}
		for(fn = 0; fn != 8; fn++) {
			tbdf = (BusPCI << 24)|(e->bus << 16)|((e->dev | fn) << 8);
			pci = pcimatchtbdf(tbdf);
			if(pci == nil)
				continue;
			pin = pcicfgr8(pci, PciINTP);
			if(pin == 0 || pin == 0xff)
				continue;

			map = &e->maps[(pin - 1) * 3];
			link = map[0];
			irq = southbridge->get(sbpci, link);
			if(irq == 0 || irq == pci->intl)
				continue;
			if(pci->intl != 0 && pci->intl != 0xFF) {
				print("pcirouting: BIOS workaround: %τ at pin %d link %d irq %d -> %d\n",
					  tbdf, pin, link, irq, pci->intl);
				southbridge->set(sbpci, link, pci->intl);
				continue;
			}
			print("pcirouting: %τ at pin %d link %d irq %d\n", tbdf, pin, link, irq);
			pcicfgw8(pci, PciINTL, irq);
			pci->intl = irq;
		}
	}
}

static void
pcicfginit(void)
{
	char *p;
	int bno, n;
	Pcidev **list;

	cmd_install("pcihinv", "-- pci inventory", cmd_pcihinv);

	lock(&pcicfginitlock);
	if(pcicfgmode == -1){
		/*
		 * Try to determine which PCI configuration mode is implemented.
		 * Mode2 uses a byte at 0xCF8 and another at 0xCFA; Mode1 uses
		 * a DWORD at 0xCF8 and another at 0xCFC and will pass through
		 * any non-DWORD accesses as normal I/O cycles. There shouldn't be
		 * a device behind these addresses so if Mode2 accesses fail try
		 * for Mode1 (which is preferred, Mode2 is deprecated).
		 */
		/*
		 * Bits [30:24] of PciADDR must be 0,
		 * according to the spec.
		 */
		n = inl(PciADDR);
		if(!(n & 0x7F000000)){
			outl(PciADDR, 0x80000000);
			outb(PciADDR+3, 0);
			if(inl(PciADDR) & 0x80000000){
				pcicfgmode = 1;
				pcimaxdno = 31;
			}
		}
		outl(PciADDR, n);

		if(pcicfgmode == -1){
			/*
			 * The 'key' part of PciCSE should be 0.
			 */
			n = inb(PciCSE);
			if(!(n & 0xF0)){
				outb(PciCSE, 0x0E);
				if(inb(PciCSE) == 0x0E){
					pcicfgmode = 2;
					pcimaxdno = 15;
				}
			}
			outb(PciCSE, n);
		}
	
		if(pcicfgmode > 0){
			if(p = getconf("*pcimaxdno"))
				pcimaxdno = strtoul(p, 0, 0);

			list = &pciroot;
			for(bno = 0; bno <= 255; bno++){
				bno = pciscan(bno, list);
				while(*list)
					list = &(*list)->link;
			}
		}
	}
	pcirouting();
	unlock(&pcicfginitlock);
	if(getconf("*pcihinv"))
		pcihinv(pciroot, 1);
}

static int
pcicfgrw8(int tbdf, int rno, int data, int read)
{
	int o, type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	lock(&pcicfglock);
	switch(pcicfgmode){

	case 1:
		o = rno & 0x03;
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = inb(PciDATA+o);
		else
			outb(PciDATA+o, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = inb((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outb((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	}
	unlock(&pcicfglock);

	return x;
}

int
pcicfgr8(Pcidev* pcidev, int rno)
{
	return pcicfgrw8(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw8(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw8(pcidev->tbdf, rno, data, 0);
}

static int
pcicfgrw16(int tbdf, int rno, int data, int read)
{
	int o, type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	lock(&pcicfglock);
	switch(pcicfgmode){

	case 1:
		o = rno & 0x02;
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = ins(PciDATA+o);
		else
			outs(PciDATA+o, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = ins((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outs((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	}
	unlock(&pcicfglock);

	return x;
}

int
pcicfgr16(Pcidev* pcidev, int rno)
{
	return pcicfgrw16(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw16(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw16(pcidev->tbdf, rno, data, 0);
}

static int
pcicfgrw32(int tbdf, int rno, int data, int read)
{
	int type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	lock(&pcicfglock);
	switch(pcicfgmode){

	case 1:
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = inl(PciDATA);
		else
			outl(PciDATA, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = inl((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outl((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	}
	unlock(&pcicfglock);

	return x;
}

int
pcicfgr32(Pcidev* pcidev, int rno)
{
	return pcicfgrw32(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw32(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw32(pcidev->tbdf, rno, data, 0);
}

void
pciclrmwi(Pcidev* p)
{
	p->pcr &= ~MemWrInv;
	pcicfgw16(p, PciPCR, p->pcr);
}


Pcidev*
pcimatch(Pcidev* prev, int vid, int did)
{
	if(pcicfgmode == -1)
		pcicfginit();

	if(prev == nil)
		prev = pcilist;
	else
		prev = prev->list;

	while(prev != nil){
		if((vid == 0 || prev->vid == vid)
		&& (did == 0 || prev->did == did))
			break;
		prev = prev->list;
	}
	return prev;
}

Pcidev*
pcimatchtbdf(int tbdf)
{
	Pcidev *pcidev;

	if(pcicfgmode == -1)
		pcicfginit();

	for(pcidev = pcilist; pcidev != nil; pcidev = pcidev->list){
		if(pcidev->tbdf == tbdf)
			break;
	}
	return pcidev;
}

static char *
ccru2name(int ccru)
{
	switch (ccru>>8) {
	case 0x01:		/* mass storage controller */
		return "disks";
	case 0x02:		/* network controller */
		return "net";	/* probably ether */
	case 0x03:		/* display controller */
		return "video";
	case 0x04:		/* multimedia device */
		return "audio";
	case 0x07:		/* simple communication controllers */
		return "serial";
	case 0x08:		/* base system peripherals */
		return "basic";
	case 0x09:		/* input devices */
		return "input";
	case 0x0A:		/* docking stations */
		return "dock";
	case 0x0B:		/* processors */
		return "cpu";
	case 0x0C:		/* serial bus controllers */
		return "usb";
	case 0x00:
		return "memct0";	
	case 0x05:		/* memory controller */
		return "memctl";
	case 0x06:		/* bridge device */
		return "bridge";
	default:
		return "*GOK*";
	}
}

static char *
vid2name(int vid)
{
	switch (vid) {
	case 0x1000:
		return "ncr";
	case 0x1002:
		return "ati";
	case 0x100b:
		return "natsemi";
	case 0x1011:
		return "dec";
	case 0x1013:
		return "cirrus";
	case 0x1022:
		return "amd";
	case 0x1023:
		return "cyber?";
	case 0x102b:
		return "matrox";
	case 0x102c:
		return "hiq";
	case 0x1039:
		return "sis";
	case 0x104b:
		return "mylex";
	case 0x105a:
		return "promise";
	case 0x105d:
		return "number9";
	case 0x10a9:
		return "sgi";
	case 0x10b7:
		return "3com";
	case 0x10c8:
		return "neomagic";	/* or magicgraph */
	case 0x10de:
		return "nvidia";
	case 0x11ab:
		return "marvell";
	case 0x11ad:
		return "(pnic?)";
	case 0x121a:
		return "voodoo";
	case 0x12ae:
		return "alteon";
	case 0x1385:
		return "netgear";
	case 0x14c1:
		return "myri";
	case 0x15ad:
		return "vmware";
	case 0x16ec:
		return "usrobot";
	case 0x5333:			/* "S" "3".  har, har. */
		return "s3";
	case 0x8086:
		return "intel";
	default:
		return "*GOK*";
	}
}

void
pcihinv(Pcidev* p, ulong flags)
{
	int i;
	Pcidev *t;

	if(p == nil) {
		p = pciroot;
		print("bus dev type ");
		if (flags)
			print("%7s", "");
		print("vid  ");
		if (flags)
			print("%8s", "");
		print("did intl memory\n");
	}
	for(t = p; t != nil; t = t->link) {
		print("%d.%2d.%d %.4ux", BUSBNO(t->tbdf), BUSDNO(t->tbdf),
			BUSFNO(t->tbdf), t->ccru);
		if (flags)
			print(" %-6s", ccru2name(t->ccru));
		print(" %.4ux", t->vid);
		if (flags)
			print(" %-7s", vid2name(t->vid));
		print(" %.4ux %2d  ", t->did, t->intl);

		for(i = 0; i < nelem(p->mem); i++) {
			if(t->mem[i].size == 0)
				continue;
			print("%d:%.8lux %d ", i,
				t->mem[i].bar, t->mem[i].size);
		}
		print("\n");
	}
	while(p != nil) {
		if(p->bridge != nil)
			pcihinv(p->bridge, flags);
		p = p->link;
	}
}

void
pcireset(void)
{
	Pcidev *p;
	int pcr;

	if(pcicfgmode == -1)
		pcicfginit();

	for(p = pcilist; p != nil; p = p->list){
		pcr = pcicfgr16(p, PciPSR);
		pcicfgw16(p, PciPSR, pcr & ~0x04);
	}
}

void
pcisetbme(Pcidev* p)
{
	int pcr;

	pcr = pcicfgr16(p, PciPCR);
	pcr |= 0x04;
	pcicfgw16(p, PciPCR, pcr);
}

void
pciclrbme(Pcidev* p)
{
	p->pcr &= ~MASen;
	pcicfgw16(p, PciPCR, p->pcr);
}
