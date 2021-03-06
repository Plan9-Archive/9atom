/*
 * vblade — virtual aoe target
 * copyright © 2007—2013 erik quanstrom
 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include <ip.h>			/* irony */
#include <fis.h>

enum {
	Eaddrlen	= 6,		/* only defined in kernel */
};
#include "aoe.h"

enum {
	Fclone,
	Fdata,
	Flast,

	Fraw	= 1<<0,

	Nether	= 8,
	Nvblade	= 8,
	Nmask	= 10,
	Nmaxout= 128,
	Maxpkt	= 10000,
	Hdrlba	= 128,
	Conflen	= 1024,
};

typedef	struct	Conf	Conf;
typedef	struct	Vbhdr	Vbhdr;
typedef	struct	Vblade	Vblade;

struct Conf {
	int	iflag;
	int	flag;
	int	shelf;
	int	slot;
	uvlong	maxlba;
	uint	nmask;
	uchar	*mask;
	char	*config;
};

struct Vbhdr {
	char	magic[32];
	char	size[32];
	char	address[16];
	char	configlen[6];
	char	mask[Nmask*(12+1)];
	char	pad[512-32-32-16-6-Nmask*(12+1)];
	char	config[Conflen];
};

typedef struct Vblade {
	Vbhdr	hdr;
	vlong	maxlba;
	vlong	hdrsz;
	uint	nmask;
	Lock	mlk;
	uchar	*mask;
	int	shelf;
	int	slot;
	int	clen;
	int	flag;
	int	fd;
};

static	Vblade	vblade[Nvblade];
static	int	nblade;

static	char	*ethertab[Nether] = {
	"/net/ether0",
};
static	int	etheridx = 1;
static	int	efdtab[Nether*Flast];
static	uchar	pkttab[Nether][Maxpkt];
static	uchar	bctab[Nether][Maxpkt];
static	int	mtutab[Nether];
static	char	Magic[] = "aoe vblade\n";

static int
getmtu(char *p)
{
	char buf[50];
	int fd, mtu;

	snprint(buf, sizeof buf, "%s/mtu", p);
	if((fd = open(buf, OREAD)) == -1)
		return 2;
	if(read(fd, buf, 36) < 0)
		return 2;
	close(fd);
	buf[36] = 0;
	mtu = strtoul(buf+12, 0, 0)-Aoehsz-Aoeatasz;
	return mtu>>9;
}

int
parseshelf(char *s, int *shelf, int *slot)
{
	int a, b;

	a = strtoul(s, &s, 0);
	if(*s++ != '.')
		return -1;
	b = strtoul(s, &s, 0);
	if(*s != 0)
		return -1;
	*shelf = a;
	*slot = b;
	return 0;
}

static vlong
getsize(char *s)
{
	static char tab[] = "ptgmk";
	char *p;
	vlong v;

	v = strtoull(s, &s, 0);
	while((p = strchr(tab, *s++)) && *p)
		while(*p++)
			v *= 1024;
	if(s[-1])
		return -1;
	return v;
}

vlong
sizetolba(vlong size)
{
	if(size < 512 || size & 0x1ff){
		fprint(2, "invalid size %lld\n", size);
		exits("size");
	}
	return size>>9;
}

static int
savevblade(int fd, Vblade *vb)
{
	char *p, *e;
	int i;

	sprint(vb->hdr.size, "%lld", vb->maxlba<<9);
	sprint(vb->hdr.address, "%d.%d", vb->shelf, vb->slot);
	sprint(vb->hdr.configlen, "%d", vb->clen);

	p = vb->hdr.mask;
	e = p + sizeof(vb->hdr.mask);
	for(i = 0; i < vb->nmask; i++)
		p = seprint(p, e, "%E ", vb->mask+Eaddrlen*i);
	*p = 0;

	if(vb->flag & Fraw)
		return 0;
	if(pwrite(fd, vb, sizeof *vb, 0) != sizeof *vb)
		return -1;
	return 0;
}

static void
chkmask(Vblade *vb, Vbhdr *h)
{
	char *p;
	uchar ea[Eaddrlen];
	int i;

	p = h->mask;
	for(i = 0;; i++){
		while(*p == ' ')
			p++;
		if(*p == 0)
			break;
		if(parseether(ea, p) == -1)
			break;
		vb->mask = realloc(vb->mask, (vb->nmask+1) * Eaddrlen);
		memmove(vb->mask + vb->nmask*Eaddrlen, ea, Eaddrlen);
		vb->nmask++;

		p = strchr(p, ' ');
		if(p == nil)
			break;
	}
}

static char*
chkvblade(int fd, Vblade *vb)
{
	Vbhdr *h;

	h = &vb->hdr;
	if(readn(fd, (char*)h, sizeof *h) != sizeof *h)
		return "bad read";
	if(memcmp(h->magic, Magic, sizeof Magic))
		return "bad magic";
	h->size[sizeof h->size-1] = 0;
	vb->maxlba = sizetolba(strtoull(h->size, 0, 0));
	if(parseshelf(h->address, &vb->shelf, &vb->slot) == -1)
		return "bad shelf";
	h->configlen[sizeof h->configlen-1] = 0;
	vb->clen = strtoul(h->configlen, 0, 0);
	h->mask[sizeof h->mask-1] = 0;
	chkmask(vb, h);
	return 0;
}

void
checkfile(char *s, Vblade *vb, int iflag)
{
	char *e;

	vb->fd = open(s, ORDWR);
	if(vb->fd == -1)
		sysfatal("can't open backing store: %r");
	if(iflag == 0 && (e = chkvblade(vb->fd, vb)))
		sysfatal("invalid vblade %s", e);
}

void
recheck(int fd, Vblade *vb)
{
	Dir *d;
	vlong v;

	d = dirfstat(fd);
	if(d == 0)
		sysfatal("can't stat: %r");
	if((vb->flag & Fraw) == 0)
		vb->hdrsz = Hdrlba;
	v = sizetolba(d->length & ~0x1ff) - vb->hdrsz;
	free(d);
	if(vb->maxlba > v)
		sysfatal("cmdline size too large (%lld sector overhead)", vb->hdrsz);
	if(vb->maxlba == 0)
		vb->maxlba = v;

	savevblade(fd, vb);
}

int
aoeopen(char *e, int fds[])
{
	char buf[128], ctl[13];
	int n;

	snprint(buf, sizeof buf, "%s/clone", e);
	if((fds[Fclone] = open(buf, ORDWR)) == -1)
		return -1;
	memset(ctl, 0, sizeof ctl);
	if(read(fds[Fclone], ctl, sizeof ctl - 1) < 0)
		return -1;
	n = atoi(ctl);
	snprint(buf, sizeof buf, "connect %d", Aoetype);
	if(write(fds[Fclone], buf, strlen(buf)) != strlen(buf))
		return -1;
	snprint(buf, sizeof buf, "%s/%d/data", e, n);
	fds[Fdata] = open(buf, ORDWR);
	return fds[Fdata];
}

void
replyhdr(Aoehdr *h, Vblade *vblade)
{
	uchar	ea[Eaddrlen];

	memmove(ea, h->dst, Eaddrlen);
	memmove(h->dst, h->src, Eaddrlen);
	memmove(h->src, ea, Eaddrlen);

	hnputs(h->major, vblade->shelf);
	h->minor = vblade->slot;
	h->verflag |= AFrsp;
}

static int
servebad(uchar *pkt, Vblade*, int)
{
	Aoehdr *h;

	h = (Aoehdr*)pkt;
	h->verflag |= AFerr;
	h->error = AEcmd;

	return Aoehsz;
}

static uchar nilea[Eaddrlen];

static int
servemask(uchar *pkt, Vblade *vb, int mtu)
{
	int i, j, r, e;
	uchar mx[Nmask*Eaddrlen], *mtab[Nmask], *p;
	Aoem *m;
	Aoemd *d;

	m = (Aoem*)(pkt + Aoehsz);
	if(m->mcnt > (mtu - Aoehsz - Aoemsz)/Aoemdsz)
		return -1;

	if(!canlock(&vb->mlk))
		return -1;		/* drop */

	switch(m->mcmd){
	default:
		unlock(&vb->mlk);
		return servebad(pkt, vb, mtu);
	case Medit:
		memcpy(mx, vb->mask, vb->nmask*Eaddrlen);
		j = 0;
		for(i = 0; i < vb->nmask; i++){
			p = mx + i*Eaddrlen;
			if(memcmp(p, nilea, Eaddrlen) != 0)
				mtab[j++] = p;
		}
		e = 0;
		p = pkt + Aoehsz + Aoemsz;
		for(i = 0; i < m->mcnt && e == 0; i++){
			d = (Aoemd*)(p + i*Aoemdsz);
			switch(d->dcmd){
			default:
				e = MEunk;
				break;
			case MDnop:
				break;
			case MDadd:
				for(i = 0; i < j; i++)
					if(memcmp(d->ea, mtab[j], Eaddrlen) == 0)
						continue;
				if(j == Nmask)
					e = MEfull;
				else
					memcpy(mtab[j++], d->ea, Eaddrlen);
				break;
			case MDdel:
				for(i = 0; i < j; i++)
					if(memcmp(d->ea, mtab[j], Eaddrlen) == 0)
						break;
				if(i < j){
					for(; i < j; i++)
						mtab[i] = mtab[i+1];
					j--;
				}
				break;
			}
		}

		if(e != 0){
			m->merr = e;
			r = Aoehsz + Aoemsz;
			break;
		}

		p = malloc(j*Eaddrlen);
		if(p == nil){
			r = -1;
			break;
		}

		for(i = 0; i < j; i++)
			memcpy(p+i*Eaddrlen, mtab[i], Eaddrlen);
		free(vb->mask);
		vb->nmask = j;
		vb->mask = p;
	case Mread:
		m->mcnt = vb->nmask;
		m->merr = 0;
		p = pkt + Aoehsz + Aoemsz;
		for(i = 0; i < m->mcnt; i++){
			d = (Aoemd*)(p + i*Aoemdsz);
			d->dres = 0;
			d->dcmd = MDnop;
			memcpy(d->ea, vb->mask + i*Eaddrlen, Eaddrlen);
		}
		r = Aoehsz + Aoemsz + m->mcnt * Aoemdsz;
		break;
	}

	unlock(&vb->mlk);
	return r;
}

static int
serveconfig(uchar *pkt, Vblade *vb, int mtu)
{
	char *cfg;
	int cmd, reqlen, len;
	Aoehdr *h;
	Aoecfg *q;

	h = (Aoehdr*)pkt;
	q = (Aoecfg*)(pkt + Aoehsz);

	if(memcmp(h->src, h->dst, Eaddrlen) == 0)
		return -1;

	reqlen = nhgets(q->cslen);
	len = vb->clen;
	cmd = q->verccmd&0xf;
	cfg = (char*)(pkt + Aoehsz + Aoecfgsz);

	switch(cmd){
	case AQCtest:
		if(reqlen != len)
			return -1;
	case AQCprefix:
		if(reqlen > len)
			return -1;
		if(memcmp(vb->hdr.config, cfg, reqlen) != 0)
			return -1;
	case AQCread:
		break;
	case AQCset:
		if(len && len != reqlen || memcmp(vb->hdr.config, cfg, reqlen) != 0){
			h->verflag |= AFerr;
			h->error = AEcfg;
			break;
		}
	case AQCfset:
		if(reqlen > Conflen){
			h->verflag |= AFerr;
			h->error = AEarg;
			break;
		}
		memset(vb->hdr.config, 0, sizeof vb->hdr.config);
		memmove(vb->hdr.config, cfg, reqlen);
		vb->clen = len = reqlen;
		savevblade(vb->fd, vb);
		break;
	default:
		h->verflag |= AFerr;
		h->error = AEarg;
		break;
	}

	memmove(cfg, vb->hdr.config, len);
	hnputs(q->cslen, len);
	hnputs(q->bufcnt, Nmaxout);
	q->scnt = mtu;
	hnputs(q->fwver, 2323);
	q->verccmd = Aoever<<4 | cmd;

	return len;
}

static ushort ident[256] = {
	[47] 0x8000,
	[49] 0x0200,
	[50] 0x4000,
	[83] 0x5400,
	[84] 0x4000,
	[86] 0x1400,
	[87] 0x4000,
	[93] 0x400b,
};

static void
idmoveto(char *a, int idx, int len, char *s)
{
	char *p;

	p = a+idx*2;
	for(; len > 0; len -= 2) {
		if(*s == 0)
			p[1] = ' ';
		else
			p[1] = *s++;
		if (*s == 0)
			p[0] = ' ';
		else
			p[0] = *s++;
		p += 2;
	}
}

static void
lbamoveto(char *p, int idx, int n, vlong lba)
{
	int i;

	p += idx*2;
	for(i = 0; i < n; i++)
		*p++ = lba>>i*8;
}

enum {
	Crd		= 0x20,
	Crdext		= 0x24,
	Cwr		= 0x30,
	Cwrext		= 0x34,
	Cid		= 0xec,
};

static uvlong
getlba(uchar *p)
{
	uvlong v;

	v = p[0];
	v |= p[1]<<8;
	v |= p[2]<<16;
	v |= p[3]<<24;
	v |= (uvlong)p[4]<<32;
	v |= (uvlong)p[5]<<40;
	return v;
}

static void
putlba(uchar *p, vlong lba)
{
	p[0] = lba;
	p[1] = lba>>8;
	p[2] = lba>>16;
	p[3] = lba>>24;
	p[4] = lba>>32;
	p[5] = lba>>40;
}

static int
serveata(uchar *pkt, Vblade *vb, int mtu)
{
	char *buf;
	int rbytes, bytes, len;
	vlong lba, off;
	Aoehdr *h;
	Aoeata *a;

	h = (Aoehdr*)pkt;
	a = (Aoeata*)(pkt + Aoehsz);

	buf = (char*)(pkt + Aoehsz + Aoeatasz);
	lba = getlba(a->lba);
	len = a->scnt<<9;
	off = lba+vb->hdrsz<<9;

	if(a->scnt > mtu || a->scnt == 0){
		h->verflag |= AFerr;
		h->error = AEarg;
		a->cmdstat = ASdrdy|ASerr;
		return 0;
	}
	
	if(a->cmdstat != Cid)
	if(lba+a->scnt > vb->maxlba){
		a->errfeat = Eidnf;
		a->cmdstat = ASdrdy|ASerr;
		return 0;
	}

	if(a->cmdstat&0xf0 == 0x20)
		lba &= 0xfffffff;
	switch(a->cmdstat){
	default:
		a->errfeat = Eabrt;
		a->cmdstat = ASdrdy|ASerr;
		return 0;
	case Cid:
		memmove(buf, ident, sizeof ident);
		idmoveto(buf, 27, 40, "Plan 9 Vblade");
		idmoveto(buf, 10, 20, "serial#");
		idmoveto(buf, 23, 8, "2");
		lbamoveto(buf, 60, 4, vb->maxlba);
		lbamoveto(buf, 100, 8, vb->maxlba);
		a->cmdstat = ASdrdy;
		return 512;
		break;
	case Crd:
	case Crdext:
		bytes = pread(vb->fd, buf, len, off);
		rbytes = bytes;
		break;
	case Cwr:
	case Cwrext:
		bytes = pwrite(vb->fd, buf, len, off);
		rbytes  = 0;
		break;
	}
	if(bytes != len){
		a->errfeat = Eabrt;
		a->cmdstat = ASdf|ASerr;
		putlba(a->lba, lba+(len-bytes)>>9);
		return 0;
	}

	putlba(a->lba, lba+a->scnt);
	a->scnt = 0;
	a->errfeat = 0;
	a->cmdstat = ASdrdy;

	return rbytes;
}

static int
myea(uchar ea[6], char *p)
{
	char buf[50];
	int fd;

	snprint(buf, sizeof buf, "%s/addr", p);
	if((fd = open(buf, OREAD)) == -1)
		return -1;
	if(read(fd, buf, 12) < 12)
		return -1;
	close(fd);
	return parseether(ea, buf);
}

static int
bcastpkt(uchar *pkt, uint shelf, uint slot, int i)
{
	Aoehdr *h;

	h = (Aoehdr*)pkt;
	myea(h->dst, ethertab[i]);
	memset(h->src, 0xff, Eaddrlen);
	hnputs(h->type, Aoetype);
	hnputs(h->major, shelf);
	h->minor = slot;
	h->cmd = ACconfig;
	*(u32int*)h->tag = 0;
	return Aoehsz + Aoecfgsz;
}

int
bladereply(Vblade *v, int i, int fd, uchar *pkt)
{
	int n;
	Aoehdr *h;

	h = (Aoehdr*)pkt;
	switch(h->cmd){
	case ACata:
		n = serveata(pkt, v, mtutab[i]);
		n += Aoehsz+Aoeatasz;
		break;
	case ACconfig:
		n = serveconfig(pkt, v, mtutab[i]);
		if(n >= 0)
			n += Aoehsz+Aoecfgsz;
		break;
	case ACmask:
		n = servemask(pkt, v, mtutab[i]);
		break;
	default:
		n = servebad(pkt, v, mtutab[i]);
		break;
	}
	if(n == -1)
		return -1;
	replyhdr(h, v);
	if(n < 60){
		memset(pkt+n, 0, 60-n);
		n = 60;
	}
	if(write(fd, h, n) != n){
		fprint(2, "vblade: write to %s failed: %r\n", ethertab[i]);
		return -1;
	}
	return 0;
}

int
filter(Vblade *v, uchar *ea)
{
	int i;
	uchar *u;

	if(v->nmask == 0)
		return 0;

	u = v->mask;
	for(i = 0; i < v->nmask; i++)
		if(memcmp(u + i*Eaddrlen, ea, Eaddrlen) == 0)
			return 0;
	return -1;
}

void
serve(void *a)
{
	int i, j, popcnt, vec, n, s, efd;
	uchar *pkt, *bcpkt;
	Aoehdr *h;
	Vblade *v;

	i = (int)(uintptr)a;

	efd = efdtab[i*Flast+Fdata];
	pkt = pkttab[i];
	bcpkt = bctab[i];

	n = 60;
	h = (Aoehdr*)pkt;
	bcastpkt(pkt, 0xffff, 0xff, i);
	goto start;

	for(;;){
		n = read(efd, pkt, Maxpkt);
	start:
		if(n < 60 || h->verflag & AFrsp)
			continue;
		s = nhgets(h->major);
		popcnt = 0;
		vec = 0;
		for(j = 0; j < nblade; j++){
			v = vblade+j;
			if(v->shelf == s || s == 0xffff)
			if(v->slot == h->minor || h->minor == 0xff)
			if(v->nmask == 0 || filter(v, h->src) == 0){
				popcnt++;
				vec |= 1<<j;
			}
		}
		if(popcnt == 0)
			continue;
		for(j = 0;; j++){
			if((vec & 1<<j) == 0)
				continue;
			if(--popcnt>0){
				memcpy(bcpkt, pkt, n);
				bladereply(vblade + j, i, efd, bcpkt);
			}else{
				bladereply(vblade + j, i, efd, pkt);
				break;
			}
		}
	}
}

void
launch(char *tab[], int fdtab[])
{
	int i;

	for(i = 0; tab[i]; i++){
		if(aoeopen(tab[i], fdtab+Flast*i) < 0)
			sysfatal("network open: %r");
		/*
		 * use proc not threads.  otherwise we will block on read/write.
		 */
		proccreate(serve, (void*)i, 32*1024);
	}
}

void
usage(void)
{
	fprint(2, "vblade [-ir] [-s size] [-a shelf.slot] [-m mask] [-c config] [-e ether] file\n");
	exits("usage");
}

void
goblade(Vblade *vblade, char *file, Conf *c)
{
	char *anal;

	if(c->iflag == 1){
		memcpy(vblade->hdr.magic, Magic, sizeof Magic);
		vblade->nmask = c->nmask;
		vblade->mask = c->mask;
	}
	checkfile(file, vblade, c->iflag);

	vblade->flag = c->flag;
	if(c->shelf != -1){
		vblade->shelf = c->shelf;
		vblade->slot = c->slot;
	}
	if(c->maxlba > 0)
		vblade->maxlba = c->maxlba;
	if(c->config != nil)
		memmove(vblade->hdr.config, c->config, vblade->clen = strlen(c->config));
	if(c->nmask > 0){
		free(vblade->mask);
		vblade->nmask = c->nmask;
		vblade->mask = c->mask;
	}
	recheck(vblade->fd, vblade);

	anal = "";
	if(vblade->maxlba > 1)
		anal = "s";
	fprint(2, "lblade %d.%d %lld sector%s\n", vblade->shelf, vblade->slot, vblade->maxlba, anal);
}

void
threadmain(int argc, char **argv)
{
	uchar ea[6];
	int i, lastc, anye;
	Conf c;

	fmtinstall('E', eipfmt);
	anye = 0;
	for(;;){
		if(nblade == nelem(vblade))
			sysfatal("too many blades");
		c = (Conf){0, 0, -1, -1, 0, 0, nil, nil};
		lastc = 0;
		ARGBEGIN{
		case 'a':
			lastc = 'a';
			if(parseshelf(EARGF(usage()), &c.shelf, &c.slot) == -1)
				sysfatal("bad vblade address");
			break;
		case 'c':
			lastc = 'c';
			c.config = EARGF(usage());
			break;
		case 'e':
			lastc = 'e';
			if(anye++ == 0)
				etheridx = 0;
			if(etheridx == nelem(ethertab))
				sysfatal("too many interfaces");
			ethertab[etheridx++] = EARGF(usage());
			break;
		case 'i':
			lastc = 'i';
			c.iflag = 1;
			break;
		case 'm':
			lastc = 'm';
			if(parseether(ea, EARGF(usage())) == -1)
				usage();
			if((c.mask = realloc(c.mask, Eaddrlen*(c.nmask+1))) == nil)
				sysfatal("realloc: %r");
			memmove(c.mask+Eaddrlen*c.nmask, ea, Eaddrlen);
			c.nmask++;
			break;
		case 'r':
			lastc = 'r';
			c.flag |= Fraw;
			c.iflag = 1;
			break;
		case 's':
			lastc = 's';
			c.maxlba = sizetolba(getsize(EARGF(usage())));
			break;
		default:
			lastc = '?';
			usage();
		}ARGEND;

		if(argc == 0 && lastc == 'e')
			break;
		if(argc == 0)
			usage();
		goblade(vblade + nblade++, *argv, &c);
		if(argc == 1)
			break;
	}

	if(nblade == 0)
		usage();	
	for(i = 0; i < etheridx; i++)
		mtutab[i] = getmtu(ethertab[i]);

	launch(ethertab, efdtab);

	for(; sleep(1*1000) != -1;)
		;
	threadexitsall("interrupted");
}
