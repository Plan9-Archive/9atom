#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "fs.h"

/*
 * There are 3584 bytes available at CONFADDR.
 *
 * The low-level boot routines in l.s leave data for us at CONFADDR,
 * which we pick up before reading the plan9.ini file.
 */
enum {
	Maxblocks	= 10,
	Maxitem		= 32,
	Maxconf		= 100,
	Bootlinelen	= 64,
	Bootargslen	= 3584-0x200-Bootlinelen,
};
#define Bootargs	((char*)(CONFADDR+Bootlinelen))

typedef struct Mblock Mblock;
struct Mblock {
	Mblock	*next;
	char	name[28];
	char	v[512];
};

static	Mblock	*blktab[Maxblocks];
static	Mblock	*conftab[Maxconf];
static	Mblock	*curblk;
static	int	nblock;
extern	char	**ini;

	void	parseini(char*);

static Mblock*
lookblock(char *s)
{
	int i;

	for(i = 0; i < nblock; i++)
		if(strcmp(s, blktab[i]->name) == 0)
			return blktab[i];
	return nil;
}

static Mblock*
newblock(char *s)
{
	char buf[28];
	int l;
	Mblock *b;

	if(nblock == Maxblocks)
		return nil;
	if(s[0] != '[' || (l = strlen(++s)) >= 28)
		return nil;
	if(s[--l] != ']' || l < 1)
		return nil;
	memmove(buf, s, l);
	buf[l] = 0;
	if(b = lookblock(buf))
		return curblk = b;
	b = blktab[nblock++] = malloc(sizeof *b);
	memmove(b->name, buf, l + 1);
	return curblk = b;
}

static Mblock*
common(void)
{
	return newblock("[common]");
}

static Mblock*
lookitem(char *s)
{
	Mblock *b;

	for(b = curblk; b; b = b->next)
		if(strcmp(b->name, s) == 0)
			return b;
	return nil;
}

static Mblock*
newitem0(Mblock *m)
{
	Mblock *b;

	for(b = curblk; b->next; b = b->next)
		;
	b->next = m;
	return m;
}

static Mblock*
newitem(char *s)
{
	char *p;
	int l;
	Mblock *m;

	p = strchr(s, '=');
	if(p == nil || (l = p - s) == 0 || l >= 28)
		return nil;
	if(s[l - 1] == ' ' && --l == 0)
		return nil;
	if(*++p && *p == ' ')
		p++;
	m = malloc(sizeof *m);
	memmove(m->name, s, l);
	m->name[l] = 0;
	snprint(m->v, sizeof m->v, "%s", p);
	return newitem0(m);
}

static int
varsep(int c)
{
	return strchr("!/= {}()", c) != nil;
}

static void
dovar(Mblock *b0, Mblock *b, char *p, char *e)
{
	char *p0, *q, buf[28], tok[80];
	int l, dol;
	Mblock *c, *m;

	while(*p){
		p0 = p;
		if(dol = *p == '$')
			p++;
		for(q = p; *q; q++)
			if(varsep(*q))
				break;
		l = snprint(buf, sizeof buf, "%.*s", (int)(q-p), p);
		if(0)
			print("dovar %s[%s] %s\n", dol? "$ ": "", buf, q);
		m = nil;
		if(q > p && dol)
			for(c = b0; c != nil && c != b; c = c->next)
				if(cistrcmp(c->name, buf) == 0)
					m = c;
		if(m){
			snprint(tok, sizeof tok, "%s%s", m->v, q);
			seprint(p0, e, "%s", tok);
		}else{
			p += l;
			if(varsep(*p) && *p)
				p++;
		}
	}
}

static void
varsubst(Mblock *b0)
{
	Mblock *b;

	for(b = b0; b != nil; b = b->next)
		dovar(b0, b, b->v, b->v + sizeof b->v);
}

void
changeconf(char *name, int append, char *fmt, ...)
{
	char *p, *e, buf[128];
	int m;
	va_list arg;
	Mblock *b;

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof buf, fmt, arg);
	va_end(arg);

	for(b = common(); b != nil; b = b->next){
		m = cistrcmp(name, b->name) == 0;
		if(!m && b->next == nil && !append){
			b->next = malloc(sizeof *b);
			memmove(b->next->name, name, strlen(name));
		}else if(m){
			if(!append)
				b->v[0] = 0;
			p = b->v;
			e = p + sizeof b->v;
			p += strlen(b->v);
			seprint(p, e, "%s", buf);
			dovar(common(), b, p, e);
		}
	}
}

static Mblock**
buildvtab(Mblock *b0)
{
	int i;
	Mblock *b, **t;

	t = conftab;
	i = 0;
	for(b = b0; b != nil && i < Maxconf-1; b = b->next)
		t[i++] = b;
	t[i] = 0;
	return t;
}

char*
getconf(char *name)
{
	int i, n, nmatch;
	char *r, buf[128], itab[Maxconf];
	Mblock **t, *b;

redux:
	t = buildvtab(common());
	nmatch = 0;
	for(i = 0; t[i] != nil; i++)
		if(cistrcmp(name, t[i]->name) == 0)
			itab[nmatch++] = i;
	n = nmatch;
	if(n > 1){
		print("\n");
		for(i = 0; i < nmatch; i++)
			print("%d. %s\n", i + 1, t[itab[i]]->v);
		print("a. add an item\n");
		do{
			getstr(name, buf, sizeof buf, nil, 0);
			n = strtol(buf, &r, 0);
			if(buf[0] == 'a' && buf[1] == ' '){
				changeconf(" ", 0, buf + 2);
				if(b = lookitem(" "))
					snprint(b->name, sizeof b->name, "%s", name);
				goto redux;
			}
		}while(n < 1 || n > i);
	}
	r = nil;
	if(n != 0)
		r = t[itab[n-1]]->v;
	for(i = 0; i < nmatch; i++)
		if(i != n && i > 0)
			t[itab[i]-1]->next = t[itab[i]+1];
	return r;
}

static int
comma(char *s, char **f)
{
	char *r;
	int i, n;
	static char buf[128];

	f[0] = f[1] = "";
	snprint(buf, sizeof buf, "%s", s);
	n = getfields(buf, f, 2, ',');
	for(i = 0; i < n; i++){
		if(f[i][0] == ' ')
			f[i]++;
		r = f[i] + strlen(f[i]);
		if(r > f[i] && r[-1] == ' ')
			r[-1] = 0;
	}
	return n;
}

static char*
pritem(Mblock *b)
{
	static char buf[128];

	if(b->v[0] == 0)
		snprint(buf, sizeof buf, "[%s]", b->name);
	else
		snprint(buf, sizeof buf, "%s=%s", b->name, b->v);
	return buf;
}
	
static void
menu(void)
{
	char *r, *f[2], buf[80], buf1[10];
	int i, n, tmout, item, dfltno;
	Mblock *m, *b, *p, *dflt, *mtab[Maxblocks], *ptab[Maxblocks];

	if((b = lookblock("menu")) == nil)
		return;
	dflt = nil;
	tmout = 0;
	item = 0;
	for(p = b->next; p != nil; p = p->next){
		n = comma(p->v, f);
		m = lookblock(f[0]);
		if(cistrcmp(p->name, "menuitem") == 0){
			if(n != 2 || m == nil)
				print("invalid block %s\n", f[0]);
			else{
				ptab[item] = p;
				mtab[item++] = m;
			}
		}else if(cistrcmp(p->name, "menudefault") == 0){
			if(m == nil)
				print("invalid menudefault %s\n", f[0]);
			else{
				dflt = m;
				tmout = strtol(f[1], 0, 0);
			}
		}else if(cistrcmp(p->name, "menuconsole") == 0)
			consinit(f[0]);
		else
			print("invalid line in [menu] %s=%s\n", p->name, p->v);
	}
redux:
	print("\nPlan 9 Startup Menu:\n====================\n");
	dfltno = 0;
	for(i = 0; i < item; i++){
		comma(ptab[i]->v, f);
		print("    %d. %s\n", i + 1, f[1]);
		if(mtab[i] == dflt)
			dfltno = i + 1;
	}
	if(dfltno == 0 && dflt != nil){
		print("bad default [%s]\n", dflt->name);
		tmout = 0;
	}
	for(;;){
		snprint(buf1, sizeof buf1, "%d", dfltno);
		getstr("Selection", buf, sizeof buf, dfltno>0? buf1: nil, tmout);
		tmout = 0;
		i = strtoul(buf, &r, 0);
		if(*r == 'm' && r[1] == 0)
			goto redux;
		print("\n");
		if(*r == 'a' && r[1] == ' ')
			parseini(r + 2);
		else if(*r == 'p' || *r == 'P'){
			for(p = common(); p; p = p->next)
				print("%s\n", pritem(p));
			if(i > 0 && i < item)
				for(p = mtab[i-1]; p; p = p->next)
					print("%s\n", pritem(p));
		}else if(*r == 0 && i <= item){
			m = mtab[i-1];
			common();
			newitem0(m->next);	/* chain 'em up */
			if(b = lookblock("commontail"))
				newitem0(b->next);
			comma(ptab[i-1]->v, f);
			changeconf("menuitem", 0, "%s", f[0]);
			return;
		}
	}
}

void
buildconf(void)
{
	char *p, *e;
	Mblock *b;

	p = Bootargs;
	e = p + Bootargslen;
	for(b = common()->next; b; b = b->next)
		if(b->v[0])
			p = seprint(p, e, "%s=%s\n", b->name, b->v);
}

static char*
sanitize(char *s)
{
	char *p, *e, *tok[20];
	int m, j, b;
	static char buf[128];

	b = 1;
	for(p = s; *p; p++)
		if(*p == '\t' || *p == '\r')
			*p = ' ';
		else if(b && *p == '#')
			*p = 0;
		else
			b = 0;
	m = getfields(s, tok, nelem(tok), ' ');
	buf[0] = 0;
	if(m > 0){
		j = 0;
		e = buf + sizeof buf;
		for(p = buf;;){
			p = seprint(p, e, "%s", tok[j]);
			if(++j == m)
				break;
			p = seprint(p, e, " ");
		}
	}
	return buf;
}

void
parseini(char *s)
{
	char *p, *line[Maxconf];
	int n, i;

	common();
	n = getfields(s, line, nelem(line), '\n');
	for(i = 0; i < n; i++){
		p = sanitize(line[i]);
		if(*p == '[')
			newblock(p);
		else if(*p)
			newitem(p);
	}
}

int
dotini(Fs *fs, char *name, char *devstr)
{
	char *p;
	int n;
	File rc;

	if(fswalk(fs, *ini, &rc) <= 0)
		return -1;
	p = Bootargs;
	if((n = fsread(&rc, p, Bootargslen-2)) <= 0)
		return -1;
	p[n] = '\n';
	p[n+1] = 0;

//	common();
	changeconf("bootdev", 0, "%s", name);
	changeconf("bootpath", 0, "%s/$bootdev", devstr);
	parseini(p);
	menu();
	varsubst(common());
	return 0;
}

static char*
match(char *s, char *p)
{
	if(cistrncmp(s, p, strlen(p)) == 0)
		return s + strlen(p);
	return nil;
}

static char*
copyto(char *p, char *t, int nt, int term)
{
	char *e;

	e = t + nt;
	for(; *p != 0 && *p != term && t < e;)
		*t++ = *p++;
	*t = 0;
	return p;
}

int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	char cc[NAMELEN], *p, *r;

	snprint(cc, sizeof cc, "%s%d", class, ctlrno);
	if((p = getconf(cc)) == nil)
		return 0;
	for(;;){
		if(*p== ' ')
			p++;
		if(*p == 0)
			break;
		if(r = match(p, "type="))
			p = copyto(r, isa->type, sizeof isa->type, ' ');
		else if(r = match(p, "port="))
			isa->port = strtoul(r, &p, 0);
		else if(r = match(p, "irq="))
			isa->irq = strtoul(r, &p, 0);
		else if(r = match(p, "mem="))
			isa->mem = strtoul(r, &p, 0);
		else if(r = match(p, "size="))
			isa->size = strtoul(r, &p, 0);
		else if(r = match(p, "ea=")){
			if(parseether(isa->ea, r) == -1)
				memset(isa->ea, 0, 6);
		}else if(isa->nopt < NISAOPT)
			p = copyto(p, isa->opt[isa->nopt++], ISAOPTLEN, ' ');
		while(*p && *p != ' ')
			p++;
	}
	return 0;	
}

void
readlsconf(void)
{
	ushort *p;
	ulong *l;

	if(strcmp((char*)CONFADDR, "APM") == 0){
		p = (ushort*)CONFADDR;
		l = (ulong*)CONFADDR;
		apm.haveinfo = 1;
		apm.ax = p[2];
		apm.cx = p[3];
		apm.dx = p[4];
		apm.di = p[5];
		apm.ebx = l[3];
		apm.ebx = l[4];
		print("apm ax=%x cx=%x dx=%x di=%x ebx=%x esi=%x\n",
			apm.ax, apm.cx, apm.dx, apm.di, apm.ebx, apm.esi);
	}
	e820();	/* e820(CONFADDR + 20); */
}
