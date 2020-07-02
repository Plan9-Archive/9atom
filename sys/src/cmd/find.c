#include <u.h>
#include <libc.h>
#include <bio.h>

char 	*defargv[] = {".", 0};
char	*fmt = "%q\n";
int	flag[256];
uint	dev ;
uint	type;
int	errors;
Biobuf	out;

void
warn(char *s)
{
	if(flag['f'] == 0)
		fprint(2, "find: %s: %r\n", s);
	errors = 1;
}

void
usage(void)
{
	fprint(2, "usage: find [-1dfq] [path ...]\n");
	exits("usage");
}

/*  if you think this scales you'd be wrong.  this is is 1/128th of a linear search.  */

enum{
	Ncache		= 128,		/* must be power of two */
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

int
seen(Dir *dir)
{
	int i;
	Fsig *f;
	Cache *c;

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
dskip(Dir *d)
{
	if(flag['1']){
		if(dev == 0 && type == 0){
			dev = d->dev;
			type = d->type;
		}
		if(d->dev != dev || d->type != type)
			return 0;
	}
	return 1;
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
	if(!flag['D'])
		Bprint(&out, fmt, name);
	for(; (n = dirread(fd, &buf)) > 0; free(buf))
		for(p = buf, e = p+n; p < e; p++){
			snprint(file, sizeof file, "%s/%s", name, p->name);
			if((p->qid.type&QTDIR) == 0 || !dskip(p)){
				if(!flag['d'])
					Bprint(&out, fmt, file);
			}else if(!skip(p))
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
	case 'D':
	case 'd':
	case 'f':
	case '1':
		flag[ARGC()] = 1;
		break;
	case 'q':
		fmt = "%s\n";
		break;
	default:
		usage();
	}ARGEND

	Binit(&out, 1, OWRITE);
	if(argc == 0)
		argv = defargv;
	for(; *argv; argv++){
		find(*argv);
		clearcache();
	}
	Bterm(&out);
	if(errors)
		exits("errors");
	exits("");
}


