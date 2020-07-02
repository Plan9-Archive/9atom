#include <u.h>
#include "goo.h"
#include "snap.h"

int pfile[] = {
[Pregs]		252,
[Pmem]		253,
[Psegment]	254,
};

int fakey[] = {
[Pfpregs]		108,
};

char *pname[] = {
[Psegment]	"segment",
[Pfd]		"fd",
[Pfpregs]	"fpregs",
[Pkregs]		"kregs",
[Pnoteid]	"noteid",
[Pns]		"ns",
[Pproc]		"proc",
[Pregs]		"regs",
[Pstatus]	"status",
[Npfile]		"<bug>",
[Pmem]		"mem",
};

/* research 32-bit crc.  good enough. */
static ulong
sumr(ulong sum, void *buf, int n)
{
	ushort *s, *send;

	if(buf == 0)
		return sum;
	for(s = buf, send = s+n/2; s < send; s++)
		if(sum & 1)
			sum = ((sum>>1)+*s+0x80000000);
		else
			sum = ((sum>>1)+*s);
	return sum;
}

static int npage;
static Page *pgtab[1<<10];
static char zp[1024];

/*
 * hopefully we should need reread less than 5%.
 * otherwise our footprint will be gigabytes
 */
int
recheck(Page *pg, char *p, long len)
{
	int fd, r;
	char *buf;

print("recheck!\n");
	fd = aoeopen(pfile[pg->pfile]);
print("recheck\n");
	if(fd == -1)
		sysfatal("recheck: open %r");
	buf = emalloc(len);
	if(aoepread(fd, buf, len, pg->offset) != len)
		sysfatal("aoereadp: short read: %r");
	r = memcmp(buf, p, len);
	aoeclose(fd);
	return r;
}

Page*
datapage(int pfile, char *p, long len, uvlong offset)
{
	Page *pg;
	long sum, psum;
	int iszero;

	if(len > Pagesize)
		sysfatal("datapage cannot handle pages > 1024");

	sum = sumr(0, p, len);
	if(sum == 0 && memcmp(p, zp, len) == 0)
		iszero = 1;
	else
		iszero = 0;
	psum = sum;
	psum &= nelem(pgtab)-1;

	for(pg = pgtab[psum]; pg; pg = pg->link)
		if(pg->len == len && pg->sum == sum)
		if(iszero || !recheck(pg, p, len))
			return pg;

//	pg = emalloc(sizeof *pg+len);
	pg = emalloc(sizeof *pg);
	pg->type = 0;
	pg->len = len;
	pg->offset = offset;
	pg->pfile = pfile;

//	memmove(pg->data, p, len);
	pg->link = pgtab[psum];
	pgtab[psum] = pg;
	if(iszero) {
		pg->type = 'z';
		pg->written = 1;
	}
	++npage;

	return pg;
}

void
writeseg(Biobuf *b, Seg *s)
{
	Bprint(&b->h, "%-11llud %-11llud ", s->offset, s->len);
}

void
writepage(Biobuf *b, Proc *proc, Seg *s, Page *p, char *buf)
{
	int type;

	type = proc->text ==  s ? 't' : 'm';
	if(p->written){
		if(p->type == 'z'){
			Bprint(&b->h, "z");
			return;
		}
		Bprint(&b->h, "%c%-11ld %-11llud ", p->type, p->pid, p->offset);
		return;
	}
	Bprint(&b->h, "r");
	Bwrite(&b->h, buf, p->len);
	p->written = 1;
	p->type = type;
	p->pid = proc->pid;
}

static Data*
readsection(int section)
{
	char buf[8192];
	int n, fd;
	int hdr, tot;
	Data *d;

	if((fd = aoeopen(pfile[section])) == -1)
		return 0;
	d = 0;
	tot = 0;
	hdr = (int)((Data*)0)->data;
	while((n = aoepread(fd, buf, sizeof buf, tot)) > 0) {
		d = erealloc(d, tot+n+hdr);
		memmove(d->data+tot, buf, n);
		tot += n;
	}
	aoeclose(fd);
	if(d == 0)
		return 0;
	d->len = tot;
	return d;
}

static void
readwseg(Biobuf *b, Proc *proc, int fd, vlong off, ulong len, char *name)
{
	char buf[Pagesize];
	Page *pg;
	int npg;
	Seg *s;
	ulong i;
	int n;

	s = emalloc(sizeof *s);
	s->name = estrdup(name);
	s->offset = off;
	s->len = len;
	writeseg(b, s);

//	pg = emalloc((sizeof *pg*len)/Pagesize);
	npg = 0;
	for(i=0; i<len; ) {
		n = Pagesize;
		if(n > len-i)
			n = len-i;
//print("aoepread(%d, %lld)\n", n, off+npg*Pagesize);
		if((n = aoepread(fd, buf, n, off+npg*Pagesize)) <= 0)
			break;
		pg = datapage(fd, buf, n, off+npg*Pagesize);
		npg++;
		i += n;
		writepage(b, proc, s, pg, buf);
		if(n != Pagesize)	/* any short read, planned or otherwise */
			break;
	}

	if(i != len)
		sysfatal("bad segment %s: %uld != %uld", s->name, len, i);

	s->pg = 0;
	s->npg = npg;
	free(s);
}

Proc*
snapw(Biobuf *b, long pid)
{
	Data *d;
	Proc *proc;
	Seg **s;
	char *name, *segdat, *q, *f[128+1];
	int fd, i, nf, np;
	long n;
	uvlong off, len;

	shelf = pid;
	proc = emalloc(sizeof(*proc));
	proc->pid = pid;

	np = 0;
	for(i=0; i<nelem(pfile); i++) {
		if(pfile[i] == 0 || i == Pmem)
			continue;
		d = proc->d[i] = readsection(i);
		if(!d){
			fprint(2, "warning: can't include shelf %ld %s\n", pid, pname[i]);
			continue;
		}
		np++;
		Bprint(&b->h, "%-11ld %s\n%-11lud ", proc->pid, pname[i], d->len);
		Bwrite(&b->h, d->data, d->len);
	}
	/* add fakey sagments because acid pouts without */
	for(i=0; i<nelem(fakey); i++) {
		if((n = fakey[i]) == 0)
			continue;
		q = malloc(n);
		memset(q, '0', n);
		Bprint(&b->h, "%-11ld %s\n%-11lud ", proc->pid, pname[i], n);
		Bwrite(&b->h, q, n);
		free(q);
	}
	if(np == 0)
		return 0;

	if(!(d=proc->d[Psegment])) {
		fprint(2, "warning: no segment table, no memory image\n");
		return proc;
	}

	segdat = emalloc(d->len+1);
	memmove(segdat, d->data, d->len);
	segdat[d->len] = 0;

	nf = getfields(segdat, f, nelem(f), 1, "\n");
	if(nf == nelem(f)) {
		nf--;
		fprint(2, "shelf %ld has >%d segments; only using first %d\n",
			pid, nf, nf);
	}
	if(nf <= 0) {
		fprint(2, "warning: couldn't understand segment table, no memory image\n");
		free(segdat);
		return proc;
	}
	if((fd = aoeopen(pfile[Pmem])) == -1) {
		fprint(2, "warning: can't open memory image shelf %ld\n", pid);
		return proc;
	}

	Bprint(&b->h, "%-11ld mem\n%-11d ", proc->pid, nf);
	s = emalloc(nf*sizeof(*s));
	for(i=0; i<nf; i++) {
		if(q = strchr(f[i], ' ')) 
			*q = 0;
		name = f[i];
		off = strtoull(name+10, &q, 16);
		len = strtoull(q, &q, 16) - off;
		readwseg(b, proc, fd, off, len, name);
	}
	proc->nseg = nf;
	proc->seg = s;

	aoeclose(fd);
	return proc;
}
