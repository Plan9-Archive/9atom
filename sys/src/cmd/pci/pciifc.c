#include <u.h>
#include <libc.h>
#include <bio.h>

#include "dat.h"

static char* bustypes[] = {
	"CBUSI",
	"CBUSII",
	"EISA",
	"FUTURE",
	"INTERN",
	"ISA",
	"MBI",
	"MBII",
	"MCA",
	"MPI",
	"MPSA",
	"NUBUS",
	"PCI",
	"PCMCIA",
	"TC",
	"VL",
	"VME",
	"XPRESS",
};

/*static*/	Pcidev	*pcidevs;

static int
tbdffmt(Fmt* fmt)
{
	char p[128], t[16];
	int r;
	uint type, tbdf;

	switch(fmt->r){
	case 'T':
		tbdf = va_arg(fmt->args, int);
		if(tbdf == BUSUNKNOWN){
			snprint(p, sizeof p, "unknown");
			break;
		}
		type = BUSTYPE(tbdf);
		if(fmt->flags & FmtSharp)
			t[0] = 0;
		else if(type < nelem(bustypes))
			snprint(t, sizeof t, "%s.", bustypes[type]);
		else
			snprint(t, sizeof t, "%s.", "???");
		snprint(p, sizeof p, "%s%d.%d.%d", t, 
			BUSBNO(tbdf), BUSDNO(tbdf), BUSFNO(tbdf));
		break;

	default:
		snprint(p, sizeof p, "(tbdfconv)");
		break;
	}
	r = fmtstrcpy(fmt, p);

	return r;
}

static int
mkbus(uint t, uint b, uint d, uint f)
{
	return t<<24 | (uchar)b<<16 | (d&0x1f)<<11 | (f&7)<<8;
}

uint
getn(Pcidev *p, uint o, uint n)
{
	uchar *u;
	uint r, i;

	if(o >= sizeof p->cfg || (o&n-1))
		sysfatal("pcicfgr%d: offset range: %d", n*8, o);
	u = p->cfg + o;
	r = 0;
	for(i = 0; i < n; i++)
		r |= u[i]<<i*8;
	return r;
}

uint
pcicfgr32(Pcidev *p, uint o)
{
	return getn(p, o, 4);
}

uint
pcicfgr16(Pcidev *p, uint o)
{
	return getn(p, o, 2);
}

uint
pcicfgr8(Pcidev *p, uint o)
{
	return getn(p, o, 1);
}

void
putn(Pcidev *p, uint o, uint w, uint n)
{
	char buf[128];
	int fd, i;
	uchar *u;

	if(o >= sizeof p->cfg || (o&n-1))
		sysfatal("pcicfgw%d: offset range: %d", n*8, o);
	u = p->cfg + o;
	dprint(Dpciwrite, "%d bytes at %.2ux\n", n, o);
	for(i = 0; i < n; i++){
		dprint(Dpciwrite, "%.2ux = %.2ux ", o+i, (uchar)(w>>i*8));
		u[i] = w>>i*8;
	}
	dprint(Dpciwrite, "\n");

	snprint(buf, sizeof buf, "/dev/pci/%#Traw", p->tbdf);
	fd = open(buf, OWRITE);
	if(fd == -1)
		sysfatal("open: %r");

	dprint(Dpciwrite, "writep(%d, %.*lH [%.*ux], %d, %#.2ux)\n", fd, n, u, n*2, w, n, o);

	if(pwrite(fd, u, n, o) != n)
		sysfatal("pwrite: %r");
	close(fd);
}

void
pcicfgw32(Pcidev *p, uint o, uint w)
{
	putn(p, o, w, 4);
}

void
pcicfgw16(Pcidev *p, uint o, uint w)
{
	putn(p, o, w, 2);
}

void
pcicfgw8(Pcidev *p, uint o, uint w)
{
	putn(p, o, w, 1);
}

Pcidev*
pcimatch(Pcidev *p, int vid, int did)
{
	if(p == nil)
		p = pcidevs;
	else
		p = p->next;
	for(; p != nil; p = p->next)
		if(vid == 0 || p->vid == vid)
		if(did == 0 || p->did == did)
			break;
	return p;
}

Pcidev*
pcimatchtbdf(int tbdf)
{
	Pcidev *p;

	for(p = pcidevs; p != nil; p = p->next)
		if(p->tbdf == tbdf)
			break;
	return p;
}

void
pcisetbme(Pcidev* p)
{
	p->pcr |= MASen;
	pcicfgw16(p, PciPCR, p->pcr);
}

/* this is very ugly */
ulong
pcibarsize(Pcidev *p, int rno)
{
	ulong v, size;

	v = pcicfgr32(p, rno);
	pcicfgw32(p, rno, 0xFFFFFFF0);
	size = pcicfgr32(p, rno);
	if(v & 1)
		size |= 0xFFFF0000;
	pcicfgw32(p, rno, v);

	return -(size & ~0x0F);
}

void
bogusgetbarsizes(Pcidev *p)
{
	char buf[256], *f[20], *r;
	int fd, i, n, floor;
	uvlong u;
	Bar *b;

	n = 0;
	for(i = 0; i < nelem(p->mem); i++)
		n += p->mem[i].bar>0;
	if(n == 0)
		return;
	snprint(buf, sizeof buf, "/dev/pci/%#Tctl", p->tbdf);
	if((fd = open(buf, OREAD)) == -1)
		return;
	n = read(fd, buf, sizeof buf-1);
	buf[n] = 0;

	n = tokenize(buf, f, nelem(f));
	floor = -1;
	for(i = 0; i < n; i++){
		u = strtoull(f[i], &r, 16);
		if(*r != ':' || (int)u <= floor || u >= nelem(p->mem))
			continue;
		floor = u;
		b = p->mem + floor;
		u = strtoull(r+1, &r, 16);
		if(u != b->bar)
			print("%T: mismatched bar %d %.8p %.8llux\n", p->tbdf, floor, b->bar, u);
		i++;
		u = strtoull(f[i], &r, 0);
		if(u > 256*1024*1024)
			print("%T: bogus bar size %llud\n", p->tbdf, u);
		b->size = u;
	}
	close(fd);
}

void
pcicrackbar(Pcidev *p, Bar bar[6])
{
	int i, rno;

	memset(bar, 0, 6*sizeof bar[0]);

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
	case 0x0d:		/* wireless controller */
	case 0x0e:		/* intelligent controller */
	case 0x0f:		/* satellite controller */
	case 0x10:		/* crypto unit */
	case 0x11:		/* signal processor */
		if((pcicfgr8(p, PciHDT) & 0x7F) != 0)
			break;
		rno = PciBAR0;
		for(i = 0; i < nelem(p->mem); i++) {
			p->mem[i].bar = pcicfgr32(p, rno);
		//	p->mem[i].size = pcibarsize(p, rno);
			rno += 4;
		}
		bogusgetbarsizes(p);
		break;
	case 0x06:		/* bridge device */
//		bridgecfg(p);
		break;
	case 0x00:
	case 0x05:		/* memory controller */
	default:
		break;
	}
}

Pcidev*
getpcidev(char *s, int tbdf)
{
	char buf[256];
	int fd, sub;
	Pcidev *p;

	snprint(buf, sizeof buf, "/dev/pci/%s", s);
	fd = open(buf, OREAD);
	if(fd == -1)
		return nil;
	p = malloc(sizeof *p);
	if(p == nil)
		sysfatal("malloc: %r");
	memset(p, 0, sizeof *p);
	p->ncfg = read(fd, p->cfg, sizeof p->cfg);
	if(p->ncfg != 256 && p->ncfg != 4096){
		free(p);
		sysfatal("read: %r");
		return nil;
	}
	close(fd);

	p->vid = pcicfgr16(p, 0);
	p->did = pcicfgr16(p, 2);
	p->pcr = pcicfgr16(p, PciPCR);
	p->ccrb = pcicfgr8(p, PciCCRb);
	p->ccru = pcicfgr8(p, PciCCRu);
	p->ccrp = pcicfgr8(p, PciCCRp);
	p->intl = pcicfgr8(p, PciINTL);
	p->tbdf = tbdf;
	sub = pcicfgr32(p, PciSVID);
	p->svid = sub&0xffff;
	p->sdid = sub>>16;
	pcicrackbar(p, p->mem);
	return p;
}

uint
strtotbdf(char *s)
{
	char *f[5], buf[32];
	int c;

	if(strlen(s) >= sizeof buf -1)
		return ~0;
	strcpy(buf, s);
	c = gettokens(buf, f, nelem(f), ".");
	if(cistrcmp(f[0], "pci") == 0){
		c--;
		memmove(f, f+1, c*sizeof *f);
	}
	if(c != 3)
		return ~0;
	return mkbus(BusPCI, atoi(f[0]), atoi(f[1]), atoi(f[2]));
}

int
pcicap(Pcidev *p, int off, int cap)
{
	int i, c;

	/* status register bit 4 has capabilities */
	if((pcicfgr16(p, PciPSR) & 1<<4) == 0)
		return -1;	
	if(off == 0)
		switch(pcicfgr8(p, PciHDT) & 0x7f){
		default:
			return -1;
		case 0:				/* etc */
		case 1:				/* pci to pci bridge */
			off = 0x34;
			break;
		case 2:				/* cardbus bridge */
			off = 0x14;
			break;
		}
	else
		off++;
	for(i = 48; i--;){
		pcicapdbg("\t" "loop %x\n", off);
		off = pcicfgr8(p, off);
		pcicapdbg("\t" "pcicfgr8 %x\n", off);
		if(off < 0x40)
			break;
		off &= ~3;
		c = pcicfgr8(p, off);
		pcicapdbg("\t" "pcicfgr8 %x\n", c);
		if(c == 0xff)
			break;
		if(c == cap || cap == -1)
			return off;
		off++;
	}
	return -1;
}

static int
readpci(void)
{
	char *s;
	int fd, n, i, r, tbdf;
	Dir *d;
	Pcidev **p, *q;

	fd = open("/dev/pci", OREAD);
	if(fd == -1)
		return -1;
	r = 0;
	for(p = &pcidevs; q = *p; p = &q->next);
		;
	for(; n = dirread(fd, &d); free(d)){
		for(i = 0; i < n; i++){
			s = strstr(d[i].name, "raw");
			if(s == nil || strcmp(s, "raw") != 0)
				continue;
			tbdf = strtotbdf(d[i].name);
			if(tbdf == ~0)
				continue;
			if(q = getpcidev(d[i].name, tbdf)){
				*p = q;
				p = &q->next;
			}else{
				r = -1;
				break;
			}
		}
	}
	close(fd);
	return r;
}

void
pciinit(void)
{
	fmtinstall('T', tbdffmt);
	fmtinstall('H', encodefmt);

	/* this is a little questionable */
	rfork(RFNAMEG);
	if(bind("#$", "/dev", MBEFORE) == -1)
		sysfatal("bind: %r");
	if(readpci() == -1)
		sysfatal("readpci: %r");
}
