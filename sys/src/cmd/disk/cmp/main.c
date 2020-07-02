/*
 * cf. cmp <{dd -if data -bs 64k -count 20000} <{dd -if ../sda1/data -bs 64k -count 20000}
 * copyright © 2009 erik quanstrom
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <libsec.h>

enum {
	Stack	= 64*1024,
	Block	= 64*1024,
	Buffer	= 3,

	Memcmp= 1<<0,
	Sha1	= 1<<1,

	Ferror	= 1<<1,
	Fcmp	= 1<<2,
	Fend	= 1<<3,
};

typedef struct Ddargs Ddargs;
struct Ddargs {
	int	fd;
	Channel	*c;
	ulong	bs;
	uvlong	start;
	uvlong	end;
};

typedef struct Bargs Bargs;
struct Bargs {
	uvlong	nblocks;
	ulong	bs;
	int	nend;
};

typedef struct Msgbuf Msgbuf;
struct Msgbuf {
	uint	flags;
	uvlong	lba;
	char	status[ERRMAX];
	uchar	data[Block];
};

Channel	*blockfree;
Channel	*blockalloc;
Biobuf	out;

void
blockproc(void *a)
{
	uint h, t, f, e, c, m;
	uvlong i;
	Bargs *args;
	Msgbuf *s, *r, **tab;
	static Alt alts[3];

	threadsetname("blockproc");

	alts[0].c = blockalloc;
	alts[0].v = &s;
	alts[0].op = CHANSND;
	alts[1].c = blockfree;
	alts[1].v = &r;
	alts[1].op = CHANRCV;
	alts[2].op = CHANEND;

	args = (Bargs*)a;
	tab = malloc(args->nblocks * sizeof tab[0]);
	m = args->nblocks - 1;
	if(tab == nil)
		sysfatal("malloc: %r");
	for(i = 0; i < args->nblocks; i++){
		tab[i] = malloc(sizeof(Msgbuf));
		if(tab[i] == nil)
			sysfatal("malloc: %r");
	}
	h = t = 0;
	e = c = 0;
	s = nil;
	for(f = args->nend; f > 0;){
		if(s == nil){
			s = tab[h % m];
			if(s != nil){
				tab[h++ % m] = nil;
				alts[0].op = CHANSND;
			}else
				alts[0].op = CHANNOP;
		}
		switch(alt(alts)){
		case 0:
			s = nil;
			break;
		case 1:
			assert(r != nil && tab[t % m] == nil);
			tab[t++ % m] = r;
			if(r->flags & Fend)
				f--;
			if(r->flags & Fcmp)
				c++;
			if(r->flags & Ferror)
				e++;
			r = nil;
			break;
		}
	}
	for(i = 0; i < args->nblocks; i++)
		free(tab[i]);
	free(tab);
	if(e > 0)
		threadexitsall("errors");
	if(c > 0)
		threadexitsall("cmp");
	threadexitsall("");
}

Msgbuf*
bufalloc(void)
{
	Msgbuf *b;

	b = recvp(blockalloc);
	if(b == nil)
		sysfatal("recvp: %r");
	b->flags = 0;
	b->lba = 0;
	b->status[0] = 0;
	return b;
}

static int
preadn(int fd, void *av, long n, vlong o)
{
	char *a;
	long m, t;

	a = av;
	t = 0;
	while(t < n){
		m = pread(fd, a+t, n-t, o+t);
		if(m <= 0){
			if(t == 0)
				return m;
			break;
		}
		t += m;
	}
	return t;
}

void
ddproc(void *a)
{
	int rv;
	uvlong i;
	Ddargs *d;
	Msgbuf *b;

	threadsetname("ddproc");
	d = (Ddargs*)a;
	for(i = d->start; i < d->end; i++){
		b = bufalloc();
		b->lba = i;
		rv = preadn(d->fd, b->data, d->bs, b->lba * d->bs);
		if(rv != d->bs){
			errstr(b->status, sizeof b->status);
			b->flags |= Ferror;
		}
		sendp(d->c, b);
	}
	close(d->fd);

	b = bufalloc();
	b->flags |= Fend;
	sendp(d->c, b);
	threadexits("");
}

static int
Afmt(Fmt *f)
{
	char buf[SHA1dlen*2 + 1];
	uchar *u, i;

	u = va_arg(f->args, uchar*);
	if(u == 0 && f->flags & FmtSharp)
		return fmtstrcpy(f, "-");
	if(u == 0)
		return fmtstrcpy(f, "<nildigest>");
	for(i = 0; i < SHA1dlen; i++)
		sprint(buf + 2*i, "%2.2ux", u[i]);
	return fmtstrcpy(f, buf);
}
#pragma	varargck	type	"A"	uchar*

uint	bs		= Block;
uint	cmptype		= Memcmp;
Channel *dev[2];
QLock	cmplock;
int	pflag;

int
docmp(Msgbuf **b)
{
	uchar suma[SHA1dlen], sumb[SHA1dlen], *x, *y;
	uint i;
	uvlong lba;

	x = b[0]->data;
	y = b[1]->data;

	switch(cmptype){
	case Memcmp:
		if(memcmp(x, y, bs) != 0){
			lba = b[0]->lba;
			for(i = 0; i < bs; i++)
				if(x[i] != y[i])
					break;
			Bprint(&out, "%llud + %ud\n", lba, i);
			return 1;
		}
		return 0;
	case Sha1:
fprint(2, "sha1\n");
abort();
fprint(2, "  sha1\n");
		sha1(x, bs, suma, nil);
fprint(2, "b\n");
		sha1(y, bs, sumb, nil);
fprint(2, "sha1\n");
		if(memcmp(suma, sumb, sizeof suma) != 0){
fprint(2, "cmp! %A %A\n", suma, sumb);
			Bprint(&out, "%A %A\n", suma, sumb);
			return 1;
		}
fprint(2, "cmp %A %A\n", suma, sumb);
		return 0;
	default:
		abort();
		return -1;
	}
}

void
cmpproc(void*)
{
	int i, end;
	Msgbuf *b[2];

	threadsetname("cmpproc");
	for(;;){
		qlock(&cmplock);
		for(i = 0; i < 2; i++)
			b[i] = recvp(dev[i]);
		qunlock(&cmplock);
		assert(b[0] != nil && b[1] != nil);
		assert(b[0]->lba == b[1]->lba);
		end = b[0]->flags & Fend;

		if(b[0]->flags & Ferror)
			fprint(2, "cmp error: %llud: device 0 error: %s\n",
				b[0]->lba, b[0]->status);
		else if(b[0]->flags & Ferror)
			fprint(2, "cmp error: %llud: device 1 error: %s\n",
				b[1]->lba, b[1]->status);
		else if(!end && docmp(b))
			b[0]->flags |= Fcmp;
		sendp(blockfree, b[0]);
		sendp(blockfree, b[1]);
		if(end)
			break;
	}
	threadexits("");
}

void
usage(void)
{
	fprint(2, "usage: disk/cmp [-n nblocks] [-b blocksz] dev0 dev1\n");
	threadexitsall("usage");
}

Ddargs d[2];
Bargs a;

void
threadmain(int argc, char **argv)
{
	int i;
	uvlong nblocks;
	Dir *e;

	nblocks = 0;
	ARGBEGIN{
	case 'n':
		nblocks = atoi(EARGF(usage()));
		break;
	case 'b':
		bs = atoi(EARGF(usage()));
		break;
	case 's':
		cmptype = Sha1;
		break;
	case 'p':
		pflag = 1;
		break;
	default:
		usage();
	}ARGEND
	if(argc != 2)
		usage();
	fmtinstall('A', Afmt);
	Binit(&out, 1, OWRITE); 
	for(i = 0; i < 2; i++){
		d[i].fd = open(argv[i], OREAD);
		if(d[i].fd == -1)
			sysfatal("open: %r");
		d[i].bs = bs;
		d[i].start = 0;
		if(nblocks != 0)
			d[i].end = nblocks;
		else{
			e = dirfstat(d[i].fd);
			if(e == nil)
				sysfatal("dirfstat: %r");
			d[i].end = e->length / d[i].bs;
			free(e);
		}
		d[i].c = dev[i] = chancreate(sizeof(Msgbuf*), Buffer);
		if(d[i].c == nil)
			sysfatal("chancreate: %r");
	}
	blockfree = chancreate(sizeof(Msgbuf*), 1);
	blockalloc = chancreate(sizeof(Msgbuf*), 1);
	if(blockalloc == nil || blockfree == nil)
		sysfatal("chancreate: %r");
	a.nblocks = 2*Buffer;
	a.bs = bs;
	a.nend = 2;
	proccreate(ddproc, d + 0, Stack);
	proccreate(ddproc, d + 1, Stack);
	for(i = 0; i < 4; i++)
		proccreate(cmpproc, nil, Stack);
	blockproc(&a);
	threadexitsall("");
}
