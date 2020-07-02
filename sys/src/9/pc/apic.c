#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "mp.h"

enum {
	Dapicno		= 1<<0,
	Dapicmask	= 1<<1,

	Debug		= Dapicno,
};

enum {					/* Local APIC registers */
	LapicID		= 0x0020,	/* ID */
	LapicVER	= 0x0030,	/* Version */
	LapicTPR	= 0x0080,	/* Task Priority */
	LapicAPR	= 0x0090,	/* Arbitration Priority */
	LapicPPR	= 0x00A0,	/* Processor Priority */
	LapicEOI	= 0x00B0,	/* EOI */
	LapicLDR	= 0x00D0,	/* Logical Destination */
	LapicDFR	= 0x00E0,	/* Destination Format */
	LapicSVR	= 0x00F0,	/* Spurious Interrupt Vector */
	LapicISR	= 0x0100,	/* Interrupt Status (8 registers) */
	LapicTMR	= 0x0180,	/* Trigger Mode (8 registers) */
	LapicIRR	= 0x0200,	/* Interrupt Request (8 registers) */
	LapicESR	= 0x0280,	/* Error Status */
	LapicICRLO	= 0x0300,	/* Interrupt Command */
	LapicICRHI	= 0x0310,	/* Interrupt Command [63:32] */
	LapicTIMER	= 0x0320,	/* Local Vector Table 0 (TIMER) */
	LapicPCINT	= 0x0340,	/* Performance Counter LVT */
	LapicLINT0	= 0x0350,	/* Local Vector Table 1 (LINT0) */
	LapicLINT1	= 0x0360,	/* Local Vector Table 2 (LINT1) */
	LapicERROR	= 0x0370,	/* Local Vector Table 3 (ERROR) */
	LapicTICR	= 0x0380,	/* Timer Initial Count */
	LapicTCCR	= 0x0390,	/* Timer Current Count */
	LapicTDCR	= 0x03E0,	/* Timer Divide Configuration */
};

enum {					/* LapicSVR */
	LapicENABLE	= 0x00000100,	/* Unit Enable */
	LapicFOCUS	= 0x00000200,	/* Focus Processor Checking Disable */
};

enum {					/* LapicICRLO */
					/* [14] IPI Trigger Mode Level (RW) */
	LapicDEASSERT	= 0x00000000,	/* Deassert level-sensitive interrupt */
	LapicASSERT	= 0x00004000,	/* Assert level-sensitive interrupt */

					/* [17:16] Remote Read Status */
	LapicINVALID	= 0x00000000,	/* Invalid */
	LapicWAIT	= 0x00010000,	/* In-Progress */
	LapicVALID	= 0x00020000,	/* Valid */

					/* [19:18] Destination Shorthand */
	LapicFIELD	= 0x00000000,	/* No shorthand */
	LapicSELF	= 0x00040000,	/* Self is single destination */
	LapicALLINC	= 0x00080000,	/* All including self */
	LapicALLEXC	= 0x000C0000,	/* All Excluding self */
};

enum {					/* LapicESR */
	LapicSENDCS	= 0x00000001,	/* Send CS Error */
	LapicRCVCS	= 0x00000002,	/* Receive CS Error */
	LapicSENDACCEPT	= 0x00000004,	/* Send Accept Error */
	LapicRCVACCEPT	= 0x00000008,	/* Receive Accept Error */
	LapicSENDVECTOR	= 0x00000020,	/* Send Illegal Vector */
	LapicRCVVECTOR	= 0x00000040,	/* Receive Illegal Vector */
	LapicREGISTER	= 0x00000080,	/* Illegal Register Address */
};

enum {					/* LapicTIMER */
					/* [17] Timer Mode (RW) */
	LapicONESHOT	= 0x00000000,	/* One-shot */
	LapicPERIODIC	= 0x00020000,	/* Periodic */

					/* [19:18] Timer Base (RW) */
	LapicCLKIN	= 0x00000000,	/* use CLKIN as input */
	LapicTMBASE	= 0x00040000,	/* use TMBASE */
	LapicDIVIDER	= 0x00080000,	/* use output of the divider */
};

enum {					/* LapicTDCR */
	LapicX2		= 0x00000000,	/* divide by 2 */
	LapicX4		= 0x00000001,	/* divide by 4 */
	LapicX8		= 0x00000002,	/* divide by 8 */
	LapicX16	= 0x00000003,	/* divide by 16 */
	LapicX32	= 0x00000008,	/* divide by 32 */
	LapicX64	= 0x00000009,	/* divide by 64 */
	LapicX128	= 0x0000000A,	/* divide by 128 */
	LapicX1		= 0x0000000B,	/* divide by 1 */
};

enum {
	Flat,
	Cluster,
	Phys,
};

static char *modestr[] = {
	"flat",
	"cluster",
	"phys",
};

static int mode;
static ulong* lapicbase;

struct
{
	uvlong	hz;
	ulong	max;
	ulong	min;
	ulong	div;
} lapictimer;

static uint
apicphys(Apic *a)
{
	return a->apicno<<24;
}

static uint
apiclogical(Apic *a)
{
	return a->dest;
}

static int
activeproc(Apic *a)
{
	return a->used==2 && a->type == PcmpPROCESSOR;
}

static int
procmatch(Apic *a)
{
	return a->used && a->type == PcmpPROCESSOR;
}

static uint
lobits(void)
{
	if(mode == Phys)
		return ApicPHYSICAL;
	return ApicLOGICAL;
}

uint
ioapicdest(Apic *a, uint n, uint *hi)
{
	static uint i = ~0;

	for(;;){
		i = (i+1) % n;
		if(activeproc(a+i)){
			*hi = a[i].mask;
			return lobits();
		}
	}
}

static uint
topology(Apic *a, int n)
{
	char *s;
	int i, c, m, nm, x, mx;

	c = x = m = mx = 0;
	for(i = 0; i < n; i++){
		if(!procmatch(a+i))
			continue;
		c++;
		nm = a[i].apicno>>4;
		if(nm == m)
			x++;
		else{
			if(x > mx)
				mx = x;
			x = 1;
		}
		a[i].clusterid = m = nm;
	}
	if(x > mx)
		mx = x;

	mode = Flat;
	if(s = getconf("*apicmode")){
		for(i = 0; i < nelem(modestr); i++)
			if(strcmp(modestr[i], s) == 0)
				mode = i;
	}else{
		if(c > 60 || c>8 && mx > 4)
			mode = Phys;
		else if(c > 8)
			mode = Cluster;
	}

	return c;
}

char*
apicmode(void)
{
	return modestr[mode];
}

uint
apicinitdest(Apic *a, int n)
{
	int i, c;

	topology(a, n);
	c = 0;
	for(i = 0; i < n; i++){
		if(!procmatch(a+i))
			continue;
		switch(mode){
		case Flat:
			a[i].clusterid = 0;
			a[i].dest = 1<<c;
			c++;
			break;
		case Phys:
			a[i].clusterid = 0;
			a[i].dest = a[i].apicno;
			break;
		case Cluster:
		//	a[i].clusterid = c/4;	/* bios knows best */
			a[i].dest = a[i].clusterid<<4 | 1<<(c%4);
			c++;
			break;
		}
		a[i].dest <<= 24;
	}
	if(Debug & Dapicno){
		print("lapicnos: ");
		for(i = 0; i < n; i++)
			if(a[i].used)
				print("%.2ux/%.2ux ", a[i].apicno, a[i].dest>>24);
		print("\n");
	}
	return mode;
}

void
lapicsetmasks(Apic *a, int n)
{
	uint i, j, c;

	switch(mode){
	case Flat:
		c = 0;
		for(i = 0; i < n; i++)
			if(activeproc(a+i))
				c |= a[i].dest;
		for(i = 0; i < n; i++)
			if(activeproc(a+i))
				a[i].mask = c;
		break;
	case Cluster:
		for(i = 0; i < 16; i++){
			c = 0;
			for(j = 0; j < n; j++)
				if(activeproc(a+j) && a[j].clusterid == i)
					c |= a[j].dest;
			for(j = 0; j < n; j++)
				if(activeproc(a+j) && a[j].clusterid == i)
					a[j].mask = c;
		}
		break;
	case Phys:
		for(i = 0; i < n; i++)
			if(activeproc(a+i))
				a[i].mask = a[i].dest;
		break;
	}
	if(Debug & Dapicmask){
		print("masks: ");
		for(i = 0; i < n; i++)
			if(activeproc(a+i))
				print("%.2ux/%.2ux ", a[i].apicno, a[i].mask>>24);
		print("\n");
	}
}

static ulong
lapicr(int r)
{
	return *(lapicbase+(r/sizeof(*lapicbase)));
}

static void
lapicw(int r, ulong data)
{
	*(lapicbase+(r/sizeof(*lapicbase))) = data;
	data = *(lapicbase+(LapicID/sizeof(*lapicbase)));
	USED(data);
}

void
lapiconline(void)
{
	/*
	 * Reload the timer to de-synchronise the processors,
	 * then lower the task priority to allow interrupts to be
	 * accepted by the APIC.
	 */
	microdelay((TK2MS(1)*1000/conf.nmach) * m->machno);
	lapicw(LapicTICR, lapictimer.max);
	lapicw(LapicTIMER, LapicCLKIN|LapicPERIODIC|(VectorPIC+IrqTIMER));

	lapicw(LapicTPR, 0);
}

/*
 *  use the i8253 clock to figure out our lapic timer rate.
 */
static void
lapictimerinit(void)
{
	uvlong x, v, hz;

	v = m->cpuhz/1000;
	lapicw(LapicTDCR, LapicX1);
	lapicw(LapicTIMER, ApicIMASK|LapicCLKIN|LapicONESHOT|(VectorPIC+IrqTIMER));

	if(lapictimer.hz == 0ULL){
		x = fastticks(&hz);
		x += hz/10;
		lapicw(LapicTICR, 0xffffffff);
		do{
			v = fastticks(nil);
		}while(v < x);

		lapictimer.hz = (0xffffffffUL-lapicr(LapicTCCR))*10;
		lapictimer.max = lapictimer.hz/HZ;
		lapictimer.min = lapictimer.hz/(100*HZ);

		if(lapictimer.hz > hz-(hz/10)){
			if(lapictimer.hz > hz+(hz/10))
				panic("lapic clock %lld > cpu clock > %lld",
					lapictimer.hz, hz);
			lapictimer.hz = hz;
		}
		lapictimer.div = hz/lapictimer.hz;
	}
}

void
lapicinit(Apic* apic)
{
	ulong lvt;

	if(lapicbase == 0)
		lapicbase = apic->addr;

	if(mode == Flat)
		lapicw(LapicDFR, 0xFFFFFFFF);
	else
		lapicw(LapicDFR, 0x0FFFFFFF);
	apic->apicid0 = lapicr(LapicID);
	if(apic->apicid0 != apicphys(apic))
		panic("cpu%d: bad lapic id %ux != %ux",
			m->machno, apic->apicid0, apicphys(apic));
	lapicw(LapicLDR, apiclogical(apic));
	lapicw(LapicTPR, 0xFF);
	lapicw(LapicSVR, LapicENABLE|(VectorPIC+IrqSPURIOUS));

	lapictimerinit();

	/*
	 * Some Pentium revisions have a bug whereby spurious
	 * interrupts are generated in the through-local mode.
	 */
	switch(m->cpuidax & 0xFFF){
	case 0x526:				/* stepping cB1 */
	case 0x52B:				/* stepping E0 */
	case 0x52C:				/* stepping cC0 */
		wrmsr(0x0E, 1<<14);		/* TR12 */
		break;
	}

	/*
	 * Set the local interrupts. It's likely these should just be
	 * masked off for SMP mode as some Pentium Pros have problems if
	 * LINT[01] are set to ExtINT.
	 * Acknowledge any outstanding interrupts.
	lapicw(LapicLINT0, apic->lintr[0]);
	lapicw(LapicLINT1, apic->lintr[1]);
	 */
	lapiceoi(0);

	lvt = (lapicr(LapicVER)>>16) & 0xFF;
	if(lvt >= 4)
		lapicw(LapicPCINT, ApicIMASK);
	lapicw(LapicERROR, VectorPIC+IrqERROR);
	lapicw(LapicESR, 0);
	lapicr(LapicESR);

	/*
	 * Issue an INIT Level De-Assert to synchronise arbitration ID's.
	 */
	lapicw(LapicICRHI, 0);
	lapicw(LapicICRLO, LapicALLINC|ApicLEVEL|LapicDEASSERT|ApicINIT);
	while(lapicr(LapicICRLO) & ApicDELIVS)
		;

	/*
	 * Do not allow acceptance of interrupts until all initialisation
	 * for this processor is done. For the bootstrap processor this can be
	 * early during initialisation. For the application processors this should
	 * be after the bootstrap processor has lowered priority and is accepting
	 * interrupts.
	lapicw(LapicTPR, 0);
	 */
}

void
lapicstartap(Apic* apic, int v)
{
	int i;
	ulong crhi;

	crhi = apicphys(apic);
	lapicw(LapicICRHI, crhi);
	lapicw(LapicICRLO, LapicFIELD|ApicLEVEL|LapicASSERT|ApicINIT);
	microdelay(200);
	lapicw(LapicICRLO, LapicFIELD|ApicLEVEL|LapicDEASSERT|ApicINIT);
	delay(10);

	for(i = 0; i < 2; i++){
		lapicw(LapicICRHI, crhi);
		lapicw(LapicICRLO, LapicFIELD|ApicEDGE|ApicSTARTUP|ApicPHYSICAL|v/BY2PG);
		microdelay(200);
	}
}

void
lapicerror(Ureg*, void*)
{
	ulong esr;

	lapicw(LapicESR, 0);
	esr = lapicr(LapicESR);
	switch(m->cpuidax & 0xFFF){
	case 0x526:				/* stepping cB1 */
	case 0x52B:				/* stepping E0 */
	case 0x52C:				/* stepping cC0 */
		return;
	}
	print("cpu%d: lapicerror: %#.8lux\n", m->machno, esr);
}

void
lapicspurious(Ureg*, void*)
{
	print("cpu%d: lapicspurious\n", m->machno);
}

int
lapicisr(int v)
{
	ulong isr;

	isr = lapicr(LapicISR + (v/32));

	return isr & (1<<(v%32));
}

int
lapiceoi(int v)
{
	lapicw(LapicEOI, 0);

	return v;
}

void
lapicicrw(ulong hi, ulong lo)
{
	lapicw(LapicICRHI, hi);
	lapicw(LapicICRLO, lo);
}

void
ioapicrdtr(Apic* apic, int sel, int* hi, int* lo)
{
	ulong *iowin;

	iowin = apic->addr+(0x10/sizeof(ulong));
	sel = IoapicRDT + 2*sel;

	lock(apic);
	*apic->addr = sel+1;
	if(hi)
		*hi = *iowin;
	*apic->addr = sel;
	if(lo)
		*lo = *iowin;
	unlock(apic);
}

void
ioapicrdtw(Apic* apic, int sel, int hi, int lo)
{
	ulong *iowin;

	iowin = apic->addr+(0x10/sizeof(ulong));
	sel = IoapicRDT + 2*sel;

	lock(apic);
	*apic->addr = sel+1;
	*iowin = hi;
	*apic->addr = sel;
	*iowin = lo;
	unlock(apic);
}

void
ioapicmask(Apic* apic, int intin, int mask)
{
	int lo, hi;

	ioapicrdtr(apic, intin, &hi, &lo);
	lo &= ~ApicIMASK;
	if(mask)
		lo |= ApicIMASK;
	ioapicrdtw(apic, intin, hi, lo);
}

void
ioapicinit(Apic* apic, int apicno)
{
	int hi, lo, v;
	ulong *iowin;

	/*
	 * Initialise the I/O APIC.
	 * The MultiProcessor Specification says it is the responsibility
	 * of the O/S to set the APIC id.
	 * Make sure interrupts are all masked off for now.
	 */
	iowin = apic->addr+(0x10/sizeof(ulong));
	lock(apic);
	*apic->addr = IoapicVER;
	apic->mre = (*iowin>>16) & 0xFF;

	*apic->addr = IoapicID;
	*iowin = apicno<<24;
	unlock(apic);

	hi = 0;
	lo = ApicIMASK;
	for(v = 0; v <= apic->mre; v++)
		ioapicrdtw(apic, v, hi, lo);
}

void
lapictimerset(uvlong next)
{
	vlong period;
	int x;

	x = splhi();
	lock(&m->apictimerlock);

	period = lapictimer.max;
	if(next != 0){
		period = next - fastticks(nil);
		period /= lapictimer.div;

		if(period < lapictimer.min)
			period = lapictimer.min;
		else if(period > lapictimer.max - lapictimer.min)
			period = lapictimer.max;
	}
	lapicw(LapicTICR, period);

	unlock(&m->apictimerlock);
	splx(x);
}

void
lapicclock(Ureg *u, void*)
{
	timerintr(u, 0);
}

void
lapicintron(void)
{
	lapicw(LapicTPR, 0);
//	lapicw(LapicSVR, LapicENABLE|(VectorPIC+IrqSPURIOUS));
}

void
lapicintroff(void)
{
	lapicw(LapicTPR, 0xFF);
//	lapicw(LapicSVR, (VectorPIC+IrqSPURIOUS));
}

void
lapicnmienable(void)
{
	lapicw(LapicPCINT, ApicNMI);
}

void
lapicnmidisable(void)
{
	lapicw(LapicPCINT, ApicIMASK);
}
