#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "mp.h"

#define dprint(...)	{if(debug) print(__VA_ARGS__); }

typedef	struct	Madt	Madt;
typedef	struct	Rsd	Rsd;

struct Madt {				/* Multiple APIC DT */
	uchar	sdthdr[36];		/* "MADT" + length[4] + [28] */
	uchar	addr[4];			/* Local APIC Address */
	uchar	flags[4];
	uchar	structures[];
};

struct Rsd {				/* Root System Description */
	uchar	signature[8];		/* "RSD PTR " */
	uchar	rchecksum;
	uchar	oemid[6];
	uchar	revision;
	uchar	raddr[4];			/* RSDT */
	uchar	length[4];
	uchar	xaddr[8];			/* XSDT */
	uchar	xchecksum;		/* XSDT */
	uchar	pad[3];			/* reserved */
};

static int debug = 1;

static uchar*
biosseg(uintptr a)
{
	return KADDR(a<<4);
}

static ushort
get16(uchar *p)
{
	return p[1]<<8 | p[0];
}

static uint
get32(uchar *p)
{
	return p[3]<<24 | p[2]<<16 | p[1]<<8 | p[0];
}

static uvlong
get64(uchar *p)
{
	uvlong u;

	u = get32(p+4);
	return u<<32 | get32(p);
}

extern	int	mpmachno;

static void
acpiapic(void *v)
{
	char *already;
	uchar *p;
	int i, l, n, f, np;
	u32int lapicaddr;
	Apic *apic;
	Madt *madt;

	madt = v;
	lapicaddr = get32(madt->addr);
	dprint("APIC lapic addr %#.8ux, flags %#8.8ux\n",
		lapicaddr, get32(madt->flags));

	n = get32(&madt->sdthdr[4]);
	p = madt->structures;
	np = 0;
	for(i = offsetof(Madt, structures[0]); i < n; i += l){
		already = "";
		switch(p[0]){
		case 0:				/* processor lapic */
			if((get32(p+4) & 1) == 0 || p[3] > MaxAPICNO)
				break;
			apic = mplapic + p[3];
			f = (np++ == 0)*PcmpBP | PcmpEN;
			if(apic->used){
				already = "(mp)";
				goto pr;
			}
			apic->used = 1;
			apic->type = PcmpPROCESSOR;
			apic->paddr = lapicaddr;
			apic->apicno = p[3];
			apic->flags = f;
			apic->lintr[0] = ApicIMASK;
			apic->lintr[1] = ApicIMASK;
			/* botch! just enumerate */
			if(apic->flags & PcmpBP)
				apic->machno = 0;
			else
				apic->machno = ++mpmachno;
			procvec[mpmachno] = apic;
		pr:
			dprint("apic proc %d/%d apicid %d %s\n", np-1, apic->machno, p[3], already);
			break;
		case 1:				/* i/o apic */
			if(p[2] > MaxAPICNO)
				break;
			apic = mpioapic + p[2];
			apic->gsibase = get32(p+8);		/* of global system interrupts */
			if(apic->used){
				already = "(mp)";
				goto pr1;
			}
			apic->paddr = get32(p+4);
			if((apic->addr = vmap(apic->paddr, 1024)) == nil){
				print("apic %d: failed to map %#P\n", p[3], apic->paddr);
				already = "(fail)";
				goto pr1;
			}
			apic->used = 1;
			apic->type = PcmpIOAPIC;
			apic->apicno = p[2];
			apic->flags = PcmpEN;
		pr1:
			dprint("ioapic %d ", p[2]);
			dprint("addr %#P base %d %s\n", apic->paddr, apic->gsibase, already);
			break;
		}
		l = p[1];
		p += l;
	}
}

static void*
rsdchecksum(void* addr, int length)
{
	uchar *p, sum;

	sum = 0;
	for(p = addr; length-- > 0; p++)
		sum += *p;
	if(sum == 0)
		return addr;

	return nil;
}

static void*
rsdscan(uchar* addr, int len, char* signature)
{
	int sl;
	uchar *e, *p;

	e = addr+len;
	sl = strlen(signature);
	for(p = addr; p+sl < e; p += 16){
		if(memcmp(p, signature, sl))
			continue;
		return p;
	}

	return nil;
}

static void*
rsdsearch(char* signature)
{
	uintptr p;
	uchar *bda;
	Rsd *rsd;

	/*
	 * Search for the data structure signature:
	 * 1) in the first KB of the EBDA;
	 * 2) in the BIOS ROM between 0xE0000 and 0xFFFFF.
	 */
	if(strncmp((char*)KADDR(0xFFFD9), "EISA", 4) == 0){
		bda = biosseg(0x40);
		if((p = (bda[0x0F]<<8)|bda[0x0E])){
			if(rsd = rsdscan(KADDR(p), 1024, signature))
				return rsd;
		}
	}
	return rsdscan(biosseg(0xE000), 0x20000, signature);
}

void
mpacpi(void)
{
	char buf[32];
	uchar *p, *sdt;
	int asize, i, l, n;
	uintptr dhpa, sdtpa;
	Rsd *rsd;

	if((rsd = rsdsearch("RSD PTR ")) == nil)
		return;
	snprint(buf, sizeof buf, "%.6s", (char*)rsd->oemid);
	dprint("rsd %#p, physaddr %#ux length %ud %#llux rev %d oem %s\n",
		rsd, get32(rsd->raddr), get32(rsd->length),
		get64(rsd->xaddr), rsd->revision, buf);

	if(rsd->revision == 2){
		if(rsdchecksum(rsd, 36) == nil)
			return;
		sdtpa = get64(rsd->xaddr);
		asize = 8;
	}
	else{
		if(rsdchecksum(rsd, 20) == nil)
			return;
		sdtpa = get32(rsd->raddr);
		asize = 4;
	}
	if((sdt = vmap(sdtpa, 8)) == nil)
		return;
	if((sdt[0] != 'R' && sdt[0] != 'X') || memcmp(sdt+1, "SDT", 3) != 0){
		vunmap(sdt, 8);
		return;
	}

	n = get32(sdt+4);
	vunmap(sdt, 8);
	if((sdt = vmap(sdtpa, n)) == nil)
		return;
	if(rsdchecksum(sdt, n) == nil){
		vunmap(sdt, n);
		return;
	}
	for(i = 36; i < n; i += asize){
		if(asize == 8)
			dhpa = get64(sdt+i);
		else
			dhpa = get32(sdt+i);
		if((p = vmap(dhpa, 8)) == nil)
			continue;
		l = get32(p+4);
		vunmap(p, 8);
		if((p = vmap(dhpa, l)) == nil)
			continue;
		if(rsdchecksum(p, l) != nil){
			if(memcmp(p, "APIC", 4) == 0)
				acpiapic(p);
		}
		vunmap(p, l);
	}
	vunmap(sdt, n);
}
