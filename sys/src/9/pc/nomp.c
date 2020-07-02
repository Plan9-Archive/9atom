#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "mp.h"

static	_MP_	*mp;
static	void	*lapic;

static _MP_*
mpscan(uchar *addr, int len)
{
	uchar *e, *p, sum;
	int i;

	e = addr+len;
	for(p = addr; p < e; p += sizeof(_MP_)){
		if(memcmp(p, "_MP_", 4))
			continue;
		sum = 0;
		for(i = 0; i < sizeof(_MP_); i++)
			sum += p[i];
		if(sum == 0)
			return (_MP_*)p;
	}
	return 0;
}

static _MP_*
mpsearch(void)
{
	uchar *bda;
	ulong p;
	_MP_ *mp;

	/*
	 * Search for the MP Floating Pointer Structure:
	 * 1) in the first KB of the EBDA;
	 * 2) in the last KB of system base memory;
	 * 3) in the BIOS ROM between 0xE0000 and 0xFFFFF.
	 */
	bda = KADDR(0x400);
	if((p = (bda[0x0F]<<8)|bda[0x0E])){
		if(mp = mpscan(KADDR(p), 1024))
			return mp;
	}
	else{
		p = ((bda[0x14]<<8)|bda[0x13])*1024;
		if(mp = mpscan(KADDR(p-1024), 1024))
			return mp;
	}
	return mpscan(KADDR(0xF0000), 0x10000);
}

static char *atttab[] = {
	"proc",
	"bus",
	"ioapic",
	"iointr",
	"lintr",
};

static char *ittab[] = {
	"vec",
	"nmi",
	"smi",
	"ext",
};

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

char*
bustype(PCMPbus *p)
{
	int i;

	for(i = 0; buses[i]; i++){
		if(strncmp(buses[i], p->string, sizeof(p->string)) == 0)
			break;
	}
	return buses[i];
}

static char *mttab[] = {
	"i/o",
	"mem",
	"pref",
};

static char*
memtype(PCMPsasm *s)
{
	if(s->addrtype >= nelem(mttab))
		return "unk";
	return mttab[s->addrtype];
}

static char*
decode(PCMPhierarchy *h)
{
	if(h->info & 1)
		return "sub";
	return "---";
}

static char*
range(PCMPcbasm *c)
{
	switch(c->range){
	case 0:
		return "isa io";
	case 1:
		return "vga io";
	default:
		return "unk";
	}
}

#define Pr(...) snprint(buf, sizeof buf, __VA_ARGS__)

long
mptableread0(_MP_ *mp0, void *v, long n, vlong off)
{
	char *s, *sp, *t, buf[80];
	uchar *e, *p;
	int m, n0;
	PCMP *pcmp;
	PCMPbus *bus;
	PCMPcbasm *c;
	PCMPhierarchy *h;
	PCMPioapic *apic;
	PCMPintr *i;
	PCMPprocessor *a;
	PCMPsasm *sasm;

	if(mp != mp0)
		mp = mp0;
	if(mp == 0)
		return 0;
	s = v;
	n0 = n;

	m = Pr("mp\t%s\t%d\n", mp->imcrp>>7? "pic": "vwire", mp->type);
	if(m <= off)
		off -= m;
	else{
		m -= off;
		sp = buf + off;
		off = 0;
		if(m > n)
			m = n;
		memmove(s, sp, m);
		n -= m;
		s += m;
	}
	pcmp = KADDR(mp->physaddr);
	/*
	 * Run through the table saving information needed for starting
	 * application processors and initialising any I/O APICs. The table
	 * is guaranteed to be in order such that only one pass is necessary.
	 */
	p = (uchar*)pcmp + sizeof(PCMP);
	e = (uchar*)pcmp + pcmp->length + pcmp->xlength;
	while(p < e) switch(*p){
	default:
		m = Pr("mpinit: unknown PCMP type 0x%uX (e-p 0x%luX)\n",
			*p, e-p);
		p = e;
		goto dopr;
	case PcmpPROCESSOR:
		a = (PCMPprocessor*)p;
		if(!(a->flags & PcmpEN) || a->apicno > MaxAPICNO){
			m = Pr("badproc\t%d\t%.8ux\n",
				a->apicno, a->flags);
		}else{
			m = Pr("proc\t%.2ux\t%ux\t%p %p\n",
				a->apicno, a->flags, lapic, pcmp->lapicbase);
		}
		p += sizeof(PCMPprocessor);
		goto dopr;
	case PcmpBUS:
		bus = (PCMPbus*)p;
		m = Pr("bus\t%d %s\n", bus->busno, bustype(bus));
		p += sizeof(PCMPbus);
		goto dopr;
	case PcmpIOAPIC:
		apic = (PCMPioapic*)p;
		if(!(apic->flags & PcmpEN) || apic->apicno > MaxAPICNO)
			t = "badioa";
		else
			t = "ioapic";
		m = Pr("%s\t" "%d\t%p\t%.8ux\n", t, apic->apicno, apic->addr, apic->flags);
		p += sizeof(PCMPioapic);
		goto dopr;
	case PcmpIOINTR:
		i = (PCMPintr*)p;
		m = Pr("iointr\t%d:%d\t->\t%d:%d\n", i->busno, i->irq, i->apicno, i->intin);
		p += sizeof(PCMPintr);
		goto dopr;
	case PcmpLINTR:
		i = (PCMPintr*)p;
		m = Pr("lintr\t%d:%d\t->\t%d:%d\n", i->busno, i->irq, i->apicno, i->intin);
		p += sizeof(PCMPintr);
		goto dopr;

	case PcmpSASM:
		sasm = (PCMPsasm*)p;
		m = Pr("sasm\t%d\t%s\t%.16llux \t%.16llux\n", sasm->busno, memtype(sasm),
			sasm->addrbase, sasm->addrbase + sasm->addrlength);
		p += sizeof(PCMPsasm);
		goto dopr;
	case PcmpHIERARCHY:
		h = (PCMPhierarchy*)p;
		m = Pr("heir\t%d\t%d\t%s\n", h->busno, h->parent, decode(h));
		p += sizeof(PCMPhierarchy);
		goto dopr;
	case PcmpCBASM:
		c = (PCMPcbasm*)p;
		m = Pr("cb\t%d\t%s\t%s\n", c->busno, c->modifier? "-": "+", range(c));
		p += sizeof(PCMPcbasm);
		goto dopr;

	dopr:
		if(m <= off)
			off -= m;
		else{
			m -= off;
			sp = buf + off;
			off = 0;
			if(m > n)
				m = n;
			memmove(s, sp, m);
			n -= m;
			s += m;
		}
		continue;
	}
	return n0 - n;
}

static long
mptableread(Chan*, void *v, long n, vlong off)
{
	if(mp == 0)
		return 0;
	return mptableread0(mp, v, n, off);
}

void
nomplink(void)
{
	char *s;
	uchar *p, sum;
	ulong length;
	PCMP *pcmp;

	if((s = getconf("*nomp")) == nil || strtol(s, 0, 0) == 0)
		return;
	mp = mpsearch();
	if(mp == nil){
		print("nomp: no mp structure\n");
		return;
	}
	pcmp = KADDR(mp->physaddr);
	if(memcmp(pcmp, "PCMP", 4))
		return;
	length = pcmp->length;
	sum = 0;
	for(p = (uchar*)pcmp; length; length--)
		sum += *p++;
	if(sum || (pcmp->version != 1 && pcmp->version != 4))
		return;
	if((lapic = vmap(pcmp->lapicbase, 1024)) == nil)
		return;
	print("nomp LAPIC: %p %p %d %d\n", pcmp->lapicbase, lapic, pcmp->length, pcmp->xlength);
	addarchfile("mptable", 0444, mptableread, nil);
}
