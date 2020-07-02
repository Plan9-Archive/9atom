#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "apic.h"
#include "io.h"
#include "adr.h"

typedef struct Rbus Rbus;
typedef struct Rdt Rdt;

struct Rbus {
	Rbus	*next;
	int	devno;
	Rdt	*rdt;
};

struct Rdt {
	Apic	*apic;
	int	intin;
	u32int	lo;

	int	ref;				/* could map to multiple busses */
	int	enabled;				/* times enabled */
};

enum {						/* IOAPIC registers */
	Ioregsel	= 0x00,			/* indirect register address */
	Iowin		= 0x04,			/* indirect register data */
	Ioipa		= 0x08,			/* IRQ Pin Assertion */
	Ioeoi		= 0x10,			/* EOI */

	Ioapicid	= 0x00,			/* Identification */
	Ioapicver	= 0x01,			/* Version */
	Ioapicarb	= 0x02,			/* Arbitration */
	Ioabcfg		= 0x03,			/* Boot Coniguration */
	Ioredtbl	= 0x10,			/* Redirection Table */
};

static Rdt rdtarray[Nrdt];
static int nrdtarray;
static Rbus* rdtbus[Nbus];
static Rdt* rdtvecno[IdtMAX+1];

static Lock idtnolock;
static int idtno = IdtIOAPIC;

Apic	xioapic[Napic];

static void
rtblget(Apic* apic, int sel, u32int* hi, u32int* lo)
{
	sel = Ioredtbl + 2*sel;

	apic->addr[Ioregsel] = sel+1;
	*hi = apic->addr[Iowin];
	apic->addr[Ioregsel] = sel;
	*lo = apic->addr[Iowin];
}

static void
rtblput(Apic* apic, int sel, u32int hi, u32int lo)
{
	sel = Ioredtbl + 2*sel;

	apic->addr[Ioregsel] = sel+1;
	apic->addr[Iowin] = hi;
	apic->addr[Ioregsel] = sel;
	apic->addr[Iowin] = lo;
}

Rdt*
rdtlookup(Apic *apic, int intin)
{
	int i;
	Rdt *r;

	for(i = 0; i < nrdtarray; i++){
		r = rdtarray + i;
		if(apic == r->apic && intin == r->intin)
			return r;
	}
	return nil;
}

void
ioapicintrinit(int busno, int apicno, int intin, int devno, u32int lo)
{
	Rbus *rbus;
	Rdt *rdt;
	Apic *apic;

	if(busno >= Nbus || apicno >= Napic || nrdtarray >= Nrdt)
		return;
	apic = &xioapic[apicno];
	if(!apic->useable || intin >= apic->nrdt)
		return;

	rdt = rdtlookup(apic, intin);
	if(rdt == nil){
		rdt = &rdtarray[nrdtarray++];
		rdt->apic = apic;
		rdt->intin = intin;
		rdt->lo = lo;
	}else{
		if(lo != rdt->lo){
			print("mutiple irq botch bus %d %d/%d/%d lo %d vs %d\n",
				busno, apicno, intin, devno, lo, rdt->lo);
			return;
		}
		DBG("dup rdt %d %d %d %d %.8ux\n", busno, apicno, intin, devno, lo);
	}
	rdt->ref++;
	rbus = malloc(sizeof *rbus);
	rbus->rdt = rdt;
	rbus->devno = devno;
	rbus->next = rdtbus[busno];
	rdtbus[busno] = rbus;
}

/*
 * deal with ioapics at the same physical address.  seen on
 * certain supermicro atom systems.  the hope is that only
 * one will be used, and it will be the second one initialized.
 * (the pc kernel ignores this issue.)  it could be that mp and
 * acpi have different numbering?
 */
static Apic*
dupaddr(uintmem pa)
{
	int i;
	Apic *p;

	for(i = 0; i < nelem(xioapic); i++){
		p = xioapic + i;
		if(p->paddr == pa)
			return p;
	}
	return nil;
}

void
ioapicinit(int id, int ibase, uintmem pa)
{
	Apic *apic, *p;
	static int base;

	/*
	 * Mark the IOAPIC useable if it has a good ID
	 * and the registers can be mapped.
	 */
	if(id >= Napic || (apic = xioapic+id)->useable)
		return;

	if((p = dupaddr(pa)) != nil){
		print("ioapic%d: same pa as apic%ld\n", id, p-xioapic);
		if(ibase != -1)
			return;		/* mp irqs reference mp apic#s */
		apic->addr = p->addr;
	}
	else{
		adrmapck(pa, 1024, Aapic, Mfree);	/* not in adr? */
		if((apic->addr = vmap(pa, 1024)) == nil){
			print("ioapic%d: can't vmap %#P\n", id, pa);
			return;
		}
	}
	apic->useable = 1;
	apic->paddr = pa;

	/*
	 * Initialise the I/O APIC.
	 * The MultiProcessor Specification says it is the
	 * responsibility of the O/S to set the APIC ID.
	 */
	lock(apic);
	apic->addr[Ioregsel] = Ioapicver;
	apic->nrdt = (apic->addr[Iowin]>>16 & 0xff) + 1;
	if(ibase != -1)
		apic->ibase = ibase;
	else{
		apic->ibase = base;
		base += apic->nrdt;
	}
	apic->addr[Ioregsel] = Ioapicid;
	apic->addr[Iowin] = id<<24;
	unlock(apic);
}

void
ioapicdump(void)
{
	int i, n;
	Rbus *rbus;
	Rdt *rdt;
	Apic *apic;
	u32int hi, lo;

	if(!DBGFLG)
		return;
	for(i = 0; i < Napic; i++){
		apic = &xioapic[i];
		if(!apic->useable || apic->addr == 0)
			continue;
		print("ioapic %d addr %#p nrdt %d ibase %d\n",
			i, apic->addr, apic->nrdt, apic->ibase);
		for(n = 0; n < apic->nrdt; n++){
			lock(apic);
			rtblget(apic, n, &hi, &lo);
			unlock(apic);
			print(" rdt %2.2d %#8.8ux %#8.8ux\n", n, hi, lo);
		}
	}
	for(i = 0; i < Nbus; i++){
		if((rbus = rdtbus[i]) == nil)
			continue;
		print("iointr bus %d:\n", i);
		for(; rbus != nil; rbus = rbus->next){
			rdt = rbus->rdt;
			print(" apic %ld devno %#ux (%d %d) intin %d lo %#ux ref %d\n",
				rdt->apic-xioapic, rbus->devno, rbus->devno>>2,
				rbus->devno & 0x03, rdt->intin, rdt->lo, rdt->ref);
		}
	}
}

static char*
ioapicprint(char *p, char *e, Ioapic *a, int i)
{
	char *s;

	s = "ioapic";
	p = seprint(p, e, "%-8s ", s);
	p = seprint(p, e, "%8ux ", i);
	p = seprint(p, e, "%6d ", a->nrdt);
	p = seprint(p, e, "%6d ", a->ibase);
	p = seprint(p, e, "%#P ", a->paddr);
	p = seprint(p, e, "\n");
	return p;
}

static long
ioapicread(Chan*, void *a, long n, vlong off)
{
	char *s, *e, *p;
	long i, r;

	s = malloc(READSTR);
	e = s+READSTR;
	p = s;

	for(i = 0; i < nelem(xioapic); i++)
		if(xioapic[i].useable)
			p = ioapicprint(p, e, xioapic + i, i);
	r = -1;
	if(!waserror()){
		r = readstr(off, a, n, s);
		poperror();
	}
	free(s);
	return r;
}

void
ioapiconline(void)
{
	int i;
	Apic *apic;

	addarchfile("ioapic", 0444, ioapicread, nil);
	for(apic = xioapic; apic < &xioapic[Napic]; apic++){
		if(!apic->useable || apic->addr == nil)
			continue;
		for(i = 0; i < apic->nrdt; i++){
			lock(apic);
			rtblput(apic, i, 0, Im);
			unlock(apic);
		}
	}
	ioapicdump();
}

static void
ioapicintrdd(u32int* hi, u32int* lo)
{
	static int i;

	/*
	 * Set delivery mode (lo) and destination field (hi)
	 *
	 * Currently, assign each interrupt to a different CPU
	 * using physical mode delivery.  Using the topology
	 * (packages/cores/threads) could be helpful.
	 */
	for(;; i = (i+1) % nelem(xlapic)){
		if(!xlapic[i].useable)
			continue;
		if(sys->machptr[xlapic[i].machno] == nil)
			continue;
		if(sys->machptr[xlapic[i].machno]->online != 0)
			break;
	}
	*hi = i++<<24;
	*lo |= Pm|MTf;
}

int
nextvec(void)
{
	uint vecno;

	lock(&idtnolock);
	vecno = idtno;
	idtno = (idtno+8) % IdtMAX;
	if(idtno < IdtIOAPIC)
		idtno += IdtIOAPIC;
	unlock(&idtnolock);

	return vecno;
}

static int
msimask(Vkey *v, int mask)
{
	Pcidev *p;

	p = pcimatchtbdf(v->tbdf);
	if(p == nil)
		return -1;
	return pcimsimask(p, mask);
}

static int
intrenablemsi(Vctl* v, Pcidev *p)
{
	uint vno, lo, hi;
	uvlong msivec;

	vno = nextvec();

	lo = IPlow | TMedge | vno;
	ioapicintrdd(&hi, &lo);

	if(lo & Lm)
		lo |= MTlp;

	msivec = (uvlong)hi<<32 | lo;
	if(pcimsienable(p, msivec) == -1)
		return -1;
	v->isr = lapicisr;
	v->eoi = lapiceoi;
	v->vno = vno;
	v->type = "msi";
	v->mask = msimask;

	DBG("msiirq: %T: enabling %.16llux %s irq %d vno %d\n", p->tbdf, msivec, v->name, v->irq, vno);
	return vno;
}

int
disablemsi(Vctl*, Pcidev *p)
{
	if(p == nil)
		return -1;
	return pcimsimask(p, 1);
}

int
ioapicintrenable(Vctl* v)
{
	Rbus *rbus;
	Rdt *rdt;
	u32int hi, lo;
	int busno, devno, vecno;

	/*
	 * Bridge between old and unspecified new scheme,
	 * the work in progress...
	 */
	if(v->tbdf == BUSUNKNOWN){
		if(v->irq >= IrqLINT0 && v->irq <= MaxIrqLAPIC){
			if(v->irq != IrqSPURIOUS)
				v->isr = lapiceoi;
			v->type = "lapic";
			return v->irq;
		}
		else{
			/*
			 * Legacy ISA.
			 * Make a busno and devno using the
			 * ISA bus number and the irq.
			 */
			extern int mpisabusno;

			if(mpisabusno == -1)
				panic("no ISA bus allocated");
			busno = mpisabusno;
			devno = v->irq<<2;
		}
	}
	else if(BUSTYPE(v->tbdf) == BusPCI){
		/*
		 * PCI.
		 * Make a devno from BUSDNO(tbdf) and pcidev->intp.
		 */
		Pcidev *pcidev;

		busno = BUSBNO(v->tbdf);
		if((pcidev = pcimatchtbdf(v->tbdf)) == nil)
			panic("no PCI dev for tbdf %#8.8ux", v->tbdf);
		if((vecno = intrenablemsi(v, pcidev)) != -1)
			return vecno;
		disablemsi(v, pcidev);
		if((devno = pcicfgr8(pcidev, PciINTP)) == 0)
			panic("no INTP for tbdf %#8.8ux", v->tbdf);
		devno = BUSDNO(v->tbdf)<<2|(devno-1);
		DBG("ioapicintrenable: tbdf %#8.8ux busno %d devno %d\n",
			v->tbdf, busno, devno);
	}
	else{
		SET(busno, devno);
		panic("unknown tbdf %#8.8ux", v->tbdf);
	}

	rdt = nil;
	for(rbus = rdtbus[busno]; rbus != nil; rbus = rbus->next)
		if(rbus->devno == devno){
			rdt = rbus->rdt;
			break;
		}
	if(rdt == nil){
		extern int mpisabusno;

		/*
		 * First crack in the smooth exterior of the new code:
		 * some BIOS make an MPS table where the PCI devices are
		 * just defaulted to ISA.
		 * Rewrite this to be cleaner.
		 */
		if((busno = mpisabusno) == -1)
			return -1;
		devno = v->irq<<2;
		for(rbus = rdtbus[busno]; rbus != nil; rbus = rbus->next)
			if(rbus->devno == devno){
				rdt = rbus->rdt;
				break;
			}
		DBG("isa: tbdf %#8.8ux busno %d devno %d %#p\n",
			v->tbdf, busno, devno, rdt);
	}
	if(rdt == nil)
		return -1;

	/*
	 * Second crack:
	 * what to do about devices that intrenable/intrdisable frequently?
	 * 1) there is no ioapicdisable yet;
	 * 2) it would be good to reuse freed vectors.
	 * Oh bugger.
	 */
	/*
	 * This is a low-frequency event so just lock
	 * the whole IOAPIC to initialise the RDT entry
	 * rather than putting a Lock in each entry.
	 */
	lock(rdt->apic);
	DBG("%T: %ld/%d/%d (%d)\n", v->tbdf, rdt->apic - xioapic, rbus->devno, rdt->intin, devno);
	if((rdt->lo & 0xff) == 0){
		vecno = nextvec();
		rdt->lo |= vecno;
		rdtvecno[vecno] = rdt;
	}else
		DBG("%T: mutiple irq bus %d dev %d\n", v->tbdf, busno, devno);

	rdt->enabled++;
	lo = (rdt->lo & ~Im);
	ioapicintrdd(&hi, &lo);
	rtblput(rdt->apic, rdt->intin, hi, lo);
	vecno = lo & 0xff;
	unlock(rdt->apic);

	DBG("busno %d devno %d hi %#8.8ux lo %#8.8ux vecno %d\n",
		busno, devno, hi, lo, vecno);
	v->isr = lapicisr;
	v->eoi = lapiceoi;
	v->vno = vecno;
	v->type = "ioapic";

	return vecno;
}

int
ioapicintrdisable(int vecno)
{
	Rdt *rdt;

	/*
	 * FOV. Oh dear. This isn't very good.
	 * Fortunately rdtvecno[vecno] is static
	 * once assigned.
	 * Must do better.
	 *
	 * What about any pending interrupts?
	 */
	if(vecno < 0 || vecno > MaxVectorAPIC){
		panic("ioapicintrdisable: vecno %d out of range", vecno);
		return -1;
	}
	if((rdt = rdtvecno[vecno]) == nil){
		panic("ioapicintrdisable: vecno %d has no rdt", vecno);
		return -1;
	}

	lock(rdt->apic);
	rdt->enabled--;
	if(rdt->enabled == 0)
		rtblput(rdt->apic, rdt->intin, 0, rdt->lo);
	unlock(rdt->apic);

	return 0;
}
