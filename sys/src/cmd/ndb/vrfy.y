%{
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ip.h>

typedef struct T T;
struct T{
	T	*next;
	T	*down;
	char	*s;
	char	*t;
	int	line;
	char	*file;
};
#pragma	varargck	type	"T"	T*

Biobuf	in;
char	*file;
char	*ipmask;
int	line;
int	factotum;
T	*dblist;
T	*ll0;
T	**ll;
T	*tab0;
T	**tab;
int	fd;

extern	int	yyparse(void);
extern	void	yyerror(char*, ...);

#pragma	varargck	argpos	yyerror		1
#pragma	varargck	argpos	tokerror		2

char*
nonil(char *s)
{
	if(s == nil)
		return "";
	return s;
}

int
Tfmt(Fmt *f)
{
	int sharp;
	T *t, *t0;

	t0 = va_arg(f->args, T*);
	sharp = f->flags & FmtSharp;
	for(; t0 != nil; t0 = t0->down){
		if((t = t0) != nil)
			fmtprint(f, "%s=%s", t->s, nonil(t->t));
		while(t = t->next)
			fmtprint(f, " %s=%s", t->s, nonil(t->t));
		if(sharp == 0)
			break;
		if(t0->down != nil)
			fmtprint(f, "\n");
	}
	return 0;
}

T*
newt(char *s, char *t)
{
	T *x;

	x = malloc(sizeof *x);
	if(x == nil)
		sysfatal("malloc: %r");
	memset(x, 0, sizeof *x);
	x->s = s;
	x->t = t;
	x->line = line;
	x->file = file;
	return x;
}

static char *nakedtab[] = {
	"database",
	"soa",
	"trampok",
};

int
cknaked(char *s)
{
	int i;

	for(i = 0; i < nelem(nakedtab); i++)
		if(strcmp(nakedtab[i], s) == 0)
			return 0;
	yyerror("naked token %s", s);
	return -1;
}

%}

%union{
	T	*t;
	char	*s;
}

%type	<s>	rhs
%token	<s>	S
%start	db
%%
db	:
	| db '\n'
	| db tuples '\n'
	;
tuples:	tuple {
		*tab = ll0;
		tab = &ll0->down;

		ll0 = nil;
		ll = &ll0;
	}
	| tuple tuples
	;
tuple:	S rhs {
		T *x;

		if($2 == nil)
			cknaked($1);
		x = *ll = newt($1, $2);
		ll = &x->next;
	}
	;
rhs:		{ $$ = nil; }
	| '=' S	{ $$ = $2; }
	;
%%
void
freet(T *t)
{
	T *n, *d;

	for(; t != nil; t = d){
		d = t->down;
		for(; t != nil; t = n){
			n = t->next;
			free(t->s);
			free(t->t);
			free(t);
		}
	}
}

void
yyinit(char *f)
{
	file = f;
	line = 1;
	fd = open(file, OREAD);
	if(fd == -1)
		sysfatal("open: %r");
	if(Binit(&in, fd, OREAD) == -1)
		sysfatal("Binit: %r");
}

void
yyterm(void)
{
	freet(tab0);
	ll0 = nil;
	tab0 = nil;
	ll = &ll0;
	tab = &tab0;
}

void
yyerror(char *fmt, ...)
{
	char buf[256], *p, *e;
	va_list arg;
	static int nerrors;

	e = buf + sizeof buf;
	va_start(arg, fmt);
	p = seprint(buf, e, "%s:%d ", file, line);
	p = vseprint(p, e, fmt, arg);
	p = seprint(p, e, "\n");
	va_end(arg);
	write(2, buf, p - buf);
	nerrors++;
	if(nerrors > 5)
		sysfatal("too many errors");
}

void
tokerror(T *t, char *fmt, ...)
{
	char buf[256], *p, *e;
	va_list arg;

	e = buf + sizeof buf;
	va_start(arg, fmt);
	p = seprint(buf, e, "%s:%d ", t->file, t->line);
	p = vseprint(p, e, fmt, arg);
	p = seprint(p, e, "\n");
	va_end(arg);
	write(2, buf, p - buf);
}

enum{
	Base,
	Seeneq,
};

int
yylex(void)
{
	char buf[128], *s;
	int c, i;
	Rune r;
	static int state = Base;
	static int quoting;

	if(quoting != 0){
		yyerror("bad quote");
		quoting = 0;
	}
	i = 0;
again:
	c = Bgetrune(&in);
	if(c == Beof)
		return 0;
	r = c;
	switch(r){
	case '':
	case '\0':
		yyerror("bad character %#ux\n", r);
		goto again;
	case '#':
		for(;;){
			switch(Bgetrune(&in)){
			case Beof:
				yyerror("EOF in comment");
				return 0;
			case '\n':
				Bungetrune(&in);
				goto again;
			}
		}
	case '\n':
		if(state != Base){
			Bungetrune(&in);
			goto tok;
		}
		line++;
		switch(Bgetrune(&in)){
		case Beof:
			return '\n';
		case ' ':
		case '\t':
			goto again;
		}
		Bungetrune(&in);
		return '\n';
	case ' ':
	case '\t':
		if(state != Base){
			Bungetrune(&in);
			goto tok;
		}
		goto again;
	case '=':
		if(state != Base)
			break;
		state = Seeneq;
		return '=';
	default:
		break;
	}

	/* token */
	if(r == '"'){
		quoting = 1;
		c = Bgetrune(&in);
		r = c;
	}
	for(;;){
		if(i >= sizeof buf - UTFmax - 1){
			yyerror("token too long");
			break;
		}
		i += runetochar(buf + i, &r);
		c = Bgetrune(&in);
		if(c == Beof)
			break;
		r = c;
		switch(r){
		case '':
		case '\0':
			yyerror("bad character %#ux\n", r);
			goto tok;
		case ' ':
		case '\t':
			if(quoting)
				break;
		case '\n':
			Bungetrune(&in);
			goto tok;
		case '=':
			if(state == Base){
				Bungetrune(&in);
				goto tok;
			}
			break;
		case '\'':
			if(factotum)
				goto quote;
			break;
		case '"':
			if(!factotum)
			if(quoting == 1){
				quoting = 0;
				goto tok;
			}
			break; 
		case '#':
			yyerror("# in token");
		}
	}
tok:
	if(state == Base && i == 0)
		yyerror("name is nil");
	if(state == Seeneq)
		state = Base;
	buf[i] = 0;
	s = strdup(buf);
	if(s == nil)
		sysfatal("malloc: %r");
	yylval.s = s;
	return S;

quote:
	quoting = 1;
	for(;;){
		/* extra space for ' if quoting fails.  ndb is wierd */
		if(i >= sizeof buf - UTFmax - 1 - 1){
			yyerror("token too long");
			break;
		}
		i += runetochar(buf + i, &r);

		c = Bgetrune(&in);
		if(c == Beof)
			break;
		r = c;
		switch(r){
		case '\n':
			/* wierd!  this is actually what ndb does */
			Bungetrune(&in);
			memmove(buf, buf + 1, i);
			buf[0] = '\'';
			goto tok;
		case '\'':
			if(Bgetrune(&in) == '\'')
				break;
			Bungetrune(&in);
			goto tok;
		}
	}
	goto tok;
}

void
go(char *file)
{
	yyinit(file);
	yyparse();
	close(fd);
}

int
parseipxmask(uchar *ip, uchar *m, char *s0)
{
	uchar tmp[IPaddrlen];
	char *msk, s[4*8+4];

	snprint(s, sizeof s, "%s", s0);
	msk = strchr(s, '/');
	if(msk == nil){
		if(parseipmask(m, "/120") == -1)
			return -1;
	}else{
		if(parseipmask(m, msk) == -1)
			return -1;
		*msk = 0;
	}
	if(parseip(tmp, s) == -1)
		return -1;
	maskip(tmp, m, ip);
	return 0;
}

void
checkip(void)
{
	uchar m0[IPaddrlen], mask[IPaddrlen], ip1[IPaddrlen], m1[IPaddrlen];
	int havemsk;
	T *d, *t;

	havemsk = 0;
	if(ipmask != nil)
	if(parseipxmask(m0, mask, ipmask) == 0)
		havemsk = 1;
	for(d = tab0; d != nil; d = d->down)
		for(t = d; t != nil; t = t->next){
			if(strcmp(t->s, "ip") != 0)
				continue;
			if(parseip(ip1, t->t) == -1){
				tokerror(t, "bad ip %T", d);
				continue;
			}
			if(havemsk == 0)
				continue;
			maskip(ip1, mask, m1);
			if(ipcmp(m0, m1) != 0)
				tokerror(t, "wrong file: %I", ip1);
		}
}

typedef struct B B;
struct B {
	uchar	ea[IPaddrlen];
	T	*d[10];
	int	nd;
};

static	B	*btab;
static	uint	nbtab;
static	uint	nbtaba;

int
addrcmp(uchar *a, uchar *b)
{
	int d;

	d = memcmp(a, b, 6);
	if(d > 0)
		return 1;
	if(d < 0)
		return -1;
	return 0;
}

static B*
bsearch(uchar *ea)
{
	B *tab, *m;
	int n, i, d;

	tab = btab;
	for(n = nbtab; n > 0;){
		i = n/2;
		m = tab+i;
		d = addrcmp(m->ea, ea);
		if(d == 0)
			return m;
		if(d < 0){
			tab += i+1;
			n -= i+1;
		}else
			n = i;
	}
	return nil;
}

void
binsert(B *m, T *d, uchar *ea)
{
	char buf[256], *p, *e;
	int i;
	T **x;

	if(m == nil){
		if(nbtab + 1 >= nbtaba){
			nbtaba = nbtab + 1 << 1;
			btab = realloc(btab, sizeof btab[0]*nbtaba);
			if(btab == nil)
				sysfatal("realloc: %r");
		}
		for(m=btab+nbtab; m > btab && addrcmp(m[-1].ea, ea) > 0; m--)
			m[0] = m[-1];
		memset(m, 0, sizeof *m);
		m->nd = 0;
		memcpy(m->ea, ea, IPaddrlen);
		nbtab++;
	}
	if(m->nd < nelem(m->d))
		m->d[m->nd++] = d;
	if(m->nd > 1){
		x = m->d;
		p = buf;
		e = buf + sizeof buf;
		for(i = 0; i < m->nd - 1; i++)
			p = seprint(p, e, "%s:%d ", x[i]->file, x[i]->line);
		tokerror(d, "duplicate ea %E %s", m->ea, buf);
	}
}

void
prdb(void)
{
	int i;

	for(i = 0; i < nbtab; i++)
		print("%E\n", btab[i].ea);
}

void
checkether(void)
{
	uchar ea[IPaddrlen];
	T *d, *t;

	for(d = tab0; d != nil; d = d->down)
		for(t = d; t != nil; t = t->next){
			if(strcmp(t->s, "ether") != 0)
				continue;
			if(strspn(t->t, "0123456789abcdef") != 12){
				tokerror(t, "bad ea %s", t->t);
				continue;
			}
			parseether(ea, t->t);
			binsert(bsearch(ea), d, ea);
		}
}

static char *soaattr[] = {
	"refresh",
	"ttl",
	"ns",
	"mx",
	"pref",
	"mbox",
};

static char *cattrtab[] = {
	"clog",			/* only in consoledb */
	"console",
	"dev",
	"gid",
	"group",
	"speed",
	"uid",
	"openondemand",
};

static char *xattrtab[] = {
	"auth",
	"authdom",
	"bootf",
	"cname",
	"console",
	"database",
	"dns",
	"dnsdomain",
	"dom",
	"el",
	"ether",
	"file",
	"fs",
	"gw",
	"imap",
	"ip",
	"ipgw",
	"ipmask",
	"ipnet",
	"mb",
	"mbox",
	"mx",
	"ns",
	"pref",
	"proto",
	"refresh",
	"smtp",
	"soa",
	"sys",
	"trampok",
	"ttl",
	"txtrr",

	"pri",
	"srv",
	"weight",

	"gre",
	"il",
	"ipv4proto",
	"port",
	"protocol",
	"restricted",
	"tcp",
	"udp",

	"uid",
	"hostid"
};

char	**attrtab	= xattrtab;
int	nattrtab	= nelem(xattrtab);

void
checkattrs(void)
{
	int i;
	T *d, *t;

	for(d = tab0; d != nil; d = d->down)
		for(t = d; t != nil; t = t->next){
			for(i = 0; i < nattrtab; i++)
				if(strcmp(t->s, attrtab[i]) == 0)
					break;
			if(i == nattrtab)
				tokerror(t, "unknown attr %s", t->s);
		}
}

void
usage(void)
{
	fprint(2, "usage: ndb/vrfy [-m ip/mask]  [file ...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i;

	fmtinstall('T', Tfmt);
	fmtinstall('I', eipfmt);
	fmtinstall('M', eipfmt);
	fmtinstall('E', eipfmt);
	ARGBEGIN{
	case 'c':
		attrtab = cattrtab;
		nattrtab = nelem(cattrtab);
		break;
	case 'm':
		ipmask = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	yyterm();
	for(i = 0; i < argc; i++)
		go(argv[i]);
	checkip();
	checkether();
	checkattrs();
	yyterm();

	exits("");
}
