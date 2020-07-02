#include <u.h>
#include <libc.h>
#include <bio.h>

char 	*defargv[] = {".", 0};
char	*cmpldir;
char	*base;
int	flag[256];
uint	dev ;
uint	type;
Biobuf	out;

void
warn(char *s)
{
	if(flag['f'] == 0)
		fprint(2, "fmode: %s: %r\n", s);
}

void
usage(void)
{
	fprint(2, "usage: fmode [-1dfq] [path ...]\n");
	exits("usage");
}

/*  if you think this scales you'd be wrong.  this is is 1/4096th of a linear search.  */

enum{
	Ncache		= 4096,		/* must be power of two */
	Cachebits	= Ncache-1,
};

typedef struct{
	vlong	qpath;
	uint	dev;
	uchar	type;
} Fsig;

typedef	struct	Cache	Cache;
struct Cache{
	Fsig	*cache;
	int	n;
	int	nalloc;
} cache[Ncache];

void
clearcache(void)
{
	int i;

	for(i = 0; i < nelem(cache); i++)
		free(cache[i].cache);
	memset(cache, 0, nelem(cache)*sizeof cache[0]);
}

enum {
	Mask	= DMDIR | DMAPPEND | DMEXCL | 0777ul,
};

void
cmp(ulong p0, char *file)
{
	char *s;
	Dir *d;

	file += strlen(base)+1;
	s = smprint("%s/%s", cmpldir, file);
	d = dirstat(s);
	if(d == nil){
		if(!flag['f'])
			fprint(2, "# %s: deleted\n", file);
		return;
	}
	if((d->mode & Mask) != (p0 & Mask))
		Bprint(&out, "chmod %.3ulo %q\n", p0 & Mask, file);
	free(d);
}

int
seen(Dir *dir)
{
	Fsig 	*f;
	Cache 	*c;
	int	i;

	c = &cache[dir->qid.path&Cachebits];
	f = c->cache;
	for(i = 0; i < c->n; i++)
		if(dir->qid.path == f[i].qpath
			&& dir->type == f[i].type
			&& dir->dev == f[i].dev)
			return 1;
	if(i == c->nalloc){
		c->nalloc += 20;
		f = c->cache = realloc(c->cache, c->nalloc*sizeof *f);
	}
	f[c->n].qpath = dir->qid.path;
	f[c->n].type = dir->type;
	f[c->n].dev = dir->dev;
	c->n++;
	return 0;
}

int
skip(Dir *d)
{
	if(strcmp(d->name, ".") == 0|| strcmp(d->name, "..") == 0 || seen(d))
		return 1;
	return 0;
}

void
find(char *name)
{
	int fd, n;
	Dir *buf, *p, *e;
	char file[256];

	if((fd = open(name, OREAD)) < 0) {
		warn(name);
		return;
	}
//	Bprint(&out, fmt, name);
	for(; (n = dirread(fd, &buf)) > 0; free(buf))
		for(p = buf, e = p+n; p < e; p++){
			snprint(file, sizeof file, "%s/%s", name, p->name);
			cmp(p->mode, file);
			if(p->qid.type&QTDIR && !skip(p))
				find(file);
		}
	close(fd);
}

void
main(int argc, char *argv[])
{
	doquote = needsrcquote;
	quotefmtinstall();

	ARGBEGIN{
	case 'f':
		flag[ARGC()] = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc < 1)
		usage();
	cmpldir = *argv++;

	Binit(&out, 1, OWRITE);
	if(argc == 0)
		argv = defargv;
	base = *argv;
	find(*argv);
	Bterm(&out);
	exits(0);
}
