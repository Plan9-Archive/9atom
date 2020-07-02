#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

#include "apic.h"
#include "io.h"
#include "adr.h"

enum {						/* Local APIC registers */
	Id		= 0x0020,		/* Identification */
	Ver		= 0x0030,		/* Version */
	Tp		= 0x0080,		/* Task Priority */
	Ap		= 0x0090,		/* Arbitration Priority */
	Pp		= 0x00a0,		/* Processor Priority */
	Eoi		= 0x00b0,		/* EOI */
	Ld		= 0x00d0,		/* Logical Destination */
	Df		= 0x00e0,		/* Destination Format */
	Siv		= 0x00f0,		/* Spurious Interrupt Vector */
	Is		= 0x0100,		/* Interrupt Status (8) */
	Tm		= 0x0180,		/* Trigger Mode (8) */
	Ir		= 0x0200,		/* Interrupt Request (8) */
	Es		= 0x0280,		/* Error Status */
	Iclo		= 0x0300,		/* Interrupt Command */
	Ichi		= 0x0310,		/* Interrupt Command [63:32] */
	Lvt0		= 0x0320,		/* Local Vector Table 0 */
	Lvt5		= 0x0330,		/* Local Vector Table 5 */
	Lvt4		= 0x0340,		/* Local Vector Table 4 */
	Lvt1		= 0x0350,		/* Local Vector Table 1 */
	Lvt2		= 0x0360,		/* Local Vector Table 2 */
	Lvt3		= 0x0370,		/* Local Vector Table 3 */
	Tic		= 0x0380,		/* Timer Initial Count */
	Tcc		= 0x0390,		/* Timer Current Count */
	Tdc		= 0x03e0,		/* Timer Divide Configuration */

	Tlvt		= Lvt0,			/* Timer */
	Lint0		= Lvt1,			/* Local Interrupt 0 */
	Lint1		= Lvt2,			/* Local Interrupt 1 */
	Elvt		= Lvt3,			/* Error */
	Pclvt		= Lvt4,			/* Performance Counter */
	Tslvt		= Lvt5,			/* Thermal Sensor */
};

enum {						/* Siv */
	Swen		= 0x00000100,		/* Software Enable */
	Fdis		= 0x00000200,		/* Focus Disable */
};

enum {						/* Iclo */
	Lassert		= 0x00004000,		/* Assert level */

	DSnone		= 0x00000000,		/* Use Destination Field */
	DSself		= 0x00040000,		/* Self is only destination */
	DSallinc	= 0x00080000,		/* All including self */
	DSallexc	= 0x000c0000,		/* All Excluding self */
};

enum {						/* Tlvt */
	Periodic	= 0x00020000,		/* Periodic Timer Mode */
};

enum {						/* Tdc */
	DivX2		= 0x00000000,		/* Divide by 2 */
	DivX4		= 0x00000001,		/* Divide by 4 */
	DivX8		= 0x00000002,		/* Divide by 8 */
	DivX16		= 0x00000003,		/* Divide by 16 */
	DivX32		= 0x00000008,		/* Divide by 32 */
	DivX64		= 0x00000009,		/* Divide by 64 */
	DivX128		= 0x0000000a,		/* Divide by 128 */
	DivX1		= 0x0000000b,		/* Divide by 1 */
};

static u32int* lapicbase;
static int lapmachno = 1;

Apic	xlapic[Napic];

static u32int
lapicrget(int r)
{
	return lapicbase[r/4];
}

static void
lapicrput(int r, u32int data)
{
	lapicbase[r/4] = data;
}

int
lapiceoi(int vecno)
{
	lapicrput(Eoi, 0);
	return vecno;
}

int
lapicisr(int vecno)
{
	int isr;

	isr = lapicrget(Is + (vecno/32)*16);

	return isr & (1<<(vecno%32));
}

static char*
lapicprint(char *p, char *e, Lapic *a, int i)
{
	char *s;

	s = "proc";
	p = seprint(p, e, "%-8s ", s);
	p = seprint(p, e, "%8ux ", i);
//	p = seprint(p, e, "%.8ux ", a->dest);
//	p = seprint(p, e, "%.8ux ", a->mask);
//	p = seprint(p, e, "%c", a->flags & PcmpBP? 'b': ' ');
//	p = seprint(p, e, "%c ", a->flags & PcmpEN? 'e': ' ');
//	p = seprint(p, e, "%8ux %8ux", a->lintr[0], a->lintr[1]);
	p = seprint(p, e, "%12d\n", a->machno);
	return p;
}

static long
lapicread(Chan*, void *a, long n, vlong off)
{
	char *s, *e, *p;
	long i, r;

	s = malloc(READSTR);
	e = s+READSTR;
	p = s;

	for(i = 0; i < nelem(xlapic); i++)
		if(xlapic[i].useable)
			p = lapicprint(p, e, xlapic + i, i);
	r = -1;
	if(!waserror()){
		r = readstr(off, a, n, s);
		poperror();
	}
	free(s);
	return r;
}

void
lapicinit(int lapicno, uintmem pa, int isbp)
{
	Apic *apic;

	/*
	 * Mark the LAPIC useable if it has a good ID
	 * and the registers can be mapped.
	 * The LAPIC Extended Broadcast and ID bits in the HyperTransport
	 * Transaction Control register determine whether 4 or 8 bits
	 * are used for the LAPIC ID. There is also xLAPIC and x2LAPIC
	 * to be dealt with sometime.
	 */
	DBG("lapicinit: lapicno %d pa %#P isbp %d caller %#p\n", lapicno, pa, isbp, getcallerpc(&lapicno));
	addarchfile("lapic", 0444, lapicread, nil);

	if(lapicno >= Napic){
		print("lapicinit%d: out of range\n", lapicno);
		return;
	}
	if((apic = &xlapic[lapicno])->useable){
//		print("lapicinit%d: already initialised\n", lapicno);
		return;
	}
	if(lapicbase == nil){
		adrmapck(pa, 1024, Aapic, Mfree);
		if((lapicbase = vmap(pa, 1024)) == nil){
			print("lapicinit%d: can't map lapicbase %#P\n", lapicno, pa);
			return;
		}
		DBG("lapicinit%d: lapicbase %#P -> %#p\n", lapicno, pa, lapicbase);
	}
	apic->useable = 1;

	/*
	 * Assign a machno to the processor associated with this
	 * LAPIC, it may not be an identity map.
	 * Machno 0 is always the bootstrap processor.
	 */
	if(isbp){
		apic->machno = 0;
		m->apicno = lapicno;
	}
	else
		apic->machno = lapmachno++;
}

static void
lapicdump0(Apic *apic, int i)
{
	if(!apic->useable || apic->addr != 0)
		return;
	DBG("lapic%d: machno %d lint0 %#8.8ux lint1 %#8.8ux\n",
		i, apic->machno, apic->lvt[0], apic->lvt[1]);
	DBG(" tslvt %#8.8ux pclvt %#8.8ux elvt %#8.8ux\n",
		lapicrget(Tslvt), lapicrget(Pclvt), lapicrget(Elvt));
	DBG(" tlvt %#8.8ux lint0 %#8.8ux lint1 %#8.8ux siv %#8.8ux\n",
		lapicrget(Tlvt), lapicrget(Lint0),
		lapicrget(Lint1), lapicrget(Siv));
}

void
lapicdump(void)
{
	int i;

	if(!DBGFLG)
		return;

	DBG("lapicbase %#p lapmachno %d\n", lapicbase, lapmachno);
	for(i = 0; i < Napic; i++)
		lapicdump0(xlapic + i, i);
}

static void
apictimer(Ureg* ureg, void*)
{
	timerintr(ureg, 0);
}

int
lapiconline(void)
{
	Apic *apic;
	u64int tsc;
	u32int dfr, ver;
	int apicno, nlvt;

	if(lapicbase == nil)
		panic("lapiconline: no lapic base");

	if((apicno = ((lapicrget(Id)>>24) & 0xff)) >= Napic)
		return 0;
	apic = &xlapic[apicno];
	if(!apic->useable || apic->addr != nil)
		return 0;

	/*
	 * Things that can only be done when on the processor
	 * owning the APIC, apicinit above runs on the bootstrap
	 * processor.
	 */
	ver = lapicrget(Ver);
	nlvt = ((ver>>16) & 0xff) + 1;
	if(nlvt > nelem(apic->lvt)){
		print("lapiconline%d: nlvt %d > max (%d)\n",
			apicno, nlvt, nelem(apic->lvt));
		nlvt = nelem(apic->lvt);
	}
	apic->nlvt = nlvt;
	apic->ver = ver & 0xff;

	/*
	 * These don't really matter in Physical mode;
	 * set the defaults anyway.
	 */
//	if(memcmp(m->cpuinfo, "AuthenticAMD", 12) == 0)
//		dfr = 0xf0000000;
//	else
		dfr = 0xffffffff;
	lapicrput(Df, dfr);
	lapicrput(Ld, 0x00000000);

	/*
	 * Disable interrupts until ready by setting the Task Priority
	 * register to 0xff.
	 */
	lapicrput(Tp, 0xff);

	/*
	 * Software-enable the APIC in the Spurious Interrupt Vector
	 * register and set the vector number. The vector number must have
	 * bits 3-0 0x0f unless the Extended Spurious Vector Enable bit
	 * is set in the HyperTransport Transaction Control register.
	 */
	lapicrput(Siv, Swen|IdtSPURIOUS);

	/*
	 * Acknowledge any outstanding interrupts.
	 */
	lapicrput(Eoi, 0);

	/*
	 * Use the TSC to determine the lapic timer frequency.
	 * It might be possible to snarf this from a chipset
	 * register instead.
	 */
	lapicrput(Tdc, DivX1);
	lapicrput(Tlvt, Im);
	tsc = rdtsc() + m->cpuhz/10;
	lapicrput(Tic, 0xffffffff);

	while(rdtsc() < tsc)
		;

	apic->hz = (0xffffffff-lapicrget(Tcc))*10;
	apic->max = apic->hz/HZ;
	apic->min = apic->hz/(100*HZ);
	apic->div = ((m->cpuhz/apic->max)+HZ/2)/HZ;

	if(m->machno == 0 || DBGFLG){
		print("lapic%d: hz %lld max %lld min %lld div %lld\n", apicno,
			apic->hz, apic->max, apic->min, apic->div);
	}

	/*
	 * Mask interrupts on Performance Counter overflow and
	 * Thermal Sensor if implemented, and on Lintr0 (Legacy INTR),
	 * and Lintr1 (Legacy NMI).
	 * Clear any Error Status (write followed by read) and enable
	 * the Error interrupt.
	 */
	switch(apic->nlvt){
	case 7:
	case 6:
		lapicrput(Tslvt, Im);
		/*FALLTHROUGH*/
	case 5:
		lapicrput(Pclvt, Im);
		/*FALLTHROUGH*/
	default:
		break;
	}
	lapicrput(Lint1, apic->lvt[1]|Im|IdtLINT1);
	lapicrput(Lint0, apic->lvt[0]|Im|IdtLINT0);

	lapicrput(Es, 0);
	lapicrget(Es);
	lapicrput(Elvt, IdtERROR);

	/*
	 * Reload the timer to de-synchronise the processors,
	 * then lower the task priority to allow interrupts to be
	 * accepted by the APIC.
	 */
	microdelay((TK2MS(1)*1000/lapmachno) * m->machno);
	lapicrput(Tic, apic->max);

	if(apic->machno == 0)
		intrenable(IdtTIMER, apictimer, 0, -1, "APIC timer");
	lapicrput(Tlvt, Periodic|IrqTIMER);
	if(m->machno == 0)
		lapicrput(Tp, 0);
	return 1;
}

void
lapictimerset(uvlong next)
{
	Mpl pl;
	Apic *apic;
	vlong period;

	apic = &xlapic[(lapicrget(Id)>>24) & 0xff];

	pl = splhi();
	lock(&m->apictimerlock);

	period = apic->max;
	if(next != 0){
		period = next - fastticks(nil);	/* fastticks is just rdtsc() */
		period /= apic->div;

		if(period < apic->min)
			period = apic->min;
		else if(period > apic->max - apic->min)
			period = apic->max;
	}
	lapicrput(Tic, period);

	unlock(&m->apictimerlock);
	splx(pl);
}

void
lapicsipi(int lapicno, uintmem pa)
{
	int i;
	u32int crhi, crlo;

	/*
	 * SIPI - Start-up IPI.
	 * To do: checks on lapic validity.
	 */
	crhi = lapicno<<24;
	lapicrput(Ichi, crhi);
	lapicrput(Iclo, DSnone|TMlevel|Lassert|MTir);
	microdelay(200);
	lapicrput(Iclo, DSnone|TMlevel|MTir);
	delay(10);

	crlo = DSnone|TMedge|MTsipi|((u32int)pa/(4*KiB));
	for(i = 0; i < 2; i++){
		lapicrput(Ichi, crhi);
		lapicrput(Iclo, crlo);
		microdelay(200);
	}
}

void
lapicipi(int lapicno)
{
	lapicrput(Ichi, lapicno<<24);
	lapicrput(Iclo, DSnone|TMedge|Lassert|MTf|IdtIPI);
	while(lapicrget(Iclo) & Ds)
		;
}

void
lapicpri(int pri)
{
	lapicrput(Tp, pri);
}
