#include <u.h>
#include <libc.h>

#define	dprint(...)	if(flagd)print(__VA_ARGS__);else{}
#pragma	varargck	type	"»"	vlong
#pragma	varargck	type	"»"	uvlong

enum {
	Nprec	= 10,
};

int	flagv;
int	flagd;
vlong	defm	= 1024;
static	char	power[]	= "kmgtpezy";

vlong	aexpr0(char**, int, int);

int
fmt»(Fmt *f)
{
	int i, rollup, n;
	uvlong u, m, bit;

	u = va_arg(f->args, uvlong);
	rollup = f->flags & FmtSharp;

	m = 0;
	n = 0;
	for(i = sizeof u * 8 - 1; i >= 0; i--){
		bit = u & 1ull<<i;
		if(bit)
			m = m*2 | 1;
		if(i==0 || (!rollup || bit == 0) && m){
			if(n++ > 0 && m)
				fmtprint(f, " | ");
			if(m > 0)
				fmtprint(f, "%llux", m << i+(bit==0));
			else if(n == 1)
				fmtprint(f, "0");
			m = 0;
		}
	}
	return 0;
}

int
fmt«(Fmt *f)
{
	int i, rollup, n;
	uvlong u, m, bit;

	u = va_arg(f->args, uvlong);
	rollup = f->flags & FmtSharp;

	m = 0;
	n = 0;
	for(i = sizeof u * 8 - 1; i >= 0; i--){
		bit = u & 1ull<<i;
		if(bit)
			m = m*2 | 1;
		if(i==0 || (!rollup || bit == 0) && m){
			if(n++ > 0 && m)
				fmtprint(f, " | ");
			if(m > 0)
				fmtprint(f, "%llux<<%ud", m, i+(bit==0));
			else if(n == 1)
				fmtprint(f, "0");
			m = 0;
		}
	}
	return 0;
}

int
fmtm(Fmt *f)
{
	int i, exact;
	uvlong u, d;

	u = va_arg(f->args, uvlong);
	d = 1024;
	if(f->flags & FmtWidth)
		d = f->width;
	exact = (f->flags & FmtSharp) == 0;
	i = 0;
	for(; !exact && u > 10*d || u>=d && u%d == 0; u /= d)
		i++;
	if(i>0)
		return fmtprint(f, "%lld%c", u, power[i-1]);
	return fmtprint(f, "%lld", u);
}

static int
chartolrune(Rune *r, char *p)
{
	int n, m;

	m = 0;
	for(;; p += n){
		n = chartorune(r, p);
		if(*r == 0)
			return m;
		if(isspacerune(*r)){
			m += n;
			continue;
		}
		*r = tolowerrune(*r);
		return n+m;
	}
}

vlong
atosize(char **s)
{
	char *p, *q;
	int c, n;
	Rune r;
	uvlong v, m;

	p = *s;
	/* lame; need better rule */
	if(*p == 0 && p[1] == 'B' || p[1] == 'b')
		v = strtoll(p+2, &p, 2);
	else
		v = strtoll(p, &p, 0);
	if(p == *s)
		sysfatal("bad number %s", p);
	n = chartolrune(&r, p);
	c = 0;
	m = defm;
	if(r != 0 && r < Runeself && (q = strchr(power, r))){
		c = q - power + 1;
		n = chartolrune(&r, p+=n);
	}
	if(r == 'i'){
		m = 1024;
		n = chartolrune(&r, p+=n);
	}
	if(r == 'b')
		p+=n;
	while(c--)
		v *= m;
	*s = p;
	return v;
}

vlong
atonumber(char **s)
{
	char *s0;
	uvlong start, stop, i, r;

	s0 = *s;
	start = atosize(s);
	if(**s != ':')
		return start;
	(*s)++;
	stop = atosize(s);
	if(start > 63 || stop > 63)
		sysfatal("bad bit range: %s\n", s0);
	if(start > stop){
		i = stop;
		stop = start;
		start = i;
	}
	r = 0;
	for(i = start; i <= stop; i++)
		r |= 1ull<<i;
	return r;
}

vlong
paren(char **s)
{
	int n;
	vlong v;
	Rune r;

	n = chartolrune(&r, *s);
	switch(r){
	case '(':
		*s += n;
		v = aexpr0(s, Nprec-1, 0);
		n = chartolrune(&r, *s);
		if(r != ')')
			sysfatal("expecting ')'");
		*s += n;
		return v;
	case '~':
		*s += n;
		return ~paren(s);
//	case '-':
//		*s += n;
//		return -paren(s);
	case '!':
		*s += n;
		return !paren(s);
	}
	return atonumber(s);
}

typedef struct Op Op;
struct Op{
	int	level;
	Rune	tok[2];
	Rune	equiv;
};

Op optab[] = {
	0,	'*', 0,		'*',
	0,	L'×', 0,		'*',
	0,	L'·', 0,		'*',
	0,	'%', 0,		'%',
	0,	'/', 0,		'/',
	0,	L'÷', 0,		'/',
	1,	'+', 0,		'+',
	1,	'-', 0,		'-',
	2,	'<', '<',		L'«',
	2,	L'«', 0,		L'«',
	2,	'>', '>',		L'»',
	2,	L'»', 0,		L'»',
	3,	'<', 0,		'<',
	3,	'>', 0,		'>',
	3,	'<', '=',		L'≤',
	3,	'>', '=',		L'≥',
	4,	'=', '=',		L'≡',
	4,	'!', '=',		L'≠',
	5,	'&', 0,		'&',
	6,	'^', 0,		'^',
	7,	'|', 0,		'|',
	8,	'&', '&',		L'∧',
	9,	'|', '|',		L'∨',
};

static Op*
chartotok(char *s, int *m)
{
	char *p[4];
	int i, k;
	Rune r[4];
	Op *o[4], *x;

	p[0] = s;
	memset(o, 0, sizeof o);
	memset(r, 0, sizeof r);
	for(k = 0; k < 2; k++){
		if(p[k] == 0)
			break;
		p[k] += chartolrune(r+k, p[k]);
		p[k+1] = p[k];
		for(i = 0; i < nelem(optab); i++){
			x = optab + i;
			if(k == nelem(x->tok)-1 || x->tok[k+1] == 0)
			if(memcmp(x->tok, r, (k+1)*sizeof r[0]) == 0){
				o[k] = x;
				break;
			}
		}
	}
	for(i = k; i >= 0; i--)
		if(o[i]){
			*m = p[i]-s;
			return o[i];
		}
	return nil;
}

void
oper(Op *op, vlong *t, vlong n)
{
	switch(op->equiv){
	default:
		sysfatal("number: %C: don't know how", op->equiv);
	case '*':	*t *= n;		break;
	case '%':	*t %= n;	break;
	case '/':	*t /= n;		break;
	case '+':	*t += n;	break;
	case '-':	*t -= n;		break;
	case L'«':	*t <<= n;	break;
	case L'»':	*t >>= n;	break;
	case '<':	*t = *t < n;	break;
	case '>':	*t = *t > n;	break;
	case L'≤': *t = *t <= n;	break;
	case L'≥': *t = *t >= n;	break;
	case L'≡': *t = *t == n;	break;
	case L'≠': *t = *t != n;	break;
	case '&':	*t &= n;		break;
	case '^':	*t ^= n;	break;
	case '|':	*t |= n;		break;
	case L'∧': *t = *t && n;	break;		/* botch; no short-circut */
	case L'∨': *t = *t || n;	break;		/* botch; no short-circut */
	}
}

vlong
aexpr0(char **s, int l, int depth)
{
	int m;
	vlong t, n;
	Op *op;

	for(t = paren(s);;){
		op = chartotok(*s, &m);
		if(op == nil)
			return t;
		dprint("%.2d  t = %lld %C	%d %d\n", depth, t, op->equiv, l, op->level);
		if(l < op->level){
			dprint("	%lld\n", t);
			return t;
		}
		*s += m;
		n = aexpr0(s, op->level, depth+1);
		dprint("	%lld %C %lld → ", n, op->equiv, t);
		oper(op, &t, n);
		dprint("%lld\n", t);
	}
}

vlong
aexpr(char **s)
{
	vlong t;
	Rune r;

	t = aexpr0(s, Nprec-1, 0);
	chartolrune(&r, *s);
	if(r != 0)
		sysfatal("parse error at: %s", *s);
	return t;
}

Rune	vtab[]	= L"dxXob«»m";
char	ftab[]	= "0123456789,+- #";
char	itab[]	= "lh";

char*
genfmt(char *s)
{
	char *u;
	int i, f;
	Rune r;
	static char buf[100];

	memset(buf, 0, sizeof buf);
	i = 0;
	f = -1;
	u = "";

	for(;;){
		if(i + 3 + UTFmax + 1 >= sizeof buf)
			sysfatal("fmt too large");
		s += chartorune(&r, s);
		if(r == 0)
			break;
		if(f == -1){
			i += runetochar(buf+i, &r);
			if(r == '%')
				f = 0;
			continue;
		}
		if(r<Runeself && f<2 && strchr(ftab, r)){
			buf[i++] = r;
			continue;
		}
		if(r == '.' && f==0){
			f = 1;
			buf[i++] = r;
			continue;
		}
		if(r == 'u'){
			u = "u";
			continue;
		}
		if(r<Runeself && strchr(itab, r)){
			f = 2;
			continue;
		}
		if(runestrchr(vtab, r)){
			f = 2;
			i += sprint(buf+i, "ll%s%C", u, r);
			break;
		}
		sysfatal("unexpected fmt character %C", r);
	}

	if(f != 2)
		sysfatal("no verb");
	if(strlen(s) + i > sizeof buf - 2)
		sysfatal("fmt too long");
	memcpy(buf+i, s, strlen(s));
	i += strlen(s);
	if(buf[i-1] != '\n')
		buf[i++] = '\n';
	if(flagv)
		fprint(2, "%s", buf);
	buf[i] = 0;
	return buf;
}

void
usage(void)
{
	fprint(2, "usage: number [-f fmt] ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *fmt = "%lld\n";
	int i;

	ARGBEGIN{
	default:
		usage();
	case 'd':
		flagd = 1;
		break;
	case 'v':
		flagv = 1;
		break;
	case 'f':
		fmt = genfmt(EARGF(usage()));
		break;
	}ARGEND

	fmtinstall(L'«', fmt«);
	fmtinstall(L'»', fmt»);
	fmtinstall(L'm', fmtm);
	for(i = 0; i < argc; i++)
		print(fmt, aexpr(argv + i));
	exits("");
}
