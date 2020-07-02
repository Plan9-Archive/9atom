/*
 *  convert ndb to zone
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <regexp.h>
#include <ip.h>

typedef struct Rrtab Rrtab;
typedef struct Soatab Soatab;
typedef struct Re Re;
typedef struct Retab Retab;

struct Rrtab {
	char		*s;
	Ndbtuple	*rr;
};

struct Re {
	Reprog		*re;
	char		*s;
	char		flag;
};

struct Retab {
	Re	*r;
	int	nre;
	int	nalloc;
};

struct Soatab {
	char		*s;
	Re		*re;
	Ndbtuple	*soa;
	uchar		ip[IPaddrlen];
	uchar		m[IPaddrlen];
	int		bits;
	Rrtab		*rr;
	int		nrr;
	int		arr;
};

enum {
	Fexact	= 1<<0,
	Fneg	= 1<<1,
};

static	int	flag[127];
static	Biobuf	bout;
static	Soatab	soatab[128];
static	int	soaidx;
static	ulong	soamtime;

#define	dprint(...)	if(flag['d']) fprint(2, __VA_ARGS__); else {}
#pragma	varargck	type	"t"	char*

void
usage(void)
{
	fprint(2, "usage: ndb/tozone [-dg] [-f ndbfile] [zone ...]\n");
	exits("usage");
}

/* there is no way to get " or \n in an ndb rr */
static int
fmtzonequote(Fmt *f)
{
	char *s;
	uchar buf[IPaddrlen];

	s = va_arg(f->args, char*);
	if(strpbrk(s, " \t") == nil){
		if(parseip(buf, s) == -1)
			return fmtprint(f, "%s.", s);
		else
			return fmtprint(f, "%s", s);
	}else
		return fmtprint(f, "\"%s\"", s);
}

static int
fmttabpad(Fmt *f)
{
	char *s;
	int l, p;

	l = strlen(s = va_arg(f->args, char*));
	p = f->prec;
	fmtprint(f, "%s", s);
	for(; l < p; l += 8)
		fmtprint(f, "\t");
	return 0;
}

Ndb*
ndbopen1(char *s)
{
	Dir *d;
	Ndb *db;

	db = ndbopen(s);
	if(db && (d = dirstat(s))){
		if(d->mtime > soamtime)
			soamtime = d->mtime;
		free(d);
	}
	return db;
}

Ndbtuple*
ndbfcopy0(Ndbtuple *from, char **okrr, int nok)
{
	Ndbtuple *first, *to, *last, *line;
	int newline, i;

	newline = 1;
	last = nil;
	first = nil;
	line = nil;
	for(; from != nil; from = from->entry){
		for(i = 0; i < nok; i++)
			if(strcmp(from->attr, okrr[i]) == 0)
				break;
		if(nok > 0 && i == nok){
			newline = from->line != from->entry;
			continue;
		}
		to = ndbnew(from->attr, from->val);
		if(newline)
			line = to;
		else
			last->line = to;

		if(last != nil)
			last->entry = to;
		else {
			first = to;
			line = to;
		}
		to->entry = nil;
		to->line = line;
		last = to;
		newline = from->line != from->entry;
	}
	ndbsetmalloctag(first, getcallerpc(&from));
	return first;
}

Ndbtuple*
ndbcopy(Ndbtuple *from)
{
	return ndbfcopy0(from, nil, 0);
}

static char *glue[] = {
	"ip",
	"ns",
};

Ndbtuple*
ndbfcopy(Ndbtuple *from)
{
	return ndbfcopy0(from, glue, nelem(glue));
}

enum {
	Retry,
	Expire,
	Minttl,
	Refresh,
	Serial,
	Mb,
};
char *nametab[] = {
	"retry",
	"expire",
	"minttl",
	"refresh",
	"serial",
	"mb",
};
char *deftab[] = {
	"3600",
	"86400",
	"3600",
	"3600",
	nil,
	nil,
};
char *rrtab[] = {
	"ns",
	"cname",
	"mx",
	"txtrr",
	"ip",
	"aaaa",
	"a6",
	"ptr",
};
char *rrntab[] = {
	"ns",
	"cname",
	"mx",
	"txt",
	"a",
	"aaaa",
	"a6",
	"ptr",
};
int rrorigin[] = {
	1,
	1,
	1,
	0,
	0,
	0,
	0,
	0,
};

void
origin(char *dom, int domsz, char *zone, char *origin, int inzone)
{
	char *p;
	int d;

	p = strstr(zone, origin);
	if(p != nil && (p[strlen(origin)] != 0 || p > zone && p[-1] != '.'))
		p = nil;
	if(p == nil){
		if(inzone)
			sysfatal("%s not subdomain of %s", zone, origin);
		snprint(dom, domsz, "%q", zone);
		return;
	}else
		d = p - zone - 1;
	if(d >= domsz || d == 0)
		sysfatal("bad zone: %s", zone);
	if(d == -1){
		d = 1;
		zone = "@";
	}
	memcpy(dom, zone, d);
	dom[d] = 0;
}

void
prsoa(Biobuf *b, Soatab *t)
{
	char *tab[Mb + 1], buf[12], dom[128], nm[128], mx[128], val[128];
	int i, j, l, lmax;
	Ndbtuple *nt, *pt;

	memcpy(tab, deftab, sizeof deftab);
	for(nt = t->soa; nt; nt = nt->entry)
		for(i = 0; i < nelem(nametab); i++)
			if(strcmp(nt->attr, nametab[i]) == 0)
				tab[i] = nt->val;
	if(tab[Serial] == nil){
		snprint(buf, sizeof buf, "%lud", soamtime);
		tab[Serial] = buf;
	}
	if(tab[Mb] == nil){
		snprint(nm, sizeof nm, "netmaster@%s", t->s);
		tab[Mb] = nm;
	}

	Bprint(b, "$ORIGIN %q\n", t->s);
	origin(dom, sizeof dom, t->s, t->s, 1);
	Bprint(b, "%s\t" "soa %s. %s. (\n", dom, t->s, tab[Mb]);
	Bprint(b, "\t\t\t" "%s\n", tab[Serial]);
	Bprint(b, "\t\t\t" "%s\n", tab[Refresh]);
	Bprint(b, "\t\t\t" "%s\n", tab[Retry]);
	Bprint(b, "\t\t\t" "%s\n", tab[Expire]);
	Bprint(b, "\t\t\t" "%s\n", tab[Minttl]);
	Bprint(b, "\t\t" ")\n");
	for(i = 0; i < nelem(rrtab); i++)
		for(nt = t->soa; nt; nt = nt->entry)
			if(strcmp(nt->attr, rrtab[i]) == 0){
				if(strcmp(nt->attr, "mx") == 0){
					if(pt = nt->entry)
					if(strcmp(pt->attr, "pref") == 0){
						origin(mx, sizeof mx, nt->val, t->s, 0);
						Bprint(b, "\t\t" "mx\t" "%s %s\n", pt->val, mx);
					}
				}else if(rrorigin[i]){
					origin(dom, sizeof dom, nt->val, t->s, 0);
					Bprint(b, "\t\t" "%s\t" "%s\n", rrntab[i], dom);
				}else
					Bprint(b, "\t\t" "%s\t" "%q\n", rrntab[i], nt->val);
			}
	lmax = 1;
	for(j = 0; j < t->nrr; j++){
		origin(dom, sizeof dom, t->rr[j].s, t->s, 1);
		l = strlen(dom);
		if(l > lmax)
			lmax = l;
	}
//	lmax += 8;
	lmax -= lmax%8;
	for(j = 0; j < t->nrr; j++){
		origin(dom, sizeof dom, t->rr[j].s, t->s, 1);
		for(i = 0; i < nelem(rrtab); i++)
			for(nt = t->rr[j].rr; nt; nt = nt->entry)
				if(strcmp(nt->attr, rrtab[i]) == 0){
					origin(val, sizeof val, nt->val, t->s, 0);
					Bprint(b, "%.*t" "%s\t" "%s\n", lmax+8, dom, rrntab[i], val);
				}
	}
}

char*
lower(char *s)
{
	char *p, c;

	for(p = s; c = *p; p++)
		if(c >= 'A' && c <= 'Z')
			*p = c - ('A' - 'a');
	return s;
}

int
iregex(Re *r, char *val, int fmask)
{
	char *s;
	int m, f;

	s = lower(strdup(val));
	f = r->flag & ~fmask;
	if(f & Fexact)
		m = strcmp(r->s, s) == 0;
	else
		m = regexec(r->re, s, 0, 0);
	free(s);
	if(f & Fneg)
		m = !m;
	return m;
}

Re*
iregexes(Retab *r, char *val, int fmask)
{
	int i;

	for(i = 0; i < r->nre; i++)
		if(iregex(r->r + i, val, fmask))
			return r->r + i;
	return nil;
}

void*
erealloc(void *a, ulong sz)
{
	if((a = realloc(a, sz)) == nil)
		sysfatal("realloc: %r");
	return a;
}

void
addre(Retab *r, char *s, int flag)
{
	char rebuf[128];
	int i;

	if(r->nre == r->nalloc){
		r->r = erealloc(r->r, (r->nalloc += 10)*sizeof r->r[0]);
		memset(r->r + r->nre, 0, 10*sizeof r->r[0]);
	}
	snprint(rebuf, sizeof rebuf, "%s$", s);
	for(i = r->nre; i > 0 && strlen(s) > strlen(r->r[i - 1].s); i--)
		r->r[i] = r->r[i - 1];
	r->r[i].re = regcomp(rebuf);
	if(r->r[i].re == nil)
		sysfatal("regcomp: %r");
	r->r[i].s = strdup(s);
	if(r->r[i].s == nil)
		sysfatal("strdup: %r");
	r->r[i].flag = flag;
	r->nre++;
}

void
freeretab(Retab *r)
{
	int i;

	for(i = 0; i < r->nre; i++){
		free(r->r[i].re);
		free(r->r[i].s);
	}
	free(r->r);
}

static char*
bitprint(char *p, char *e, char *fmt, uint i, int, int bits)
{
	if(bits > 8)
		bits = 8;
	if(bits > 0)
		p = seprint(p, e, fmt, i & (1<<bits) - 1);
	return p;
}

static void
revipmask(char *buf, int bufsz, char *ip, int bits)
{
	char *p, *e;
	uchar a[IPaddrlen];
	int i;

	p = buf;
	e = p + bufsz;
	if(parseip(a, ip) == 6){
		for(i = 0; i < 16; i++){
			bits = 128 - bits;
			p = bitprint(p, e, "%x.", a[IPaddrlen - i - 1]>>0, 4, bits);
			bits -= 4;
			p = bitprint(p, e, "%x.", a[IPaddrlen - i - 1]>>4, 4, bits);
			bits -= 4;
		}
	}else{
		bits = 32 - bits;
		for(i = 0; i < 4; i++){
			p = bitprint(p, e, "%d.", a[IPaddrlen - i - 1]>>0, 4, bits);
			bits -= 8;
		}
	}
	if(p > buf && p[-1] == '.')
		p--;
	p[0] = 0;
}

int
issoa(Ndbtuple *t)
{
	Ndbtuple *nt;

	for(nt = t; nt; nt = nt->entry)
		if(strcmp(nt->attr, "soa") == 0)
			return 1;
	return 0;
}

Soatab*
addrr(Soatab *s, char *v, Ndbtuple *t, int f)
{
	int j;

	dprint("%d: rr %s %s\n", f, s->s, v);
	for(j = 0; j < s->nrr; j++)
		if(strcmp(s->rr[j].s, v) == 0)
			goto found;
	if(j >= s->arr){
		s->rr = erealloc(s->rr, (s->arr += 20)*sizeof *s->rr);
		memset(s->rr + j, 0, sizeof s->rr[j]*20);
	}
	s->nrr++;
	s->rr[j].s = strdup(v);
found:
	if(f && strcmp(v, s->s) != 0)
		s->rr[j].rr = ndbconcatenate(s->rr[j].rr, ndbfcopy(t));
	else
		s->rr[j].rr = ndbconcatenate(s->rr[j].rr, ndbcopy(t));
	return s;
}

int
ipmatch(uchar *a, uchar *b, uchar *mask)
{
	uchar m[2][IPaddrlen];

	maskip(a, mask, m[0]);
	maskip(b, mask, m[1]);
	return memcmp(m[0], m[1], IPaddrlen) == 0;
}

void
addip(Soatab *s, char *v, char *val, int is6)
{
	char rev[256], r2[256];
	int bits;
	Ndbtuple *rr;

	bits = s->bits;
	if(!is6)
		bits -= 96;
	revipmask(rev, sizeof rev, val, bits);
	snprint(r2, sizeof r2, "%s.%s", rev, s->s);
	rr = ndbnew("ptr", v);
	addrr(s, r2, rr, 0);
	ndbfree(rr);
}

void
lookuprev(char *v, Ndbtuple *t)
{
	uchar a[IPaddrlen];
	int i, is6;
	Ndbtuple *nt;
	Soatab *s;

	for(nt = t; nt; nt = nt->entry)
		if(strcmp(nt->attr, "ip") == 0){
			is6 = parseip(a, nt->val) == 6;
			for(i = 0; i < soaidx; i++){
				s = soatab + i;
				if(s->bits >= 0)
				if(ipmatch(s->ip, a, s->m))
					addip(s, v, nt->val, is6);
			}
		}
}

static int
domtoip(uchar *a, uchar *m, char *ip)
{
	char *p, *r, *s, buf[8];
	int l, i, ip6, rem, bits, bits0, step, base, stop;

	memset(a, 0, IPaddrlen);
	memset(m, 0, IPaddrlen);
	l = strlen(ip);
	if(l >= 13 && cistrcmp(ip + l - 13, ".in-addr.arpa") == 0){
		ip6 = 0;
		step = 8;
		base = 0;
		stop = 96;
	}else if (l >= 9 && cistrcmp(ip + l - 9, ".ip6.arpa") == 0){
		ip6 = 1;
		step = 4;
		base = 0xf;
		stop = 0;
	}else
		return -1;
	bits = rem = 0;
	s = ip;
	if((p = strchr(ip, '/')) && p < strchr(ip, '.')){
		strtoul(ip, &r, 0);
		if(r == p){
			rem = strtoul(ip, &r, 0);
			bits = strtoul(p + 1, &s, 0);
			if(*s)
				s++;
		}
		bits += 96;
	}else{
		i = 0;
		for(p = ip; p = strchr(p, '.'); p++)
			i++;
		if(!ip6 && strchr(ip, '/'))
			i--;
		bits = step*(i - 1) + stop;
	}
	if(rem){
		i = bits%step;
		if(step == 4 && bits%8 == 0)
			rem <<= 4;
		a[i/8] |= rem;
	}
	bits0 = bits;
	bits -= bits%8;
	for(; bits >= stop && *s; ){
		i =  strtoul(s, &s, base);
		if(*s == '/'){
			strtoul(s + 1, &s, 0);
			if(*s)
				s++;
			continue;
		}
		if(*s)
			s++;
		if(step == 4 && bits%8 == 0)
			i <<= 4;
		a[bits/8 - 1] |= i;
		bits -= step;
	}
	if(ip6 == 0)
		a[10] = a[11] = 0xff;
	if(bits != stop - 8)
		sysfatal("rev zone %s missing %d bits\n", ip, bits - stop + 8);
	snprint(buf, sizeof buf, "/%d", bits0);
	if(parseipmask(m, buf) == bits0)
		sysfatal("parseipmask: %r");
	return bits0;
}

Soatab*
lookup0(Retab *r, char *v, Ndbtuple *t, int f)
{
	int i;
	Soatab *s;
	Re *m;

	if(f == 0)
		lookuprev(v, t);
	if((m = iregexes(r, v, Fneg)) == nil)
		return nil;
	for(i = 0; i < soaidx; i++)
		if(cistrcmp(soatab[i].s, v) == 0){
			s = soatab + i;
			dprint("%d: concat %s\n", f, s->s);
			s->soa = ndbconcatenate(s->soa, ndbcopy(t));
			return s;
		}else if(m == soatab[i].re)
			return addrr(soatab + i, v, t, f);
	if(soaidx == nelem(soatab))
		sysfatal("too many doms");
	s = soatab + soaidx;
	soaidx++;
	dprint("%d: new %s\n", f, v);
	s->soa = ndbconcatenate(s->soa, ndbcopy(t));
	s->s = strdup(v);
	s->re = m;
	s->bits = domtoip(s->ip, s->m, s->s);
	return s;
}

char*
match(char *z, char *s, Ndbtuple*)
{
	char *p;

	if((p = cistrstr(s, z)) > s && p[-1] == '.')
		return p - 1;
	return 0;
}

int
isglue(char *z, char *s, Ndbtuple *t)
{
	char *d, *m;

	m = match(z, s, t);
	if((d = strchr(s, '.')) && d < m)
		return 1;
	if(d <= m && issoa(t))
		return 1;
	return 0;
}

Soatab*
lookup(Retab *r, char *v, Ndbtuple *t, int f)
{
	int i;
	Soatab *s;

	s = lookup0(r, v, t, f);
	if(!flag['g'])
		for(i = 0; i < soaidx - 1; i++)
			if(isglue(soatab[i].s, v, t)){
				dprint("glue zone=%s %s\n", soatab[i].s, v);
				addrr(soatab + i, v, t, -1);
			}
	return s;
}

/* equivalent to ndb/requery -f /lib/ndb/external soa 're1|...|ren' dom */
void
soas0(char *dbfile, Ndb *db, Retab *r0, Retab *r1, int f)
{
	int m;
	Ndb *p;
	Ndbtuple *nt, *t;

	for(; t = ndbparse(db); ndbfree(t)){
		/* why doesn't ndb do this for me? */
		if(strcmp(t->attr, "database") == 0){
			for(nt = t; nt; nt = nt->entry)
				if(strcmp(nt->attr, "file") == 0)
				if(strcmp(nt->val, dbfile) != 0){
					p = ndbopen1(nt->val);
					if(p == nil)
						sysfatal("bad ndb file: %s\n", nt->val);
					soas0(nt->val, p, r0, r1, f);
					ndbclose(p);
				}
		}
		m = 0;
		if(issoa(t))
			m = 1;
		for(nt = t; nt; nt = nt->entry)
			if(strcmp(nt->attr, "dom") == 0){
				if(f == 2 && m && iregexes(r0, nt->val, 0))
					addre(r1, nt->val, 0);
				else if(f == 2 && m)
					addre(r1, nt->val, Fneg);
				else if(f == 1 && m)
					lookup(r0, nt->val, t, f);
				else if(f == 0 && !m)
					lookup(r0, nt->val, t, f);
			}
	}
}

void
soas(char *dbfile, Ndb *db, char **re, int nre)
{
	int i;
	Retab r0, r1;

	memset(&r0, 0, sizeof r0);
	memset(&r1, 0, sizeof r1);
	if(nre == 0)
		addre(&r0, ".*", 0);
	else for(i = 0; i < nre; i++)
		addre(&r0, re[i], Fexact);
	soas0(dbfile, db, &r0, &r1, 2);
//for(int i=0; i<r1.nre; i++)print("  %s	%x\n", r1.r[i].s, r1.r[i].flag);
	ndbreopen(db);
	soas0(dbfile, db, &r1, nil, 1);
	ndbreopen(db);
	soas0(dbfile, db, &r1, nil, 0);
	freeretab(&r0);
	freeretab(&r1);
}

int
alphacmp(void *v1, void *v2)
{
	char *f[2][25], *v[2];
	int i, n[2], bias;
	Rrtab *r[2];

	r[0] = v1;
	r[1] = v2;
	for(i = 0; i < 2; i++){
		v[i] = strdup(r[i]->s);
		n[i] = gettokens(v[i], f[i], nelem(f[0]), ".");
	}
	bias = 0;
	for(i = 0;; i++){
		if(i == n[0] || i == n[1]){
			if(n[0] != n[1])
				bias = n[1] - n[0];
			free(v[0]);
			free(v[1]);
			return bias;
		}
		bias = cistrcmp(f[0][n[0] - i - 1], f[1][n[1] - i - 1]);
	}
}

int
ipaddrcmp(void *v1, void *v2)
{
	uchar m[2][IPaddrlen], ip[2][IPaddrlen];
	int i, r;
	Rrtab *a, *b;

	a = v1;
	b = v2;
	domtoip(ip[0], m[0], a->s);
	domtoip(ip[1], m[1], b->s);
	for(i = 0; i < IPaddrlen; i++)
		if(r = ip[0][i] - ip[1][i])
			return r;
	return 0;
}

void
soasort(Soatab *t)
{
	int (*f)(void*, void*);

	f = alphacmp;
	if(t->bits > 0)
		f = ipaddrcmp;
	qsort(t->rr, t->nrr, sizeof t->rr[0], f);
}

void
main(int argc, char **argv)
{
	char *dbfile;
	int i;
	Ndb *db;

	dbfile = "/lib/ndb/local";
	ARGBEGIN{
	case 'f':
		dbfile = EARGF(usage());
		break;
	case 'd':
	case 'g':
		flag[ARGC()] = 1;
		break;
	default:
		usage();
	}ARGEND;

	fmtinstall('q', fmtzonequote);
	fmtinstall('t', fmttabpad);
	if(Binit(&bout, 1, OWRITE) == -1)
		sysfatal("Binit: %r");
	db = ndbopen1(dbfile);
	if(db == nil)
		sysfatal("%s: no db files\n", argv0);
	soas(dbfile, db, argv, argc);
	ndbclose(db);
	for(i = 0; i < soaidx; i++){
		if(soatab[i].re->flag & Fneg)
			continue;
		Bprint(&bout, "$TTL 3600\n");
		soasort(soatab + i);
		prsoa(&bout, soatab + i);
	}
	Bterm(&bout);
	exits(0);
}
