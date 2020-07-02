#include <u.h>
#include <libc.h>
#include <bio.h>

#include "dat.h"

uchar	flag[0x7f];
Biobuf	o;

typedef void (Cdump)(Pcidev *p, int);

static char *typetab[] = {
[0]	"endpoint",
[1]	"legacy endpoint",
[4]	"root port",
[5]	"up port",
[6]	"down port",
[7]	"pcie->pcix bridge",
[8]	"pcix->pcie bridge",
};

void
linkdump(Pcidev *p, int toff, char *s)
{
	uint cap, lanes, gen;

	cap = pcicfgr16(p, toff);
	lanes = cap>>4 & 0x3f;
	gen = cap&7;
	Bprint(&o, "%s: %d lanes gen %d\n", s, lanes, gen);
}

void
pciedump(Pcidev *p, int off)
{
	char *s, *si;
	uint cap, i, j;

	cap = pcicfgr16(p, off + PciePCP);
	i = cap>>4 & 0xf;
	s = "unknown device type";
	if(i < nelem(typetab) && typetab[i] != nil)
		s = typetab[i];
	si = "";
	if(i == 4 || i == 6)
		si = cap&1<<8? "; slot": "; integrated";
	Bprint(&o, "	pcie type: %s%s; msgs %d\n", s, si, cap>>9 & 0x3f);

	cap = pcicfgr32(p, off + PcieCAP);
	i = cap & 7;
	for(j = 128; i != 0; i--)
		j *= 2;
	Bprint(&o, "	max payload: %d bytes; pp %d\n", j, i>>3 & 3);

	linkdump(p, off + PcieLCA, "	link cap");
	linkdump(p, off + PcieLSR, "	link stat");

	cap = pcicfgr32(p, off + PcieSCA);
	i = pcicfgr16(p, off + PcieSSR);
	s = "not present";
	if(i & 1<<6)
		s = "present";
	j = pcicfgr32(p, off + PcieLCA);
	Bprint(&o, "	slot number: %d; port %d; %s\n", cap>>19, j>>24, s);
}

typedef struct Captab Captab;
struct Captab {
	int	id;
	char	*name;
	Cdump	*dump;
};

Captab ctab[] = {
1,	"pwrmgmt",	nil,
2,	"agp",		nil,
3,	"vpd",		nil,
4,	"slotid",		nil,
5,	"msi",		nil,
6,	"hotswap",	nil,
7,	"pcix",		nil,
8,	"ldt",		nil,
9,	"vendor",	nil,
0x0a,	"dbgprt",		nil,
0x0b,	"resrcctl",	nil,
0x0c,	"hotplug",	nil,
0x0d,	"bridgesvid",	nil,
0x0e,	"agp8",		nil,
0x0f,	"secure",		nil,
0x10,	"pcie",		pciedump,
0x11,	"msi-x",		nil,
0x12,	"satad/i",		nil,
0x13,	"advfeat",		nil,
};

/* extended capabilities; currently unused */
Captab ectab[] = {
0,	"null",		nil,
1,	"aer",		nil,
2,	"vc",		nil,
3,	"serial#",		nil,
4,	"pwrbgt",		nil,
5,	"rclink",		nil,
6,	"rclinkctl",		nil,
7,	"rcevent",		nil,
8,	"mfvc",		nil,
9,	"vc",		nil,
0x0a,	"rcrb",		nil,
0x0b,	"vsec",		nil,
0x0c,	"cac",		nil,
0x0d,	"acs",		nil,
0x0e,	"ari",		nil,
0x0f,	"ats",		nil,
0x10,	"sr-iov",		nil,
0x11,	"mr-iov",		nil,
0x12,	"multicast",	nil,
0x13,	"pagerq",		nil,
0x14,	"amd",		nil,
0x15,	"resizablebar",	nil,
0x16,	"dpa",		nil,
0x17,	"tph",		nil,
0x18,	"ltr",		nil,
0x19,	"secondarycap",	nil,
};

static Captab*
looktab(uint c)
{
	int i;

	for(i = 0; i < nelem(ctab); i++)
		if(ctab[i].id == c)
			return ctab + i;
	return nil;
}

void
prcap(Pcidev *p, int dump)
{
	int off, c;
	Captab *t;

	Bprint(&o, "%#T: ", p->tbdf);
	for(off = 0; (off = pcicap(p, off, -1)) != -1;){
		c = pcicfgr8(p, off);
		t = looktab(c);
		if(t != nil)
			Bprint(&o, "%s/%.2ux ", t->name, c);
		else
			Bprint(&o, "%.2ux ", c);
	}
	Bprint(&o, "\n");

	if(dump)
	for(off = 0; (off = pcicap(p, off, -1)) != -1;){
		c = pcicfgr8(p, off);
		t = looktab(c);
		if(t && t->dump)
			t->dump(p, off);
	}
}

typedef struct Etype Etype;
struct Etype {
	ushort	ccrb;
	ushort	ccru;
	ushort	ccrp;
	char	*name;
};
enum {
	Dontcare	= 0x100,
	Class	= 0x200,
};

Etype etypetab[] = {
1,	Class,	Class,	"disk",
1,	0,	0,	"scsi",
1,	1,	Dontcare,	"ide",
1,	2,	0,	"floppy",
1,	3,	0,	"ipi",
1,	4,	0,	"raid",
1,	5,	0x20,	"adma stepped",
1,	5,	0x30,	"adma continuous",
1,	6,	0,	"sata vendor specific",
1,	6,	1,	"sata ahci",
1,	6,	2,	"sata ssb",
1,	7,	0,	"sas",
1,	7,	1,	"ssb",
1,	8,	0,	"flash vendor specific",
1,	8,	1,	"flash nvmhci",
1,	8,	2,	"flash nvmhci enterprise",

2,	Class,	Class,	"network",
2,	0,	0,	"ethernet",
2,	1,	0,	"tokenring",
2,	2,	0,	"fddi",
2,	3,	0,	"atm",
2,	4,	0,	"isdn",
2,	5,	0,	"worldfip",
2,	6,	Dontcare,	"picmg",

3,	Class,	Class,	"display",
3,	0,	0,	"vga",
3,	0,	1,	"8514",
3,	1,	0,	"xga",
3,	2,	0,	"3d",
3,	3,	0x80,	"other",

4,	Class,	Class,	"media",
4,	0,	0,	"video",
4,	1,	0,	"audio",
4,	2,	0,	"computer telephony",
4,	3,	0,	"mixed mode",

5,	Class,	Class,	"memory",
5,	0,	0,	"ram",
5,	1,	0,	"flash",

6,	Class,	Class,	"bridge",
6,	0,	0,	"host",
6,	1,	0,	"isa",
6,	2,	0,	"eisa",
6,	3,	0,	"mca",
6,	4,	0,	"pci-to-pci",
6,	4,	1,	"pci-to-pci (subtractive)",
6,	5,	0,	"pcmcia",
6,	6,	0,	"nubus",
6,	7,	0,	"cardbus",
6,	8,	Dontcare,	"raceway",
6,	9,	0x40,	"semi-transparent pci-to-pci (primary to host)",
6,	9,	0x80,	"semi-transparent pci-to-pci (secondary to host)",
6,	0xa,	0,	"infiniband-to-pci vendor interface",
6,	0xa,	1,	"infiniband-to-pci asi-sig",

7,	Class,	Class,	"serial",
7,	0,	0,	"xt-compatable",
7,	0,	1,	"16450 compatable",
7,	0,	2,	"16550 compatable",
7,	0,	3,	"16650 compatable",
7,	0,	4,	"16750 compatable",
7,	0,	5,	"16850 compatable",
7,	0,	6,	"16950 compatable",
7,	1,	0,	"parallel",
7,	1,	1,	"bi-directional parallel",
7,	1,	2,	"ecp 1.x parallel",
7,	1,	3,	"ieee1284",
7,	1,	4,	"ieee1284 target",
7,	2,	0,	"multiport serial",
7,	3,	0,	"16450 compatable + hayes",
7,	3,	1,	"16550 compatable + hayes",
7,	3,	2,	"16650 compatable + hayes",
7,	3,	3,	"16750 compatable + hayes",

7,	4,	0,	"ieee488.1/2",
7,	5,	0,	"smartcard",

8,	Class,	Class,	"base",
8,	0,	0,	"8259 pic",
8,	0,	1,	"isa pic",
8,	0,	2,	"eisa pic",
8,	0,	0x10,	"ioapic",
8,	0,	0x20,	"io(x)apic",
8,	1,	0,	"8237 dma",
8,	1,	1,	"isa dma",
8,	1,	2,	"eisa dma",
8,	2,	0,	"8254 timer",
8,	2,	1,	"isa timer",
8,	2,	2,	"eisa timers",
8,	2,	3,	"hpet",
8,	3,	0,	"rtc",
8,	3,	1,	"isa rtc",
8,	4,	0,	"hotplug",
8,	5,	0,	"sd host",
8,	6,	0,	"iommu",

9,	Class,	Class,	"input",
9,	0,	0,	"keyboard",
9,	1,	0,	"pen",
9,	2,	0,	"mouse",
9,	3,	0,	"scanner",
9,	4,	Dontcare,	"gameport",

0x0a,	Class,	Class,	"dock",

0x0b,	Class,	Class,	"cpu",
0x0b,	0,	0,	"386",
0x0b,	1,	0,	"486",
0x0b,	2,	0,	"pentium",
0x0b,	0x10,	0,	"alpha",
0x0b,	0x20,	0,	"ppc",
0x0b,	0x30,	0,	"mips",
0x0b,	0x40,	0,	"coproc",

0x0c,	Class,	Class,	"serialbus",
0x0c,	0,	0,	"ieee 1394 firewire",
0x0c,	0,	0x10,	"ieee 1394 firewire openhci",
0x0c,	1,	0,	"accessbus",
0x0c,	2,	0,	"ssa",
0x0c,	3,	0,	"usb uhci",
0x0c,	3,	0x10,	"usb ohci",
0x0c,	3,	0x20,	"usb ehci",
0x0c,	3,	0x80,	"usb pi unknown",
0x0c,	3,	0xfe,	"usb target",
0x0c,	4,	0,	"fibre channel",
0x0c,	5,	0,	"smbus",
0x0c,	6,	0,	"infiniband",
0x0c,	7,	0,	"ipmi smic",
0x0c,	7,	1,	"ipmi kbd",
0x0c,	7,	2,	"ipmi block",
0x0c,	8,	0,	"sercos",
0x0c,	9,	0,	"canbus",

0x0d,	Class,	Class,	"wireless",
0x0d,	0,	0,	"irda",
0x0d,	1,	0,	"consumer ir",
0x0d,	1,	1,	"uwb radio",
0x0d,	0x10,	0,	"rf",
0x0d,	0x11,	0,	"bluetooth",
0x0d,	0x12,	0,	"broadband",
0x0d,	0x20,	0,	"802.11a 5ghz",
0x0d,	0x21,	0,	"802.11b 2.5ghz",

0x0e,	Class,	Class,	"i2o",
0x0e,	0,	0,	"i2o fifo",
0x0e,	0,	Dontcare,	"i2o",

0xf,	Class,	Class,	"satellite",
0xf,	1,	0,	"tv",
0xf,	2,	0,	"audio",
0xf,	3,	0,	"voice",
0xf,	4,	0,	"data",

0x10,	Class,	Class,	"crypt",
0x10,	0,	0,	"network/compute crypt",
0x10,	0x10,	0,	"entertainment crypt",

0x11,	Class,	Class,	"data",
0x11,	0,	0,	"dpio",
0x11,	1,	0,	"perf counter",
0x11,	0x10,	0,	"comm sync",
0x11,	0x20,	0,	"mgmt card",
	
};

char *ttab[] = {
[1]	"disk",
[2]	"net",
[3]	"vid",
[4]	"aud",
[5]	"mem",
[6]	"brg",
[7]	"ser",
[8]	"base",
[9]	"inpt",
[0xa]	"dock",
[0xb]	"proc",
[0xd]	"rad",
[0x10]	"cryp",
};

char*
pcitype(Pcidev *p)
{
	if(p->ccrb == 0x0c && p->ccru == 3)
		return "usb";
	if(p->ccrb == 0x0c && p->ccru == 5)
		return "smb";
	if(p->ccrb < nelem(ttab) && ttab[p->ccrb] != nil)
		return ttab[p->ccrb];
	return "unk";
}

void
stdhdr(Pcidev *p)
{
	int i;
	Bar *b;

	Bprint(&o, "%#T:	%-4.4s %.2ux.%.2ux.%.2ux %.4ux/%.4ux %3d",
		p->tbdf, pcitype(p), p->ccrb, p->ccru, p->ccrp, p->vid, p->did, p->intl);
	b = p->mem;
	for(i = 0; i < nelem(p->mem); i++){
		if(b[i].size == 0)
			continue;
		Bprint(&o, " %d:%.8p %d", i, b[i].bar, b[i].size);
	}
	Bprint(&o, "\n");
}

void
fnbclass(Pcidev *p)
{
	char *class, *name;
	int i;
	Etype *e;

	Bprint(&o, "%#T:\t", p->tbdf);

	class = nil;
	name = nil;
	for(i = 0; i < nelem(etypetab); i++){
		e = etypetab + i;

		if(e->ccru == Class && e->ccrp == Class){
			if(class != nil)
				break;
			if(e->ccrb == p->ccrb)
				class = e->name;
			continue;
		}

		if(e->ccrb == p->ccrb
		&& (e->ccru == p->ccru || e->ccru == Dontcare)
		&& (e->ccrp == p->ccrp || e->ccrp == Dontcare)){
			name = e->name;
			break;
		}
	}
	Bprint(&o, "\t");
	if(class != nil)
		Bprint(&o, "%s\t", class);
	if(name != nil)
		Bprint(&o, "%s", name);
	Bprint(&o, "\n");
}

static char *pcifile;

void
verbosesetup(void)
{
	int fd;
	vlong n;
	static int once;

	if(once)
		return;
	once = 1;

	fd = open("/lib/pci", OREAD);
	n = seek(fd, 0, 2);
	seek(fd, 0, 0);
	if(n == -1)
		return;
	pcifile = malloc(n+1);
	if(pcifile == nil)
		sysfatal("malloc %r");
	if(readn(fd, pcifile, n) != n){
		close(fd);
		free(pcifile);
		pcifile = nil;
		return;
	}
	pcifile[n] = 0;
	close(fd);
}

char*
findn(char *buf, char *key, int ltab, int *n)
{
	char *e, *e1, *a, *b;
	int i;

	if(ltab>0){
		for(e = buf;;){
			e1 = strchr(e, '\n');
			if(e1 == nil)
				goto fin;
			e = e1+1;
			if(e[0] == '#')
				continue;
			for(i = 0; i < ltab; i++)
				if(e[i] != '\t')
					goto fin;
		}
		
	fin:
		i = e[0];
		e[0] = 0;
		a = strstr(buf, key);
		e[0] = i;

	}else
		a = strstr(buf, key);

	if(a == nil)
		return nil;
	a += strlen(key);
	b = strchr(a, '\n');
	if(b == nil)
		return nil;
	*n = b-a;
	return a;
}

void
verbose(uint vid, uint did, uint svid, uint sdid)
{
	char buf[32], *a, *b;
	int n, bn;
	static int once;

	if(once == 0){
		verbosesetup();
		once = 1;
	}
	if(pcifile == nil)
		return;
	snprint(buf, sizeof buf, "\n%.4ux\t", vid);
	a = findn(pcifile, buf, 0, &n);
	if(a == nil)
		return;
	Bprint(&o, "	%.*s ", n, a);

	snprint(buf, sizeof buf, "\n\t%.4ux", did);
	a = findn(a+n, buf, 1, &n);
	if(a != nil)
		Bprint(&o, "%.*s ", n, a);
	Bprint(&o, "\n");

	if(svid == 0 && sdid == 0)
		return;
	if(flag['v'] < 2)
		return;

	snprint(buf, sizeof buf, "\n%.4ux\t", svid);
	b = findn(pcifile, buf, 0, &bn);
	if(b != nil)
		Bprint(&o, "		%.*s ", bn, b);

	if(a != nil){
		snprint(buf, sizeof buf, "\n\t\t%.4ux\t%.4ux", svid, sdid);
		a = findn(a+n, buf, 2, &n);
		if(a != nil){
			if(b == nil)
				Bprint(&o, "\t\t");
			Bprint(&o, "%.*s ", n, a);
		}
	}

	Bprint(&o, "(%.4ux/%.4ux)\n", svid, sdid);
}

int
crackvd(char *s, ushort *v, ushort *d)
{
	char *r;

	*v = strtoul(s, &r, 16);
	if(*r != '/')
		return -1;
	*d = strtoul(r+1, &r, 16);
	if(*r != 0)
		return -1;
	return 0;
}

void
fnnil(Pcidev *p)
{
	stdhdr(p);
	if(flag['v'])
		verbose(p->vid, p->did, p->svid, p->sdid);
}

void
fncap(Pcidev *p)
{
	prcap(p, flag['v']!=0);
}

void
fnmsi(Pcidev *p)
{
	uint c, f, datao, d;
	uvlong a;

	c = pcicap(p, 0, PciCapMSI);
	if(c == -1){
		Bprint(&o, "%#T	\n", p->tbdf);
		return;
	}
	f = pcicfgr16(p, c + 2);
	a = 0;
	datao = 8;
	if(f & Cap64){
		datao += 4;
		a = (uvlong)pcicfgr32(p, c + 8)<<32;
	}
	a |= pcicfgr32(p, c + 4);
	d = pcicfgr16(p, c + datao);

	/* tbdf: flags, msi target address, target data, next pointer */
	Bprint(&o, "%#T	%.4ux	%.16llux	%.8ux	%.2ux\n", p->tbdf, f, a, d, c);
	if(flag['v']){
		Bprint(&o, "\tflags: ");
		if(f & Msienable)
			Bprint(&o, "enabled ");
		if(f & Cap64)
			Bprint(&o, "64-bit ");
		if(f & Vmask)
			Bprint(&o, "vector-maskable ");
		Bprint(&o, "%d-alloced ", 1<<(f>>4 & 7));
		Bprint(&o, "%d-avail\n", 1<<(f>>1 & 7));

	}
}

void
usage(void)
{
	fprint(2, "usage: pci [-bcmv] [vid/did] ...\n");
	exits("usage");
}

typedef void (*Fn)(Pcidev*);

Fn fntab[] = {
['b']	fnbclass,
['c']	fncap,
['m']	fnmsi,
};

void
main(int argc, char **argv)
{
	int i, tbdf;
	Pcidev *p, x;
	void (*fn)(Pcidev*);

	fn = fnnil;
	ARGBEGIN{
	case 'b':
	case 'c':
	case 'm':
	case 'v':
		i = ARGC();
		flag[i]++;
		if(i < nelem(fntab))
		if(fntab[i] != nil)
			fn = fntab[i];
		break;
	default:
		usage();
	}ARGEND;

	if(Binit(&o, 1, OWRITE) == -1)
		sysfatal("Binit: %r");

	pciinit();
	if(argc > 0)
		for(i = 0; i < argc; i++){
			tbdf = strtotbdf(argv[i]);
			if(tbdf != BUSUNKNOWN){
				if((p = pcimatchtbdf(tbdf)) == nil)
					continue;
				fn(p);
				continue;
			}

			x.svid = x.sdid = 0;
			if(crackvd(argv[i], &x.vid, &x.did) != -1){
				p = pcimatch(nil, x.vid, x.did);
				if(p == nil){
					p = &x;
					Bprint(&o, "%.4ux/%.4ux\n", x.vid, x.did);
					if(flag['v'])
						verbose(p->vid, p->did, p->svid, p->sdid);
				}else
					fn(p);
				continue;
			}

			usage();
		}
	else
		for(p = nil; p = pcimatch(p, 0, 0);)
			fn(p);
}
