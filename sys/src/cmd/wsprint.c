#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mach.h>

typedef struct Linfo Linfo;
struct Linfo {
	Linfo	*next;

	char	*line;

	char	*type;			/* .type */
	uvlong	pc;			/* .pcs */
	int	count;			/* .ns */
	uvlong	maxwait;			/* .wait */
	uvlong	totalwait;		/* .total */

	char	sym[128];
	char	fn[128];
};

Linfo*
sortlinfo(Linfo *l)
{
	int i;
	Linfo *lnext, *l0, *l1, **ll;

	if(l == nil || l->next == nil)
		return l;

	/* cut list in halves */
	l0 = nil;
	l1 = nil;
	i = 0;
	for(; l; l=lnext){
		lnext = l->next;
		if(i++%2){
			l->next = l0;
			l0 = l;
		}else{
			l->next = l1;
			l1 = l;
		}
	}

	/* sort */
	l0 = sortlinfo(l0);
	l1 = sortlinfo(l1);

	/* merge */
	ll = &l;
	while(l0 || l1){
		if(l1==nil){
			lnext = l0;
			l0 = l0->next;
		}else if(l0==nil){
			lnext = l1;
			l1 = l1->next;
		}else if(l0->totalwait < l1->totalwait || l0->maxwait < l1->maxwait){
			lnext = l0;
			l0 = l0->next;
		}else{
			lnext = l1;
			l1 = l1->next;
		}
		*ll = lnext;
		ll = &(*ll)->next;
	}
	*ll = nil;
	return l;
}

Linfo*
readlinfo(int fd)
{
	char *f[6], *s;
	int n;
	Biobuf b;
	Linfo **ll, *l, *x;

	if(Binit(&b, fd, OREAD) == -1)
		sysfatal("wsprint: Binit: %r");

	l = nil;
	for(ll = &l;; ll = &x->next){
		for(;;){
			s = Brdstr(&b, '\n', 1);
			if(s == nil)
				goto done;
			x = malloc(sizeof *x);
			if(x == nil)
				goto done;
			memset(x, 0, sizeof *x);
			x->line = s;
			n = tokenize(x->line, f, nelem(f));
			if(n == 5)
				break;
			free(s);
			free(x);
		}
		x->type = f[0];
		x->pc = strtoull(f[1], nil, 0);
		x->count = strtoull(f[2], nil, 0);
		x->maxwait = strtoull(f[3], nil, 0);
		x->totalwait = strtoull(f[4], nil, 0);
		*ll = x;
	}
done:
	return l;
}

void
addsym(Linfo *l, char *kernel)
{
	int fd;
	uvlong bounds[2];
	Map *symmap;
	Fhdr fhdr;

	fd = open(kernel, OREAD);
	if(fd == -1){
		fprint(2, "addsym: open %s: %r\n", kernel);
		return;
	}
	symmap = loadmap(nil, fd, &fhdr);
	USED(symmap);
	if(crackhdr(fd, &fhdr) == 0){
		fprint(2, "crackheader: not a.out\n");
		close(fd);
		return;
	}
	if(syminit(fd, &fhdr) == -1){
		fprint(2, "syminit: %r");
		close(fd);
		return;
	}
	for(; l != nil; l = l->next){
		if(fileline(l->sym, sizeof l->sym, l->pc) == 0)
			l->sym[0] = 0;
		if(fnbound(l->pc, bounds) == 1)
		if(symoff(l->fn, sizeof l->fn-3, bounds[0], CTEXT) > 0)
			strcat(l->fn, "()");
	}
	close(fd);
}

void
printlinfo(Biobuf *o, Linfo *l)
{
	for(; l != nil; l = l->next)
		Bprint(o, "%s	%.16llux	%11d	%16lld	%16lld	// %s %s\n",
			l->type, l->pc, l->count, l->maxwait, l->totalwait, l->sym, l->fn);
}
		
void
freelinfo(Linfo *l)
{
	Linfo *n;

	for(; l != nil; l = n){
		n = l->next;
		free(l->line);
		free(l);
	}
}

void
usage(void)
{
	fprint(2, "usage: wsprint [-t] kernel < /dev/wsdata\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	uchar flag[0x80];
	Linfo *l;
	Biobuf o;

	ARGBEGIN{
	case 't':
		flag[ARGC()] = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();
	if(Binit(&o, 1, OWRITE) == -1)
		sysfatal("wsprint: Binit: %r");
	if(flag['t'])
		Bprint(&o, "ltype\tpc\t\tcount\t\tmax wait\t\ttot wait\t\tsym\n");

	l = readlinfo(0);
	l = sortlinfo(l);
	addsym(l, argv[0]);
	printlinfo(&o, l);
	freelinfo(l);

	Bterm(&o);
	exits("");
}
