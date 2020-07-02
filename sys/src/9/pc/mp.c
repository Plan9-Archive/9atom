#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"

#include "mp.h"
#include "apbootstrap.h"

enum {
	Dfake		= 1<<0,
	Dioapic		= 1<<1,
	Dvec		= 1<<2,
	Dmsi		= 1<<3,
	Dmsichat	= 1<<4,

	Debug		= 0,
};

#define dprint(c, ...)	if(Debug & c)iprint(__VA_ARGS__); else{}

Bus	*mpbus;
int	mpisabus = -1;
int	mpeisabus = -1;
Apic	mplapic[MaxAPICNO+1];
Apic	mpioapic[MaxAPICNO+1];
Apic	*procvec[MaxAPICNO+1];

static Bus* mpbuslast;
extern int i8259elcr;			/* mask of level-triggered interrupts */
static Lock mpveclk;
static uchar mprdvec[256/8];
static Ref mpvnoref;			/* unique vector assignment */
/*static*/ int mpmachno;

static char* buses[] = {
	"CBUSI ",
	"CBUSII",
	"EISA  ",
	"FUTURE",
	"INTERN",
	"ISA   ",
	"MBI   ",
	"MBII  ",
	"MCA   ",
	"MPI   ",
	"MPSA  ",
	"NUBUS ",
	"PCI   ",
	"PCMCIA",
	"TC    ",
	"VL    ",
	"VME   ",
	"XPRESS",
	0,
};

static void
mprdstart(Apic *a)
{
	ilock(&mpveclk);
	a->used = 2;
	lapicsetmasks(mplapic, nelem(mplapic));
	mprdvec[a->apicno/8] |= 1<<(a->apicno%8);
	iunlock(&mpveclk);
}

static int
mprdstarted(Apic *a)
{
	int i;

	ilock(&mpveclk);
	i = (mprdvec[a->apicno/8] & 1<<(a->apicno%8)) != 0;
	iunlock(&mpveclk);
	return i;
}

static Apic*
mkprocessor(PCMPprocessor* p)
{
	Apic *apic;

	if(!(p->flags & PcmpEN) || p->apicno > MaxAPICNO)
		return 0;

	apic = &mplapic[p->apicno];
	if(apic->used)
		print("apic conflict: proc gets %d\n", apic->type);
	apic->used = 1;
	apic->type = PcmpPROCESSOR;
	apic->apicno = p->apicno;
	apic->flags = p->flags;
	apic->lintr[0] = ApicIMASK;
	apic->lintr[1] = ApicIMASK;

	/* machines are not necessarly started in order */
	if(p->flags & PcmpBP)
		apic->machno = 0;
	else
		apic->machno = ++mpmachno;
	procvec[mpmachno] = apic;
	return apic;
}

static Bus*
mkbus(PCMPbus* p)
{
	Bus *bus;
	int i;

	for(i = 0; buses[i]; i++){
		if(strncmp(buses[i], p->string, sizeof(p->string)) == 0)
			break;
	}
	if(buses[i] == 0)
		return 0;

	bus = xalloc(sizeof(Bus));
	if(mpbus)
		mpbuslast->next = bus;
	else
		mpbus = bus;
	mpbuslast = bus;

	bus->type = i;
	bus->busno = p->busno;
	if(bus->type == BusEISA){
		bus->po = PcmpLOW;
		bus->el = PcmpLEVEL;
		if(mpeisabus != -1)
			print("mkbus: more than one EISA bus\n");
		mpeisabus = bus->busno;
	}
	else if(bus->type == BusPCI){
		bus->po = PcmpLOW;
		bus->el = PcmpLEVEL;
	}
	else if(bus->type == BusISA){
		bus->po = PcmpHIGH;
		bus->el = PcmpEDGE;
		if(mpisabus != -1)
			print("mkbus: more than one ISA bus\n");
		mpisabus = bus->busno;
	}
	else{
		bus->po = PcmpHIGH;
		bus->el = PcmpEDGE;
	}

	return bus;
}

static Bus*
mpgetbus(int busno)
{
	Bus *bus;

	for(bus = mpbus; bus; bus = bus->next){
		if(bus->busno == busno)
			return bus;
	}
	print("mpgetbus: can't find bus %d\n", busno);

	return 0;
}

static Apic*
mkioapic(PCMPioapic* p)
{
	Apic *apic;
	void *va;

	if(!(p->flags & PcmpEN) || p->apicno > MaxAPICNO)
		return 0;

	/*
	 * Map the I/O APIC.
	 */
	if((va = vmap(p->addr, 1024)) == nil){
		print("apic %d: failed to map %#p\n", p->apicno, p->addr);
		return 0;
	}

	apic = &mpioapic[p->apicno];
	if(apic->used)
		print("apic conflict: ioapic gets %d\n", apic->type);
	apic->used = 1;
	apic->type = PcmpIOAPIC;
	apic->apicno = p->apicno;
	apic->addr = va;
	apic->paddr = p->addr;
	apic->flags = p->flags;

	dprint(Dioapic, "ioapic%d type=%d [%d] flags %ux paddr %p\n",
		p->apicno, p->type, PcmpIOAPIC, p->flags, p->addr);

	return apic;
}

static Aintr*
mkiointr(PCMPintr* p)
{
	Bus *bus;
	Aintr *aintr;

	/*
	 * According to the MultiProcessor Specification, a destination
	 * I/O APIC of 0xFF means the signal is routed to all I/O APICs.
	 * It's unclear how that can possibly be correct so treat it as
	 * an error for now.
	 */
	if(p->apicno == 0xFF)
		return 0;
	if((bus = mpgetbus(p->busno)) == 0)
		return 0;

	aintr = xalloc(sizeof(Aintr));
	aintr->intr = p;
	aintr->apic = &mpioapic[p->apicno];
	aintr->next = bus->aintr;
	bus->aintr = aintr;

	return aintr;
}

static int
mpintrinit(Bus* bus, PCMPintr* intr, int vno, int /*irq*/)
{
	int el, po, v;

	/*
	 * Parse an I/O or Local APIC interrupt table entry and
	 * return the encoded vector.
	 */
	v = vno;

	po = intr->flags & PcmpPOMASK;
	el = intr->flags & PcmpELMASK;

	switch(intr->intr){

	default:				/* PcmpINT */
		v |= ApicLOWEST;
		break;

	case PcmpNMI:
		v |= ApicNMI;
		po = PcmpHIGH;
		el = PcmpEDGE;
		break;

	case PcmpSMI:
		v |= ApicSMI;
		break;

	case PcmpExtINT:
		v |= ApicExtINT;
		/*
		 * The AMI Goliath doesn't boot successfully with it's LINTR0
		 * entry which decodes to low+level. The PPro manual says ExtINT
		 * should be level, whereas the Pentium is edge. Setting the
		 * Goliath to edge+high seems to cure the problem. Other PPro
		 * MP tables (e.g. ASUS P/I-P65UP5 have a entry which decodes
		 * to edge+high, so who knows.
		 * Perhaps it would be best just to not set an ExtINT entry at
		 * all, it shouldn't be needed for SMP mode.
		 */
		po = PcmpHIGH;
		el = PcmpEDGE;
		break;
	}

	/*
	 */
	if(bus->type == BusEISA && !po && !el /*&& !(i8259elcr & (1<<irq))*/){
		po = PcmpHIGH;
		el = PcmpEDGE;
	}
	if(!po)
		po = bus->po;
	if(po == PcmpLOW)
		v |= ApicLOW;
	else if(po != PcmpHIGH){
		print("mpintrinit: bad polarity 0x%uX\n", po);
		return ApicIMASK;
	}

	if(!el)
		el = bus->el;
	if(el == PcmpLEVEL)
		v |= ApicLEVEL;
	else if(el != PcmpEDGE){
		print("mpintrinit: bad trigger 0x%uX\n", el);
		return ApicIMASK;
	}

	return v;
}

static int
mklintr(PCMPintr* p)
{
	Apic *apic;
	Bus *bus;
	int intin, v;

	/*
	 * The offsets of vectors for LINT[01] are known to be
	 * 0 and 1 from the local APIC vector space at VectorLAPIC.
	 */
	if((bus = mpgetbus(p->busno)) == 0)
		return 0;
	intin = p->intin;

	/*
	 * Pentium Pros have problems if LINT[01] are set to ExtINT
	 * so just bag it, SMP mode shouldn't need ExtINT anyway.
	 */
	if(p->intr == PcmpExtINT || p->intr == PcmpNMI)
		v = ApicIMASK;
	else
		v = mpintrinit(bus, p, VectorLAPIC+intin, p->irq);

	if(p->apicno == 0xFF){
		for(apic = mplapic; apic <= &mplapic[MaxAPICNO]; apic++){
			if((apic->flags & PcmpEN)
			&& apic->type == PcmpPROCESSOR)
				apic->lintr[intin] = v;
		}
	}
	else{
		apic = &mplapic[p->apicno];
		if((apic->flags & PcmpEN) && apic->type == PcmpPROCESSOR)
			apic->lintr[intin] = v;
	}

	return v;
}

static void
checkmtrr(void)
{
	int i, vcnt;
	Mach *mach0;

	/*
	 * If there are MTRR registers, snarf them for validation.
	 */
	if(!(m->cpuiddx & 0x1000))
		return;

	rdmsr(0x0FE, &m->mtrrcap);
	rdmsr(0x2FF, &m->mtrrdef);
	if(m->mtrrcap & 0x0100){
		rdmsr(0x250, &m->mtrrfix[0]);
		rdmsr(0x258, &m->mtrrfix[1]);
		rdmsr(0x259, &m->mtrrfix[2]);
		for(i = 0; i < 8; i++)
			rdmsr(0x268+i, &m->mtrrfix[(i+3)]);
	}
	vcnt = m->mtrrcap & 0x00FF;
	if(vcnt > nelem(m->mtrrvar))
		vcnt = nelem(m->mtrrvar);
	for(i = 0; i < vcnt; i++)
		rdmsr(0x200+i, &m->mtrrvar[i]);

	/*
	 * If not the bootstrap processor, compare.
	 */
	if(m->machno == 0)
		return;

	mach0 = MACHP(0);
	if(mach0->mtrrcap != m->mtrrcap)
		print("mtrrcap%d: %lluX %lluX\n",
			m->machno, mach0->mtrrcap, m->mtrrcap);
	if(mach0->mtrrdef != m->mtrrdef)
		print("mtrrdef%d: %lluX %lluX\n",
			m->machno, mach0->mtrrdef, m->mtrrdef);
	for(i = 0; i < 11; i++){
		if(mach0->mtrrfix[i] != m->mtrrfix[i])
			print("mtrrfix%d: i%d: %lluX %lluX\n",
				m->machno, i, mach0->mtrrfix[i], m->mtrrfix[i]);
	}
	for(i = 0; i < vcnt; i++){
		if(mach0->mtrrvar[i] != m->mtrrvar[i])
			print("mtrrvar%d: i%d: %lluX %lluX\n",
				m->machno, i, mach0->mtrrvar[i], m->mtrrvar[i]);
	}
}

static void
squidboy(Apic* apic)
{
//	iprint("Hello Squidboy\n");

	machinit();
	mmuinit();

	cpuidentify();
//	cpuidprint();
	checkmtrr();

	mprdstart(apic);

	lapicinit(apic);
	lapiconline();
	syncclock();
	timersinit();

	fpoff();

	lock(&active);
	active.machs |= 1<<m->machno;
	unlock(&active);

	while(!active.thunderbirdsarego)
		microdelay(100);
	schedinit();
}

static void
mpstartap(Apic* apic)
{
	ulong *apbootp;
	PTE *pdb, *pte;
	Mach *mach, *mach0;
	int i, machno;
	uchar *p;

	mach0 = MACHP(0);

	/*
	 * Initialise the AP page-tables and Mach structure. The page-tables
	 * are the same as for the bootstrap processor with the exception of
	 * the PTE for the Mach structure.
	 * Xspanalloc will panic if an allocation can't be made.
	 */
	p = xspanalloc(4*BY2PG, BY2PG, 0);
	pdb = (PTE*)p;
	memmove(pdb, mach0->pdb, BY2PG);
	p += BY2PG;

	if((pte = mmuwalk(pdb, MACHADDR, 1, 0)) == nil)
		return;
	memmove(p, KADDR(PPN(*pte)), BY2PG);
	*pte = PADDR(p)|PTEWRITE|PTEVALID;
	if(mach0->havepge)
		*pte |= PTEGLOBAL;
	p += BY2PG;

	mach = (Mach*)p;
	if((pte = mmuwalk(pdb, MACHADDR, 2, 0)) == nil)
		return;
	*pte = PADDR(mach)|PTEWRITE|PTEVALID;
	if(mach0->havepge)
		*pte |= PTEGLOBAL;
	p += BY2PG;

	machno = apic->machno;
	MACHP(machno) = mach;
	mach->machno = machno;
	mach->pdb = pdb;
	mach->gdt = (Segdesc*)p;	/* filled by mmuinit */

	/*
	 * Tell the AP where its kernel vector and pdb are.
	 * The offsets are known in the AP bootstrap code.
	 */
	apbootp = (ulong*)(APBOOTSTRAP+0x08);
	*apbootp++ = (ulong)squidboy;
	*apbootp++ = PADDR(pdb);
	*apbootp = (ulong)apic;

	/*
	 * Universal Startup Algorithm.
	 */
	p = KADDR(0x467);
	*p++ = PADDR(APBOOTSTRAP);
	*p++ = PADDR(APBOOTSTRAP)>>8;
	i = (PADDR(APBOOTSTRAP) & ~0xFFFF)/16;
	/* code assumes i==0 */
	if(i != 0)
		print("mp: bad APBOOTSTRAP\n");
	*p++ = i;
	*p = i>>8;

	nvramwrite(0x0F, 0x0A);
	lapicstartap(apic, PADDR(APBOOTSTRAP));
	for(i = 0; i < 1000; i++){
		if(mprdstarted(apic))
			break;
		delay(10);
	}
	if(i == 1000)
		print("cpu%d: timeout apic %.2ux\n", machno, apic->apicno);
	nvramwrite(0x0F, 0x00);
}

static char*
busprint(char *p, char *e, Bus *b)
{
	char *t;
	
	t = buses[b->type];
	return seprint(p, e, "%s %d po %ud el %ud\n", t, b->busno, b->po, b->el);
}

static char *ittab[] = {
	"vec",
	"nmi",
	"smi",
	"ext",
};

static char*
mpprint(char *p, char *e, PCMPintr *m)
{
	char *t;

	t = "lint";
	if(m->type == 3)
		t = "int";
	return seprint(p, e, "\t%s %s flag %d birq %d -> apicno %ux intin %ud\n",
		t, ittab[m->intr], m->flags, m->irq, m->apicno, m->intin);
}

long
mpread(Chan *, void *a, long n, vlong off)
{
	char *s, *e, *p;
	long r;
	Aintr *i;
	Bus *b;

	s = malloc(32*1024);
	e = s+32*1024;
	p = s;

	for(b = mpbus; b; b = b->next){
		p = busprint(p, e, b);
		for(i = b->aintr; i; i = i->next)
			p = mpprint(p, e, i->intr);
	}

	r = readstr(off, a, n, s);
	free(s);
	return r;
}

static char*
apicprint1(char *p, char *e, Apic *a)
{
	char *s;

	switch(a->type){
	case PcmpIOAPIC:
		s = "ioapic";
		break;
	case PcmpPROCESSOR:
		s = "proc";
		break;
	default:
		s = "*gok*";
		break;
	}
	p = seprint(p, e, "%-8s ", s);
	p = seprint(p, e, "%8ux ", a->apicno);
	p = seprint(p, e, "%.8ux ", a->apicid0);
	p = seprint(p, e, "%.8ux ", a->dest);
	p = seprint(p, e, "%.8ux ", a->mask);
	p = seprint(p, e, "%c", a->flags & PcmpBP? 'b': ' ');
	p = seprint(p, e, "%c ", a->flags & PcmpEN? 'e': ' ');
//	p = seprint(p, e, "%8ux %8ux", a->lintr[0], a->lintr[1]);
	p = seprint(p, e, "%12d\n", a->machno);
	return p;
}

long
mpapicread(Chan*, void *a, long n, vlong off)
{
	char *s, *e, *p;
	long i, r;

	s = malloc(READSTR);
	e = s+READSTR;
	p = s;

	for(i = 0; i < MaxAPICNO+1; i++){
		if(mpioapic[i].used == 1)
			p = apicprint1(p, e, mpioapic + i);
		if(mplapic[i].used != 0)
			p = apicprint1(p, e, mplapic + i);
	}
	r = -1;
	if(!waserror()){
		r = readstr(off, a, n, s);
		poperror();
	}
	free(s);
	return r;
}

static long
mptableread(Chan*, void *v, long n, vlong off)
{
	extern long mptableread0(_MP_*, void*, long, vlong);

	if(_mp_ == 0)
		return 0;
	return mptableread0(_mp_, v, n, off);
}

static long
mpvecread(Chan*, void *v, long n, vlong off)
{
	char *s, *e, *p;
	int i, j, hi, lo, r;
	Apic *a;

	s = malloc(READSTR);
	e = s+READSTR;
	p = s;

	for(i = 0; i < MaxAPICNO+1; i++){
		a = mpioapic + i;
		if(a->used == 0 || a->type != PcmpIOAPIC)
			continue;
		for(j = 0; j <= a->mre; j++){
			ioapicrdtr(a, j, &hi, &lo);
			p = seprint(p, e, "%12d" "%12d %.8ux%.8ux\n", a->apicno, j, hi, lo);
		}
	}
	r = -1;
	if(!waserror()){
		r = readstr(off, v, n, s);
		poperror();
	}
	free(s);
	return r;
}

static void
ensurebsp(void)
{
	vlong r;

	rdmsr(0x1b, &r);		/* lapic addr */
	if(r & 1<<8)		/* bsp bit */
		return;
	panic("mp boot: not bsp");
}

void
mpgo(Apic *bsp, int machno)
{
	char *cp;
	int ncpu, i;

	i8259init();
	syncclock();
	ensurebsp();

	apicinitdest(mplapic, nelem(mplapic));
	lapicinit(bsp);
	mprdstart(bsp);

	/*
	 * These interrupts are local to the processor
	 * and do not appear in the I/O APIC so it is OK
	 * to set them now.
	 */
	intrenable(IrqTIMER, lapicclock, 0, BUSUNKNOWN, "clock");
	intrenable(IrqERROR, lapicerror, 0, BUSUNKNOWN, "lapicerror");
	intrenable(IrqSPURIOUS, lapicspurious, 0, BUSUNKNOWN, "lapicspurious");
	lapiconline();

	checkmtrr();
	addarchfile("mpapic", 0444, mpapicread, nil);
	addarchfile("mpirq", 0444, mpread, nil);
	addarchfile("mptable", 0444, mptableread, nil);
	addarchfile("mpvec", 0444, mpvecread, nil);

	/*
	 * Initialise the application processors.
	 */
	if(cp = getconf("*ncpu")){
		ncpu = strtol(cp, 0, 0);
		if(ncpu < 1)
			ncpu = 1;
	}
	else
		ncpu = MIN(MAXMACH, MaxAPICNO);

	if(ncpu > MAXMACH){
		print("maxmach limited: %d\n", MAXMACH);
		ncpu = MAXMACH;
	}

	if(ncpu > machno)
		ncpu = machno;

	memmove((void*)APBOOTSTRAP, apbootstrap, sizeof(apbootstrap));
	for(i = 1; i < ncpu; i++){
		mpstartap(procvec[i]);
		conf.nmach++;
	}

	print("apic: %ld machs started; %s mode vectors\n", conf.nmach, apicmode());
	/*
	 *  we don't really know the number of processors till
	 *  here.
	 *
	 *  set conf.copymode here if nmach > 1.
	 *  Should look for an ExtINT line and enable it.
	 */
	if(X86FAMILY(m->cpuidax) == 3 || conf.nmach > 1)
		conf.copymode = 1;
}

void
mpinit(void)
{
	PCMP *pcmp;
	uchar *e, *p;
	Apic *apic, *bpapic;
	void *va;
	extern void mpacpi(void);

	if(_mp_ == 0)
		return;
	pcmp = KADDR(_mp_->physaddr);

	/*
	 * Map the local APIC.
	 */
	if((va = vmap(pcmp->lapicbase, 1024)) == nil)
		return;
	print("LAPIC: %#p %#p\n", pcmp->lapicbase, va);
	bpapic = nil;

	/*
	 * Run through the table saving information needed for starting
	 * application processors and initialising any I/O APICs. The table
	 * is guaranteed to be in order such that only one pass is necessary.
	 */
	p = ((uchar*)pcmp)+sizeof(PCMP);
	e = ((uchar*)pcmp)+pcmp->length;
	while(p < e) switch(*p){

	default:
		print("mpinit: unknown PCMP type 0x%uX (e-p 0x%luX)\n",
			*p, e-p);
		while(p < e){
			print("%uX ", *p);
			p++;
		}
		break;

	case PcmpPROCESSOR:
		if(apic = mkprocessor((PCMPprocessor*)p)){
			/*
			 * Must take a note of bootstrap processor APIC
			 * now as it will be needed in order to start the
			 * application processors later and there's no
			 * guarantee that the bootstrap processor appears
			 * first in the table before the others.
			 */
			apic->addr = va;
			apic->paddr = pcmp->lapicbase;
			if(apic->flags & PcmpBP)
				bpapic = apic;
		}
		p += sizeof(PCMPprocessor);
		continue;

	case PcmpBUS:
		mkbus((PCMPbus*)p);
		p += sizeof(PCMPbus);
		continue;

	case PcmpIOAPIC:
		if(apic = mkioapic((PCMPioapic*)p))
			ioapicinit(apic, apic->apicno);
		p += sizeof(PCMPioapic);
		continue;

	case PcmpIOINTR:
		mkiointr((PCMPintr*)p);
		p += sizeof(PCMPintr);
		continue;

	case PcmpLINTR:
		mklintr((PCMPintr*)p);
		p += sizeof(PCMPintr);
		continue;
	}

	mpacpi();		/* hack; needed for some intel mb */

	/*
	 * No bootstrap processor, no need to go further.
	 */
	if(bpapic == nil)
		return;
	mpgo(bpapic, mpmachno+1);
}

int
mpintrdisable(Vctl *v)
{
	Aintr *aintr;

	aintr = v->key;
	if(aintr == nil)
		return -1;	/* msi, already masked */
	ioapicmask(aintr->apic, aintr->intr->intin, 1);
	return 0;
}

static int
mpintrenablex(Vctl* v, int tbdf)
{
	Bus *bus;
	Aintr *aintr;
	Apic *apic;
	Pcidev *pcidev;
	int bno, dno, irq, lo, n, type, vno;
	uint hi;

	/*
	 * Find the bus.
	 */
	type = BUSTYPE(tbdf);
	bno = BUSBNO(tbdf);
	dno = BUSDNO(tbdf);
	if(type == BusISA)
		bno = mpisabus;
	for(bus = mpbus; bus != nil; bus = bus->next){
		if(bus->type != type)
			continue;
		if(bus->busno == bno)
			break;
	}
	if(bus == nil){
		print("ioapicirq: can't find bus type %d\n", type);
		return -1;
	}

	/*
	 * For PCI devices the interrupt pin (INT[ABCD]) and device
	 * number are encoded into the entry irq field, so create something
	 * to match on. The interrupt pin used by the device has to be
	 * obtained from the PCI config space.
	 */
	if(bus->type == BusPCI){
		pcidev = pcimatchtbdf(tbdf);
		if(pcidev != nil && (n = pcicfgr8(pcidev, PciINTP)) != 0)
			irq = (dno<<2)|(n-1);
		else
			irq = -1;
		//print("pcidev %uX: irq %uX v->irq %uX\n", tbdf, irq, v->irq);
	}
	else
		irq = v->irq;

	/*
	 * Find a matching interrupt entry from the list of interrupts
	 * attached to this bus.
	 */
	for(aintr = bus->aintr; aintr; aintr = aintr->next){
		if(aintr->intr->irq != irq)
			continue;

		/*
		 * Check if already enabled. Multifunction devices may share
		 * INT[A-D]# so, if already enabled, check the polarity matches
		 * and the trigger is level.
		 *
		 * Should check the devices differ only in the function number,
		 * but that can wait for the planned enable/disable rewrite.
		 * The RDT read here is safe for now as currently interrupts
		 * are never disabled once enabled.
		 */
		apic = aintr->apic;
		ioapicrdtr(apic, aintr->intr->intin, 0, &lo);
		if(!(lo & ApicIMASK)){
			vno = lo & 0xFF;
			n = mpintrinit(bus, aintr->intr, vno, v->irq);
			n |= ioapicdest(mplapic, nelem(mplapic), &hi);
			lo &= ~(ApicRemoteIRR|ApicDELIVS);
			if(n != lo || !(n & ApicLEVEL)){
				print("mpintrenable: multiple botch irq%d, tbdf %uX, lo %8.8uX, n %8.8uX\n",
					v->irq, tbdf, lo, n);
				return -1;
			}
			dprint(Dvec, "mpint: reenirq%d, tbdf %ux, lo %.8ux, n %.8ux\n",v->irq, tbdf, lo, n);

			v->type = "ioapic";
			v->key = aintr;
			v->isr = lapicisr;
			v->eoi = lapiceoi;
			return vno;
		}

		/*
		 * With the APIC a unique vector can be assigned to each
		 * request to enable an interrupt. There are two reasons this
		 * is a good idea:
		 * 1) to prevent lost interrupts, no more than 2 interrupts
		 *    should be assigned per block of 16 vectors (there is an
		 *    in-service entry and a holding entry for each priority
		 *    level and there is one priority level per block of 16
		 *    interrupts).
		 * 2) each input pin on the IOAPIC will receive a different
		 *    vector regardless of whether the devices on that pin use
		 *    the same IRQ as devices on another pin.
		 */
		lock(&mpvnoref);
		vno = VectorAPIC + mpvnoref.ref*8;
		if(vno > MaxVectorAPIC){
			unlock(&mpvnoref);
			print("mpintrenable: vno %d, irq %d, tbdf %uX\n",
				vno, v->irq, tbdf);
			return -1;
		}
		lo = mpintrinit(bus, aintr->intr, vno, v->irq);
		dprint(Dvec, "lo %#ux: busno %d intr %d vno %d irq %d elcr %#ux\n",
			lo, bus->busno, aintr->intr->irq, vno,
			v->irq, i8259elcr);
		if(lo & ApicIMASK){
			unlock(&mpvnoref);
			continue;
		}
		lock(&mpveclk);
		lo |= ioapicdest(mplapic, nelem(mplapic), &hi);
		unlock(&mpveclk);

		if((apic->flags & PcmpEN) && apic->type == PcmpIOAPIC){
			dprint(Dvec, "ioapicrdtw %.2ux intin %.8ux %.8ux\n",
				apic->apicno, hi, lo);
			ioapicrdtw(apic, aintr->intr->intin, hi, lo);
		}
		else{
			dprint(Dvec, "lo not enabled %#ux %d\n",
				apic->flags, apic->type);
			unlock(&mpvnoref);
			continue;
		}

		mpvnoref.ref++;
		unlock(&mpvnoref);

		v->type = "ioapic";
		v->key = aintr;
		v->isr = lapicisr;
		v->eoi = lapiceoi;

		
		return vno;
	}

	return -1;
}

static int
mpmsimask(Vctl *v, int mask)
{
	Pcidev *p;

	p = pcimatchtbdf(v->tbdf);
	if(p == nil)
		return -1;
	return pcimsimask(p, mask);
}

static int
mpintrenablemsi(Vctl* v, int tbdf)
{
	uint vno, lo, hi;
	uvlong msivec;
	Pcidev *p;

	p = pcimatchtbdf(tbdf);
	if(p == nil)
		return -1;

	lock(&mpvnoref);
	vno = VectorAPIC + mpvnoref.ref*8;		/* really? */
	if(vno > MaxVectorAPIC){
		unlock(&mpvnoref);
		print("msiirq: out of vectors %T %s\n", tbdf, v->name);
		return -1;
	}

	lo = ApicLOW | ApicEDGE | vno;
	lock(&mpveclk);
	lo |= ioapicdest(mplapic, nelem(mplapic), &hi);
	unlock(&mpveclk);

	if(lo & ApicLOGICAL)
		lo |= ApicLOWEST;

	msivec = (uvlong)hi<<32 | lo;
	if(pcimsienable(p, msivec) == -1){
		unlock(&mpvnoref);
		dprint(Dmsichat, "msiirq: %T: can't enable %s\n", p->tbdf, v->name);
		return -1;
	}
	v->type = "msi";
	v->isr = lapicisr;		/* XXX */
	v->eoi = lapiceoi;
	v->mask = mpmsimask;
	dprint(Dmsi, "msiirq: %T: enabling %.16llux %s irq %d vno %d\n", p->tbdf, msivec, v->name, v->irq, vno);
	mpvnoref.ref++;
	unlock(&mpvnoref);
	return vno;
}

int
mpdisablemsi(Vctl*, int tbdf)
{
	Pcidev *p;

	p = pcimatchtbdf(tbdf);
	if(p == nil)
		return -1;
	return pcimsimask(p, 1);
}

int
mpintrenable(Vctl* v)
{
	int irq, tbdf, vno;

	/*
	 * If the bus is known, try it.
	 * BUSUNKNOWN is given both by [E]ISA devices and by
	 * interrupts local to the processor (local APIC, coprocessor
	 * breakpoint and page-fault).
	 */
	tbdf = v->tbdf;
	if(tbdf != BUSUNKNOWN){
		if((vno = mpintrenablemsi(v, tbdf)) != -1)
			return vno;
		mpdisablemsi(v, tbdf);	/* should be in pcireset? */
		if((vno = mpintrenablex(v, tbdf)) != -1)
			return vno;
	}

	irq = v->irq;
	if(irq >= IrqLINT0 && irq <= MaxIrqLAPIC){
		if(irq != IrqSPURIOUS)
			v->isr = lapiceoi;
		v->type = "lapic";
		return VectorPIC+irq;
	}
	if(irq < 0 || irq > MaxIrqPIC){
		print("mpintrenable: irq %d out of range\n", irq);
		return -1;
	}

	/*
	 * Either didn't find it or have to try the default buses
	 * (ISA and EISA). This hack is due to either over-zealousness 
	 * or laziness on the part of some manufacturers.
	 *
	 * The MP configuration table on some older systems
	 * (e.g. ASUS PCI/E-P54NP4) has an entry for the EISA bus
	 * but none for ISA. It also has the interrupt type and
	 * polarity set to 'default for this bus' which wouldn't
	 * be compatible with ISA.
	 */
	if(mpeisabus != -1){
		vno = mpintrenablex(v, MKBUS(BusEISA, 0, 0, 0));
		if(vno != -1)
			return vno;
	}
	if(mpisabus != -1){
		vno = mpintrenablex(v, MKBUS(BusISA, 0, 0, 0));
		if(vno != -1)
			return vno;
	}
	print("mpintrenable: out of choices eisa %d isa %d tbdf %#ux irq %d\n",
		mpeisabus, mpisabus, v->tbdf, v->irq);
	return -1;
}

static Lock mpshutdownlock;

void
mpshutdown(void)
{
	/*
	 * To be done...
	 */
	if(!canlock(&mpshutdownlock)){
		/*
		 * If this processor received the CTRL-ALT-DEL from
		 * the keyboard, acknowledge it. Send an INIT to self.
		 */
#ifdef FIXTHIS
		if(lapicisr(VectorKBD))
			lapiceoi(VectorKBD);
#endif /* FIX THIS */
		arch->introff();
		idle();
	}

	print("apshutdown: active = %#8.8ux\n", active.machs);
	delay(1000);
	splhi();

	/*
	 * INIT all excluding self.
	 */
	lapicicrw(0, 0x000C0000|ApicINIT);

	pcireset();
	i8042reset();

	/*
	 * Often the BIOS hangs during restart if a conventional 8042
	 * warm-boot sequence is tried. The following is Intel specific and
	 * seems to perform a cold-boot, but at least it comes back.
	 * And sometimes there is no keyboard...
	 *
	 * The reset register (0xcf9) is usually in one of the bridge
	 * chips. The actual location and sequence could be extracted from
	 * ACPI but why bother, this is the end of the line anyway.
	 */
	print("no kbd; trying bios warm boot...");
	*(ushort*)KADDR(0x472) = 0x1234;	/* BIOS warm-boot flag */
	outb(0xCF9, 0x02);
	outb(0xCF9, 0x06);

	print("can't reset\n");
	for(;;)
		idle();
}

void
ioapicshutdown(void)
{
	int i;

	for(i = 0; i <= MaxAPICNO; i++)
		if(mpioapic[i].used)
		if(mpioapic[i].type == PcmpIOAPIC)
			ioapicinit(mpioapic + i, mpioapic[i].apicno);
}
