#include <u.h>
#include <libc.h>

/* WARNING.  must keep in sync with your fs */

enum
{
	RBUFSIZE		= 8192,
	SUPER_ADDR		= 2,
};

enum
{
	Tnone		= 0,
	Tsuper,			/* the super block */
	Tdirold,
	Tind1old,
	Tind2old,
	Tfile,			/* file contents */
	Tfree,			/* in free list */
	Tbuck,			/* cache fs bucket */
	Tvirgo,			/* fake worm virgin bits */
	Tcache,			/* cw cache things */
	Tconfig,		/* configuration block */
	/* Tdir & indirect blocks are last to allow for greater depth */
	Tdir,			/* directory contents */
	Tind1,			/* points to blocks */
	Tind2,			/* points to Tind1 */
	Tind3,			/* points to Tind2 */
	Tind4,			/* points to Tind3 */
	Maxtind,
	MAXTAG,

	Tmaxind = Maxtind - 1,
};

typedef	struct	Fbuf	Fbuf;
typedef	struct	Iobuf	Iobuf;
typedef	struct	Superb	Superb;
typedef	struct	Super1	Super1;
typedef	struct	Tag	Tag;
typedef		uvlong	Off;
typedef		uvlong	Wideoff;

#define	BUFSIZE	(RBUFSIZE-sizeof(Tag))
#define FEPERBUF	((BUFSIZE-sizeof(Super1)-sizeof(Off))/sizeof(Off))

struct	Tag
{
	short	pad;		/* make tag end at a long boundary */
	short	tag;
	Off	path;
};

struct	Iobuf
{
	char	iobuf[RBUFSIZE];	/* only active while locked */
	Off	addr;
	int	flags;
};

/* DONT TOUCH, this is the disk structure */
struct	Super1
{
	Off	fstart;
	Off	fsize;
	Off	tfree;
	Off	qidgen;		/* generator for unique ids */
	/*
	 * Stuff for WWC device
	 */
	Off	cwraddr;	/* cfs root addr */
	Off	roraddr;	/* dump root addr */
	Off	last;		/* last super block addr */
	Off	next;		/* next super block addr */
#ifdef AUTOSWAB
	vlong	magic;		/* for byte-order detection */
	/* in memory only, not on disk (maybe) */
	int	flags;
#endif
};
/* DONT TOUCH, this is the disk structure */
struct	Fbuf
{
	Off	nfree;
	Off	free[FEPERBUF];
};

/* DONT TOUCH, this is the disk structure */
struct	Superb
{
	Fbuf	fbuf;
	Super1;
};


/*
 * flags to getbuf
 */
enum
{
	Bread	= 1<<0,	/* read the block if miss */
	Bprobe	= 1<<1,	/* return null if miss */
	Bmod	= 1<<2,	/* buffer is dirty, needs writing */
	Bimm	= 1<<3,	/* write immediately on putbuf */
	Bres	= 1<<4,	/* reserved, never renamed */
};

#define	QPDIR		0x80000000L
#define	QPNONE		0
#define	QPROOT		1
#define	QPSUPER		2

char*	tagnames[] =
{
	[Tbuck]		"Tbuck",
	[Tdir]		"Tdir",
	[Tfile]		"Tfile",
	[Tfree]		"Tfree",
	[Tind1]		"Tind1",
	[Tind2]		"Tind2",
#ifndef OLD
	[Tind3]		"Tind3",
	[Tind4]		"Tind4",
	/* add more Tind tags here ... */
#endif
	[Tnone]		"Tnone",
	[Tsuper]	"Tsuper",
	[Tvirgo]	"Tvirgo",
	[Tcache]	"Tcache",
};

static int
Gfmt(Fmt* fmt)
{
	int t;
	char *s;

	t = va_arg(fmt->args, int);
	s = "<badtag>";
	if(t >= 0 && t < MAXTAG)
		s = tagnames[t];
	return fmtstrcpy(fmt, s);
}

#pragma	varargck	type	"G"	int
int
checktag(Iobuf *p, int tag, Off qpath)
{
	Tag *t;
	static Off lastaddr = -1;

	t = (Tag*)(p->iobuf+BUFSIZE);
	if(t->tag != tag) {
		if(p->flags & Bmod) {
			print("	tag = %d/%llud; expected %lld/%d -- not flushed\n",
				t->tag, (Wideoff)t->path, (Wideoff)qpath, tag);
			return 2;
		}
	//	if(p->dev != nil && p->dev->type == Devcw)
	//		cwfree(p->dev, p->addr);
		if(p->addr != lastaddr)
			print("	tag = %G/%llud; expected %G/%lld -- flushed (%lld)\n",
				t->tag, (Wideoff)t->path, tag, (Wideoff)qpath,
				(Wideoff)p->addr);
		lastaddr = p->addr;
	//	p->dev = devnone;
		p->addr = -1;
		p->flags = 0;
		return 2;
	}
	if(qpath != QPNONE) {
		if((qpath ^ t->path) & ~QPDIR) {
			print("	tag/path = %llud; expected %d/%llux\n",
				(Wideoff)t->path, tag, (Wideoff)qpath);
			return 0;
		}
	}
	return 0;
}

int
devread(int dev, Off m, Iobuf *p)
{
	memset(p->iobuf, 0, RBUFSIZE);
	if(pread(dev, p->iobuf, RBUFSIZE, m*RBUFSIZE) != RBUFSIZE)
		return 1;
	p->addr = m;
	return 0;
}

void
recover(Off m)
{
	Iobuf *p;
	Off baddr;
	Superb *s;

	p = malloc(sizeof *p);
	memset(p, 0, sizeof p);
	s = (Superb*)p->iobuf;
	baddr = -1;

	for(;;) {
		if(devread(0, m, p) ||
		   checktag(p, Tsuper, QPSUPER))
			break;
//		l = m;
		baddr = m;
		m = s->next;
		print("dump %lld is good; %lld next\n", (Wideoff)baddr, (Wideoff)m);
	}
	if(baddr != -1)
		print("last dump %lld\n", (Wideoff)baddr);
	else
		print("no dump found\n");
}

void
tagcheck(Off m)
{
	Iobuf *p;
	Tag *t;

	p = malloc(sizeof *p);
	memset(p, 0, sizeof p);

	for(; m < 95000; m++) {
		if(devread(0, m, p))
			continue;
		t = (Tag*)(p->iobuf+BUFSIZE);
		if(t->tag != 0 && t->tag <= Tmaxind)
			print("tag %G/%lld good\n", t->tag, m);
	}
}

void
usage(void)
{
	fprint(2, "usage: sbcheck [worm]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int fd;
	Off first;
	fmtinstall('G', Gfmt);

	first = SUPER_ADDR;
	ARGBEGIN{
	case 'f':
		first = strtoull(EARGF(usage()), 0, 0);
		break;
	default:
		usage();
	}ARGEND
	if(argc > 1)
		usage();
	if(argc == 1){
		fd = open(argv[0], OREAD);
		if(fd == -1)
			sysfatal("open: %r");
		close(0);
		dup(fd, 0);
	}
	if(1)
		recover(first);
	else
		tagcheck(first);
	exits("");
}
