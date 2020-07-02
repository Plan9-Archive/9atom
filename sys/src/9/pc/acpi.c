#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#define dprint(...)	{if(debug) print(__VA_ARGS__); }

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

/*
 * A System Descriptor Table starts with a header of 4 bytes of signature
 * followed by 4 bytes of total table length then 28 bytes of ID information
 * (including the table checksum).
 * Only the signature and length are of interest. A byte array is used
 * rather than an embedded structure to avoid structure alignment
 * problems.
 */
typedef struct Dsdt Dsdt;
typedef struct Facp Facp;

struct Dsdt {				/* Differentiated System DT */
	uchar	sdthdr[36];		/* "DSDT" + length[4] + [28] */
	uchar	db[];			/* Definition Block */
};

struct Facp {				/* Fixed ACPI DT */
	uchar	sdthdr[36];		/* "FACP" + length[4] + [28] */
	uchar	faddr[4];			/* Firmware Control Address */
	uchar	dsdt[4];			/* DSDT Address */
	uchar	pad[200];		/* total table is 244 */
};

static void
acpifacp(void *v)
{
	uchar *p, *faddr, *daddr;
	int i, l;
	Facp *facp;

	facp = v;
	faddr = (uchar*)get32(facp->faddr);
	daddr = (uchar*)get32(facp->dsdt);
	dprint("	faddr %#p dsdt %p\n", faddr, daddr);
	if((p = vmap((ulong)daddr, 8)) == nil)
		return;
	if(memcmp(p, "DSDT", 4) != 0){
		vunmap(p, 8);
		return;
	}
	l = get32(p+4);
	vunmap(p, 8);
	if((p = vmap((ulong)daddr, l)) == nil)
		return;
	for(i = 0; i < 32; i++)
		dprint(" %.2ux", p[36+i]);
	dprint("\n");
	vunmap(p, l);
}

typedef struct	Ftab	Ftab;
struct Ftab {
	char	*name;
	int	mask;
	int	bits;
};
Ftab procflags[] = {
	"e",	1<<0,	1<<0,
	"b",	1<<1,	1<<1,
};
Ftab iniflags[] = {
	"b",	3,	0,
	"h",	3,	1,
	"l",	3,	3,
	"b",	3<<2,	0<<2,
	"e",	3<<2,	1<<2,
	"l",	3<<2,	3<<2,
};

void
flagfmt(char *p, char *e, Ftab *t, int n, int f)
{
	int i;

	for(i = 0; i < n; i++)
		if((f & t[i].mask) == t[i].bits){
			p = seprint(p, e, "%s", t[i].name);
			f &= ~t[i].bits;
		}
	if(f)
		p = seprint(p, e, " %.8ux", f);
	*p = 0;
}

typedef struct {				/* Multiple APIC DT */
	uchar	sdthdr[36];		/* "MADT" + length[4] + [28] */
	uchar	addr[4];			/* Local APIC Address */
	uchar	flags[4];
	uchar	structures[];
} Madt;

static void
acpiapic(void *v)
{
	char buf[32];
	uchar *p;
	int i, l, n, f, np;
	Madt *madt;

	madt = v;
	dprint("APIC lapic addr %#.8ux, flags %#8.8ux\n",
		get32(madt->addr), get32(madt->flags));

	np = 0;
	n = get32(&madt->sdthdr[4]);
	p = madt->structures;
	for(i = offsetof(Madt, structures[0]); i < n; i += l){
		switch(p[0]){
		case 0:				/* processor lapic */
			f = get32(p+4);
			if(np++ == 0)
				f |= 2;		/* bsp */
			flagfmt(buf, buf+sizeof buf, procflags, nelem(procflags), f);
			dprint("	proc %d apicid %d flags %s\n", p[2], p[3], buf);
			break;
		case 1:				/* i/o apic */
			dprint("	ioapic %d ", p[2]);
			dprint("addr %.8ux base %.8ux\n", get32(p+4), get32(p+8));
			break;
		case 2:				/* interrupt source override */
			dprint("	irq %d ", p[3]);
			dprint("global sys interrupt %#8.8ux ", get32(p+4));
			dprint("flags %#.4ux\n", get16(p+8));
			break;
		case 3:				/* nmi source */
			dprint("	nmi\n");
			break;
		case 4:				/* lapic nmi */
			f = get16(p+3);
			flagfmt(buf, buf+sizeof buf, iniflags, nelem(iniflags), f);
			dprint("	local nmi apic %d %s lint %d\n", p[2], buf, p[5]);
			break;
		case 5:				/* Local APIC Address Override */
			dprint("	local nmi addr override\n");
			break;
		case 6:				/* i/o sapic */
			dprint("	iosapic\n");
			break;
		case 7:				/* lsapic */
			dprint("	lsapic\n");
			break;
		case 8:				/* platform interrupt sources */
			dprint("	platform interrupt source\n");
			break;
		case 9:
			dprint("	lx2apic\n");
			break;
		case 10:	
			dprint("	lx2apic nmi\n");
			break;
		default:
			dprint("	type %d, length %d\n", p[0], p[1]);
			break;
		}
		l = p[1];
		p += l;
	}
}

typedef struct {				/* MCFG Descriptor */
	uchar	addr[8];			/* base address */
	uchar	segno[2];		/* segment group number */
	uchar	sbno;			/* start bus number */
	uchar	ebno;			/* end bus number */
	uchar	pad[4];			/* reserved */
} Mcfgd;

typedef struct {				/* PCI Memory Mapped Config */
	uchar	sdthdr[36];		/* "MCFG" + length[4] + [28] */
	uchar	pad[8];			/* reserved */
	Mcfgd	mcfgd[];			/* descriptors */
} Mcfg;

static void
acpimcfg(void* v)
{
	int i, n;
	Mcfg *mcfg;
	Mcfgd *mcfgd;

	mcfg = v;
	n = get32(&mcfg->sdthdr[4]);
	mcfgd = mcfg->mcfgd;
	for(i = offsetof(Mcfg, mcfgd[0]); i < n; i += sizeof *mcfgd){
		dprint("	addr %#.16llux segno %d sbno %d ebno %d\n",
			get64(mcfgd->addr), get16(mcfgd->segno),
			mcfgd->sbno, mcfgd->ebno);
		mcfgd++;
	}
}

typedef struct {				/* HPET DT */
	uchar	sdthdr[36];			/* "FACP" + length[4] + [28] */
	uchar	id[4];				/* Event Timer Bock ID */
	uchar	addr[12];			/* ACPI Format Address */
	uchar	seqno;				/* Sequence Number */
	uchar	minticks[2];			/* Minimum Clock Tick */
	uchar	attr;				/* Page Protection */
} Hpet;

static void
acpihpet(void *v)
{
	Hpet *hpet;

	hpet = v;
	dprint("\tid %#ux addr %d %d %d %#llux seqno %d minticks %d attr %#ux\n",
		get32(hpet->id), hpet->addr[0], hpet->addr[1], hpet->addr[2],
		get64(&hpet->addr[4]), hpet->seqno,
		get16(hpet->minticks), hpet->attr);
}

typedef struct {				/* Root System Description * */
	uchar	signature[8];		/* "RSD PTR " */
	uchar	rchecksum;
	uchar	oemid[6];
	uchar	revision;
	uchar	raddr[4];			/* RSDT */
	uchar	length[4];
	uchar	xaddr[8];			/* XSDT */
	uchar	xchecksum;		/* XSDT */
	uchar	pad[3];			/* reserved */
} Rsd;

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

static void
rsdlink(void)
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
			snprint(buf, sizeof buf, "%.4s", (char*)p);
			dprint("%s\n", buf);
			if(memcmp(p, "FACP", 4) == 0)
				acpifacp(p);
			else if(memcmp(p, "APIC", 4) == 0)
				acpiapic(p);
			else if(memcmp(p, "MCFG", 4) == 0)
				acpimcfg(p);
			else if(memcmp(p, "HPET", 4) == 0)
				acpihpet(p);
			else
				dprint("	??\n");
		}
		vunmap(p, l);
	}
	vunmap(sdt, n);
}

void
acpilink(void)
{
	print("ACPI\n");
	rsdlink();
}
