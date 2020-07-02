#include <u.h>
#include <libc.h>

/* WARNING.  must keep in sync with your fs */

enum
{
	RBUFSIZE		= 8192,
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

enum {
	Tagsz		= 12,
	BUFSIZE	= RBUFSIZE-Tagsz,
};

typedef	struct	Tag	Tag;
typedef	struct	Iobuf	Iobuf;
typedef		uvlong	Off;

struct	Tag
{
	uchar	pad[2];
	uchar	tag[2];
	uchar	path[8];		/* make tag end at a long boundary */
};

struct	Iobuf
{
	char	iobuf[RBUFSIZE];	/* only active while locked */
	int	flags;
};

void
settag(Iobuf *p, int tag)
{
	Tag *t;

	t = (Tag*)(p->iobuf+BUFSIZE);
	putle(t->tag, tag, 2);
	putle(t->path, 0, 8);
}

void
usage(void)
{
	fprint(2, "usage: mkfsconf < config\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	Iobuf p;

	ARGBEGIN{
	default:
		usage();
	}ARGEND
	if(argc)
		usage();

	memset(&p, 0, sizeof p);
	read(0, p.iobuf, BUFSIZE);
	settag(&p, Tconfig);
	write(1, p.iobuf, RBUFSIZE);
	exits("");
}
