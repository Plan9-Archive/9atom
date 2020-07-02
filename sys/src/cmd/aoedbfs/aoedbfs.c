#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

Srv fs;

typedef struct{
	int	id;
	int	fd;
}Node;

enum{
	Xctl	= 1,
	Xfpregs,
	Xkregs,
	Xmem,
	Xproc,
	Xregs,
	Xsegment,
	Xtext,
	Xstatus,

};

struct{
	char 	*s;
	ulong	id;
	ulong	mode;
}tab[] = {
	"ctl",		Xctl,		0222,
	"fpregs",	Xfpregs,	0666,
	"kregs",	Xkregs,		0666,
	"mem",		Xmem,		0666,
	"proc",		Xproc,		0444,
	"regs",		Xregs,		0666,
	"text",		Xtext,		0444,
	"segment",	Xsegment,	0444,
	"status",	Xstatus,	0444,
};

#define dprint(...)		if(debug) fprint(2, _VA_ARGS_)

int	textfd = -1;
int	srvfd;
int 	debug = 0;
char*	textfile = 	"/usr/bwc/VS3/vs.nm";
char	srvname[32];
char*	procname = 	"1";
Channel	*rchan;
int	shelf = -1;

void
killall(Srv*)
{
	bind("#p", "/proc", MREPL);
	threadexitsall("done");
}

void
fsopen(Req *r)
{
	char buf[ERRMAX];
	Node *np;

	np = r->fid->file->aux;
	if (np)
	switch(np->id){
	case Xtext:
		close(textfd);
		textfd = open(textfile, OREAD);
		if(textfd < 0) {
			snprint(buf, sizeof buf, "text: %r");
			respond(r, buf);
			return;
		}
		break;
	}		
	respond(r, nil);
}

void
fsread(Req *r)
{
	int i, n;
	char buf[512];
	Node *np;

	np = r->fid->file->aux;
	switch(np->id){
	case Xfpregs:
	case Xproc:
	case Xkregs:
		respond(r, "Egreg");
		break;
	case Xregs:
	case Xmem:
	case Xsegment:
		if(sendp(rchan, r) != 1){
			snprint(buf, sizeof buf, "aoedbfs sendp: %r");
			respond(r, buf);
			return;
		}
		break;
	case Xstatus:
		n = sprint(buf, "%-28s%-28s%-28s", "remote", "system", "New");
		for(i = 0; i < 9; i++)
			n += sprint(buf+n, "%-12d", 0);
		readstr(r, buf);
		respond(r, nil);
		break;
	case Xtext:
		n = pread(textfd, r->ofcall.data, r->ifcall.count, r->ifcall.offset);
		if(n < 0) {
			rerrstr(buf, sizeof buf);
			respond(r, buf);
			break;
		}
		r->ofcall.count = n;
		respond(r, nil);
		break;
	default:
		respond(r, "unknown read");
	}
}

void
fswrite(Req *r)
{
	char buf[ERRMAX];
	Node *np;

	np = r->fid->file->aux;
	switch(np->id){
	case Xctl:
	case Xkregs:
	case Xmem:
	case Xsegment:
		if(sendp(rchan, r) != 1) {
			snprint(buf, sizeof buf, "aoedbfs sendp: %r");
			respond(r, buf);
		}
		break;
	default:
		respond(r, "Egreg");
		break;
	}
}

enum{
	Rbsize	= 8192,
};
uchar	aoebuf[Rbsize];
uvlong	aoeoff = -1;
int	aoelen;
int	aoefd = -1;

int
tailio(long (*io)(int, void*, long, vlong), int fd, void *buf, ulong l, uvlong byte)
{
	int r;
	ulong rem;

	rem = byte&(Rbsize-1);
	if(l+rem > Rbsize)
		l = Rbsize-rem;
	if(aoeoff == byte && aoefd == fd)
		r = aoelen;
	else{
		r = pread(fd, aoebuf, Rbsize, byte-rem);
		if(r < 0){
			aoeoff = -1;
			return -1;
		}
		aoeoff = byte-rem;
		aoelen = r;
		aoefd = fd;
	}
	if(l+rem >= r)
		l = r-rem;
	else if(io == pwrite)
		return -1;

	if(io == pwrite){
		memmove(aoebuf+rem, buf, l);
		r = io(fd, aoebuf, r, byte-rem);
		if(r != aoelen)
			return -1;
	}else
		memmove(buf, aoebuf+rem, l);
	return l;
}

static int
byteio(long (d)(int, void*, long, vlong), int fd, char *buf, ulong l, uvlong byte)
{
	int r;
	ulong l0;

	l0 = l;
loop:
	if(l == 0)
		return l0-l;
	if((r = tailio(d, fd, buf, l, byte)) < 0)
		return -1;
	byte += r;
	buf += r;
	l -= r;
	if(byte%Rbsize)
		return l0-l;

	while(l >= Rbsize){
		if((r = d(fd, buf, Rbsize, byte)) < 0)
			return -1;
		byte += r;
		buf += r;
		l -= r;
		if(r < Rbsize)
			return l0-l;
	}
	goto loop;
}

char*
xctl(Req *r)
{
	char *e;

	e = nil;
	if(strncmp(r->ifcall.data, "kill", 4) == 0
	|| strncmp(r->ifcall.data, "exit", 4) == 0){
		respond(r, nil);
		yield();
		bind("#p", "/proc", MREPL);
		threadexitsall("done");
	}else if(strncmp(r->ifcall.data, "close", 5)
	&& strncmp(r->ifcall.data, "open", 4))
		e = "permission denied";
	return e;
}

void
servaoe(void *)
{
	int n;
	char *e, buf[ERRMAX];
	long (*io)(int, void*, long, vlong);
	Req *r;
	Node *np;

	while(r = recvp(rchan)){
		np = r->fid->file->aux;
		e = nil;
		if(np->id == Xctl){
			e = xctl(r);
			goto rpt;
		}
		if(r->type == Twrite)
			io = pwrite;
		else //(r->type == Tread)
			io = pread;
		n = byteio(io, np->fd, r->ofcall.data, r->ifcall.count, r->ifcall.offset);
		if(n < 0){
			snprint(buf, sizeof buf, "%r");
			e = buf;
		} else if(n == r->ifcall.offset || io == pread)
			r->ofcall.count = n;
	rpt:
		respond(r, e);
	}
	sysfatal("recv: %r");
}

Srv fs = {
	.open=	fsopen,
	.read=	fsread,
	.write=	fswrite,
	.end=	killall,
};

void
setslot(char *path, int slot)
{
	File *f;
	int fd;
	Node *np;
	Dir *d;
	char buf[32], pbuf[32];

	incref(fs.tree->root);
	snprint(pbuf, sizeof pbuf, path, procname);
	f = walkfile(fs.tree->root, pbuf);
	if (f == nil)
		sysfatal("Can't walk %s: %r", pbuf);
	snprint(buf, sizeof buf, "/dev/aoe/%d.%d/data", shelf, slot);
	fd = open(buf, ORDWR);
	if (fd < 0)
		sysfatal("setslot: open: %r");
	if(!(d = dirfstat(fd)))
		sysfatal("dirfstat: %r");
	f->length = d->length;
	free(d);
	np = f->aux;
	np->fd = fd;
	closefile(f);
}

void
usage(void)
{
	fprint(2, "usage: aoedbfs [-d] [-p procnum] [-t textfile] shelf\n");
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	int i, p[2];
	File *dir;
	Node *np;

	rfork(RFNOTEG);
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'd':
		debug = 1;
		break;
	case 'p':
		procname = EARGF(usage());
		break;
	case 't':
		textfile = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	switch(argc){
	case 1:
		shelf = atoi(*argv);
		break;
	default:
		usage();
	}

	fmtinstall('H', encodefmt);
	quotefmtinstall();
	rchan = chancreate(sizeof(Req*), 10);
	if(pipe(p) < 0)
		sysfatal("pipe: %r");

	fmtinstall('F', fcallfmt);
	srvfd = p[1];
	proccreate(servaoe, nil, 8*8192);

	fs.tree = alloctree("aoedbfs", "aoedbfs", DMDIR|0555, nil);
	dir = createfile(fs.tree->root, procname, "aoedbfs", DMDIR|0555, 0);
	for(i=0; i<nelem(tab); i++) {
		np = malloc(sizeof *np);
		np->id = tab[i].id;
		np->fd = -1;
		closefile(createfile(dir, tab[i].s, "aoedbfs", tab[i].mode, np));
	}
	closefile(dir);
	setslot("%s/regs", 252);
	setslot("%s/mem", 253);
	setslot("%s/segment", 254);
	snprint(srvname, sizeof srvname, "aoedbfs.%d", getpid());
	threadpostmountsrv(&fs, srvname, "/proc", MBEFORE);
	threadexits(0);
}
