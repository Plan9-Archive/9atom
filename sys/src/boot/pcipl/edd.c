#include <u.h>
#include <fns.h>

typedef	struct	Edd	Edd;
typedef	struct	Eddbdf	Eddbdf;

struct Edd {
	uchar	bufsz[2];
 	uchar	flags[2];		/* 1<<6: no media; 1<<2: rmb */
	uchar	c[4];
	uchar	h[4];
	uchar	s[4];
	uchar	sectors[8];
	uchar	sectsize[2];
	uchar	pteptr[4];	/* seg:offset table ptr */
	uchar	pathpres[2];	/* bedd */
	uchar	pathlen;		/* 0x2c */
	uchar	res[3];
	uchar	bustype[4];
	uchar	iface[8];		/* "ATA(PI|  )   " or "(SCSI|SAS|USB )    " */
	uchar	ipath[8];
	uchar	dpath[16];
	uchar	res1;
	uchar	cksum;		/* byte 30-73 */
};

struct Eddbdf {
	uchar	bus;
	uchar	slot;
	uchar	fn;
	uchar	chan;
};

extern int getdriveparm(int dev, Edd *buf);

uint
mkbus(int b, int d, int f)
{
	return 12<<24 | b<<16 | d<<11 | f<<8;
}

static void
leaddconf(uchar *u, int w)
{
	char buf[20], *e;

	e = fmtle(buf, u, w);
	confappend("0x", 2);
	confappend(buf, e - buf);
	confappend(" ", 1);
}

void
bootdrive(void)
{
	uchar ebuf[128];
	int n;
	Edd *p;
	Eddbdf *ip;

	memset(ebuf, 0, sizeof ebuf);
	p = (Edd*)ebuf;
	p->bufsz[0] = sizeof ebuf;
	p->bufsz[1] = sizeof ebuf>>8;

	if(getdriveparm(0x80+0, p) == -1)
		return;
	confappend("drive0=sectors=", 15);
	leaddconf(p->sectors, 8);

	if(memcmp(p->bustype, "ISA", 3) == 0){
		confappend("port=", 5);
		leaddconf(p->ipath, 2);
	}else if(memcmp(p->bustype, "PCI", 3) == 0
	|| memcmp(p->bustype, "XPRS", 4) == 0){
		ip = (Eddbdf*)p->ipath;
		n = mkbus(ip->bus, ip->slot, ip->fn);
		confappend("tbdf=", 5);
		vaddconf(n, 4);
	}
	if(memcmp(p->iface, "ATA", 3) == 0)
		n = 2;
	else if(memcmp(p->iface, "SATA", 4) == 0)
		n = 1;
	else if(memcmp(p->iface, "USB", 3) == 0){
		confappend("usb=y ", 5);
		n = 8;
	}
	else
		n = 0;
	if(n > 0){
		confappend("chan=", 5);
		leaddconf(p->dpath, n);
	}
	confappend("\n", 1);
}
