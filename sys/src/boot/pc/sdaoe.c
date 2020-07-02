/*
 * aoe sd bootstrap driver, copyright © 2007-9 coraid
 */

#include "u.h"
#include "mem.h"
#include "lib.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "sd.h"
#include "aoe.h"
#include <fis.h>

#define uprint(...)	snprint(up->genbuf, sizeof up->genbuf, __VA_ARGS__);

enum{
	Tfree		= -1,
	Tmgmt,
};

typedef struct Ctlr Ctlr;
struct Ctlr{
	Ctlr	*next;
	SDunit	*unit;

	int	ctlrno;
	int	major;
	int	minor;
	uchar	ea[Eaddrlen];
	ushort	lasttag;

	ulong	vers;
	uchar	mediachange;
	Sfis;

	uvlong	sectors;
	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];
	char	ident[0x100];
};

static	Ctlr	*head;
static	Ctlr	*tail;

static	int	aoeether[10];

SDifc sdaoeifc;

static void
hnputs(uchar *p, ushort i)
{
	p[0] = i>>8;
	p[1] = i;
}

static void
hnputl(uchar *p, ulong i)
{
	p[0] = i>>24;
	p[1] = i>>16;
	p[2] = i>>8;
	p[3] = i;
}

static ushort
nhgets(uchar *p)
{
	ushort i;

	i = *p<<8;
	return i|p[1];
}

static ulong
nhgetl(uchar *p)
{
	return p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3];
}

static int
newtag(Ctlr *d)
{
	int t;
loop:
	t = ++d->lasttag<<16;
	t |= m->ticks&0xffff;
	switch(t) {
	case Tfree:
	case Tmgmt:
		goto loop;
	default:
		return t;
	}
}

static int
hset(Ctlr *d, Aoehdr *h, int cmd)
{
	int tag;

	memmove(h->dst, d->ea, Eaddrlen);
	hnputs(h->type, Aoetype);
	h->verflag = Aoever<<4;
	hnputs(h->major, d->major);
	h->minor = d->minor;
	h->cmd = cmd;
	hnputl(h->tag, tag = newtag(d));

	return tag;
}

static int
ataidentify(Ctlr *c, ushort *id)
{
	vlong s;

	s = idfeat(c, id);
	idmove(c->serial, id+10, 20);
	idmove(c->firmware, id+23, 8);
	idmove(c->model, id+27, 40);
	print("aoe discovers %d.%d: %s %s\n", c->major, c->minor, c->model, c->serial);
	c->sectors = s;
	c->mediachange = 1;

	return 0;
}

static void
identifydump(Aoeata *a)
{
	print("%E %E type=%.4ux verflag=%x error=%x %d.%d cmd=%d tag=%.8lux\n", 
		a->dst, a->src, nhgets(a->type), a->verflag, a->error, nhgets(a->major), a->minor, a->cmd, nhgetl(a->tag));
	print("   aflag=%x errfeat=%ux scnt=%d cmdstat=%ux, lba=%d? res=%.4ux\n",
		a->aflag, a->errfeat, a->scnt, a->cmdstat, 0, nhgets(a->res));
}

static int
idpkt(Ctlr *c, Aoeata *a)
{
	memset(a, 0, sizeof *a);
	a->cmdstat = Cid;
	a->scnt = 1;
	a->lba[3] = 0xa0;
	return hset(c, a, ACata);
}

static int
chktag(int *out, int nout, int tag)
{
	int j;

	for(j = 0; j <= nout; j++)
		if(out[j] == tag)
			return 0;
	print("wrong tag\n");
	for(j = 0; j <= nout; j++)
		print("%.8ux != %.8ux\n", out[j], tag);
	return -1;
}

/*
 * ignore the tag for identify.  better than ignoring
 * a response to the wrong identify request
 */
static int
identify(Ctlr *c)
{
	Etherpkt p;
	Aoeata *a;
	int tag[5], i, n;

	memset(&p, 0, sizeof p);
	a = (Aoeata*)&p;
	
	for(i = 0;;){
		if(i == 5){
			print("aoe: identify timeout\n");
			return -1;
		}
		tag[i] = idpkt(c, a);
		ethertxpkt(c->ctlrno, &p, sizeof *a, 0);
		memset(&p, 0, sizeof p);
	next:
		n = etherrxpkt(c->ctlrno, &p, 125);
		if(n == 0){
			i++;
			continue;
		}
		if(nhgets(a->type) != Aoetype)
			goto next;
		if(nhgets(a->major) != c->major || a->minor != c->minor){
			print("wrong device %d.%d want %d.%d; %d\n", nhgets(a->major), a->minor, c->major, c->minor, n);
			goto next;
		}
		if(chktag(tag, i, nhgetl(a->tag)) == -1)
			goto next;
		if(a->cmdstat&0xa9){
			print("aoe: ata error on identify: %2ux\n", a->cmdstat);
			return -1;
		}
		if(a->scnt != 1)
			continue;
		break;
	}

	c->feat = 0;
	ataidentify(c, (ushort*)(a+1));

	return 0;
}

static Ctlr*
ctlrlookup(int major, int minor)
{
	Ctlr *c;

	for(c = head; c; c = c->next)
		if(c->major == major && c->minor == minor)
			break;
	return c;
}

static Ctlr*
newctlr(Etherpkt *p)
{
	Ctlr *c;
	Aoeqc *q;
	int major, minor;

	q = (Aoeqc*)p;
	if(nhgets(q->type) != Aoetype)
		return 0;
	major = nhgets(q->major);
	minor = q->minor;

	if(major == 0xffff || minor == 0xff)
		return 0;

	if(ctlrlookup(major, minor)){
		print("duplicate shelf.slot\n");
		return 0;
	}

	if((c = malloc(sizeof *c)) == 0)
		return 0;
	c->major = major;
	c->minor = minor;
	memmove(c->ea, q->src, Eaddrlen);

	if(head != 0)
		tail->next = c;
	else
		head = c;
	tail = c;
	return c;
}

static	int	aoeminor = -1;
static	int	aoemajor = -1;

int
interestingaoe(uchar *u, int l)
{
	Aoehdr *h;

	if(l < 60)
		return 0;
	h = (Aoehdr*)u;
	if(h->minor != aoeminor || nhgets(h->major) != aoemajor)
		return 0;
	return 1;
}
	
static void
discover(int major, int minor)
{
	Aoehdr *h;
	Etherpkt p;
	int i;

	aoemajor = major;
	aoeminor = minor;
	for(i = 0; i < nelem(aoeether); i++){
		if(aoeether[i] == 0)
			continue;
		memset(&p, 0, ETHERMINTU);
		h = (Aoehdr*)&p;
		memset(h->dst, 0xff, sizeof h->dst);
		hnputs(h->type, Aoetype);
		h->verflag = Aoever<<4;
		hnputs(h->major, major);
		h->minor = minor;
		h->cmd = ACconfig;
		ethertxpkt(i, &p, ETHERMINTU, 0);
	}
}

static int
rxany(Etherpkt *p, int t)
{
	int i, n;

	for(i = 0; i < nelem(aoeether); i++){
		if(aoeether[i] == 0)
			continue;
	again:
		if(n = etherrxpkt(i, p, t)){
			if(nhgets(p->type) != Aoetype)
				goto again;
			return n;
		}
	}
	return 0;
}
	
static int
aoeprobe(int major, int minor, SDev *s)
{
	Ctlr *ctlr;
	Etherpkt p;
	int n, i;

	for(i = 0;; i += 200){
		if(i > 8000)
			return -1;
		discover(major, minor);
	again:
		n = rxany(&p, 100);
		if(n > 0 && (ctlr = newctlr(&p)))
			break;
		if(n > 0)
			goto again;
	}

	s->ctlr = ctlr;
	s->ifc = &sdaoeifc;
	s->nunit = 1;
	return 0;
}

static char 	*probef[32];
static int 	nprobe;

int
pnpprobeid(char *s)
{
	int id;

	if(strlen(s) < 2)
		return 0;
	id = 'e';
	if(s[1] == '!')
		id = s[0];
	return id;
}

int
tokenize(char *s, char **args, int maxargs)
{
	int nargs;

	for(nargs=0; nargs<maxargs; nargs++){
		while(*s!='\0' && strchr("\t\n ", *s)!=nil)
			s++;
		if(*s == '\0')
			break;
		args[nargs] = s;
		while(*s!='\0' && strchr("\t\n ", *s)==nil)
			s++;
		if(*s!='\0')
			*s++ = 0;
	}
	return nargs;
}

int
aoepnp0(void)
{
	char *p, c;
	int i;

	if((p = getconf("aoeif")) == 0)
		return 0;
	print("aoeif = %s\n", p);
	nprobe = tokenize(p, probef, nelem(probef));
	for(i = 0; i < nprobe; i++){
		if(strncmp(probef[i], "ether", 5) != 0)
			continue;
		c = probef[i][5];
		if(c > '9' || c < '0')
			continue;
		aoeether[c-'0'] = 1;
	}

	if((p = getconf("aoedev")) == 0)
		return 0;
	return nprobe = tokenize(p, probef, nelem(probef));
}

int
probeshelf(char *s, int *shelf, int *slot)
{
	char *r;
	int a, b;

	for(r = s+strlen(s)-1; r > s; r--)
		if((*r < '0' || *r > '9') && *r != '.'){
			r++;
			break;
		}
	a = strtoul(r, &r, 10);
	if(*r++ != '.')
		return -1;
	b = strtoul(r, 0, 10);

	*shelf = a;
	*slot = b;
	print("  shelf=%d.%d\n", a, b);
	return 0;
}

Ctlr*
pnpprobe(SDev *sd)
{
	static int i;
	char *p;
	int shelf, slot;

	if(i >= nprobe)
		return 0;
	p = probef[i++];
	if(strlen(p) < 2)
		return 0;
	if(p[1] == '!'){
		sd->idno = p[0];
		p += 2;
	}
	if(probeshelf(p, &shelf, &slot) == -1)
		return 0;
	if(aoeprobe(shelf, slot, sd) == -1)
		return 0;
	if(identify(sd->ctlr) == -1)
		return 0;
	return sd->ctlr;
}

/*
 * we may need to pretend we found something;
 */

SDev*
aoepnp(void)
{
	SDev *h, *t, *s;
	int n, i, id;

	print("aoepnp(%s)\n", getconf("aoeif"));
	if((n = aoepnp0()) == 0)
		n = 2;
	t = h = 0;
	for(i = 0; i < n; i++){
		id = 'e';
		s = malloc(sizeof *s);
		if(s == 0)
			break;
		s->ctlr = 0;
		s->idno = id;
		s->ifc = &sdaoeifc;
		s->nunit = 1;
		pnpprobe(s);

		if(h)
			t->next = s;
		else
			h = s;
		t = s;
	}
	return h;
}

static int
aoeverify(SDunit *u)
{
	SDev *s;
	Ctlr *c;

	s = u->dev;
	c = s->ctlr;
	if(c == 0){
		aoepnp0();
		if((c = s->ctlr = pnpprobe(s)) == 0)
			return 0;
	}
	c->mediachange = 1;

	return 1;
}

static int
aoeonline(SDunit *u)
{
	Ctlr *c;
	int r;

	c = u->dev->ctlr;
	
	if(c->mediachange){
		r = 2;
		c->mediachange = 0;
		u->sectors = c->sectors;
		u->secsize = 512;
	} else
		r = 1;

	return r;
}

static int
rio(Ctlr *c, Aoeata *a, int n, int scnt)
{
	int i, tag, cmd;

	for(i = 0;;){
		if(i == 5){
			print("aoe: rio timeout\n");
			return 0;
		}
		tag = hset(c, a, ACata);
		cmd = a->cmdstat;
		ethertxpkt(c->ctlrno, (Etherpkt*)a, n, 0);
		memset(a, 0, sizeof *a);
	again:
		n = etherrxpkt(c->ctlrno, (Etherpkt*)a, 125);
		if(n == 0){
			i++;
			continue;
		}
		if(nhgets(a->type) != Aoetype)
			goto again;
		if(nhgetl(a->tag) != tag)
			goto again;
		if(nhgets(a->major) != c->major || a->minor != c->minor)
			goto again;
		if(a->cmdstat&0xa9){
			print("aoe: ata rio error: %2ux\n", a->cmdstat);
			return 0;
		}
		switch(cmd){
		case Crd:
		case Crdext:
			if(n-sizeof *a < scnt*512){
				print("aoe: runt expect %d got %d\n", scnt*512 + sizeof *a, n);
				return 0;
			}
			return n-sizeof *a;
		case Cwr:
		case Cwrext:
			return scnt*512;
		default:
			print("unknown cmd %ux\n", cmd);
			break;
		}
		i++;
		continue;
	}
}

static void
putlba(Aoeata *a, vlong lba)
{
	uchar *c;

	c = a->lba;
	c[0] = lba;
	c[1] = lba>>8;
	c[2] = lba>>16;
	c[3] = lba>>24;
	c[4] = lba>>32;
	c[5] = lba>>40;
}

static uchar pktbuf[1024 + sizeof(Aoeata)];

static int
aoebuild(Ctlr *c, int write, char *data, vlong lba, int scnt)
{
	Aoeata *a;
	int n;

	memset(pktbuf, 0, sizeof pktbuf);
	a = (Aoeata*)pktbuf;
	hset(c, a, ACata);
	putlba(a, lba);

	a->cmdstat = 0x20;
	if(c->feat & Dllba){
		a->aflag |= AAFext;
		a->cmdstat |= 4;
	}else{
		a->lba[3] &= 0xf;
		a->lba[3] |= Ataobs | Atalba;
	}

	n = scnt;
	if(n > 2)
		n = 2;
	a->scnt = n;

	if(write){
		a->aflag |= AAFwrite;
		a->cmdstat |= 10;
		memmove(a + 1, data, n*512);
		n = sizeof *a + n*512;
	}else
		n = sizeof *a;
	return n;
}

static long
aoebio(SDunit *unit, int, int write, void *v, long nsec, uvlong lba)
{
	char *data;
	int size, n, rlen;
	Aoeata *a;
	Ctlr *c;
	
	c = unit->dev->ctlr;

	a = (Aoeata*)pktbuf;
	data = v;
	rlen = 0;

	for(; nsec > 0; nsec -= n){
//		print("aoebuild(%2x, %p, %lld, %d)\n", *cmd, data, lba, nsec);
		size = aoebuild(c, write, data, lba, nsec);
		if(size < 0)
			return -1;
		n = a->scnt;
		rlen += rio(c, a, size, n);
		if(!write)
			memmove(data, a + 1, n*512);
		data += n*512;
		lba += n;
	}
	return rlen;
}

SDifc sdaoeifc = {
	"aoe",

	aoepnp,
	nil,		/* legacy */
	nil,		/* id */
	nil,		/* enable */
	nil,		/* disable */

	aoeverify,
	aoeonline,
	nil,
	nil,
	nil,

	aoebio,
};
