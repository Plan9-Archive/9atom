/*
 * Coraid ethernet console — serial replacement.
 */

#include "all.h"
#include "../ip/ip.h"
#include "io.h"
#include "etherif.h"
#include "mem.h"

enum {
	Ncbuf 	= 4096,
	Ncmask 	= Ncbuf-1,
	Namelen	= 128,
};

enum{
	Tinita 	= 0,
	Tinitb,
	Tinitc,
	Tdata,
	Tack,
	Tdiscover,
	Toffer,
	Treset,
};

enum{
	Cunused	= 0,
	Cinitb,
	Clogin,
	Copen,
};

typedef struct{
	uchar	valid;
	uchar	ea[Easize];
	int	i;
	Queue	*reply;
}If;

typedef struct{
	uchar	dst[Easize];
	uchar	src[Easize];
	uchar	etype[2];
	uchar	type;
	uchar	conn;
	uchar	seq;
	uchar	len;
	uchar	data[0x100];
}Pkt;

typedef struct{
	QLock;
	Lock;
	uchar	ea[Easize];	/* along with cno, the key to the connection */
	uchar	cno;		/* connection number on remote host */
	uchar	stalled;		/* cectimer needs to kick it -- cecputs while !islo() */
	uchar	state;		/* connection state */
	char	retries;		/* remaining retries */
	long	idle;		/* last reply tick. */
	int	to;		/* ticks to timeout */
	Msgbuf	*m;		/* unacked message */
	If	*ifc;		/* interface for this connection */
	uchar	sndseq;		/* sequence number of last sent message */
	uchar	rcvseq;		/* sequence number of last rcv'd message */
	char	cbuf[Ncbuf];	/* curcular buffer */
	int	r, w;		/* indexes into cbuf */
	int	pwi;		/* index into passwd; */
	char	passwd[32];	/* password typed by connection */
}Conn;

enum {
	CMconfig = 1,
	CMpasswd,
	CMstat,
	CMtrace,

	Nconns = 15,
};

static	uchar	broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static 	Conn 	conn[Nconns];
static	int	early = 1;
static 	If 	iftab[MaxEther];
static	char	passwd[Namelen];
static	int	rsnd;
static	uchar	tflag;
static	Rendez	trendez;
static	int	xmit;

typedef struct {
	int	index;
	char	*name;
	int	narg[2];
	char	*usage;
} Cmdtab;

static Cmdtab cmdtab[] = {
	CMconfig,	"config",		1, 2,	"cec config [macaddr]",
	CMpasswd,	"password",	2, -1,	"cec password [password]",
	CMstat,		"stat",		1, -1,	"cec stat",
	CMtrace,	"trace",  		1, -1,	"cec trace",
};

static char *types[] = {
	"Tinita", 	"Tinitb",	"Tinitc", 
	"Tdata", 	"Tack",  	"Tdiscover",
	"Toffer",	"Treset",	"*GOK*",
};

/*
 * Since this code is in the output chain of procedures for console
 * output, we can't use the general printf functions.  See the ones
 * at the bottom of this file.  It assumes the serial port.
 */
static int
cecprint(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	va_start(arg, fmt);
	n = vseprint(buf, buf+sizeof buf, fmt, arg)-buf;
	va_end(arg);
	uartputs(buf);
	return n;
}

static void
reply(If *i, Msgbuf *m)
{
	if(i == 0){
		print("reply i is nil %#p\n", getcallerpc(&i));
		return;
	}
	send(i->reply, m);
}

static int
cbget(Conn *cp)
{
	int c;
	
	if(cp->r == cp->w)
		return -1;
	c = cp->cbuf[cp->r];
	cp->r = (cp->r+1)&Ncmask;
	return c;
}

static void
cbput(Conn *cp, int c)
{
	if(cp->r == (cp->w+1)&Ncmask)
		return;
	cp->cbuf[cp->w] = c;
	cp->w = (cp->w+1)&Ncmask;
}

	
static void
pkttrace(Pkt *p)
{
	if(tflag == 0)
		return;
	cecprint("%E > %E) seq %d, type %s, len %d, conn %d\n",
		p->src, p->dst, p->seq, types[p->type], p->len, p->conn);
}

static void
trace(Msgbuf *m)
{
	pkttrace((Pkt*)m->data);
}

static Msgbuf*
sethdr(If *ifc, uchar *ea, Pkt **pkt, int len)
{
	Msgbuf *m;
	Pkt *p;

	len += 18;
	if(len < 60)
		len = 60;
	m = mballoc(len, 0, Mbcec);
	m->count = len;
	p = (Pkt*)m->data;
	memmove(p->dst, ea, Easize);
	memmove(p->src, ifc->ea, Easize);
	p->etype[0] = 0xbc;
	p->etype[1] = 0xbc;
	p->seq = 0;
	*pkt = p;
	return m;
}

static Msgbuf*
mbclone(Msgbuf *a)
{
	Msgbuf *m;

	m = mballoc(a->count, a->chan, a->category);
	memmove(m->data, a->data, a->count);
	return m;
}


static void
pktsend(Conn *cp, Msgbuf *m)
{
	if(cp->m != nil)
		panic("cecsend: cp->m not nil\n");
	cp->m = mbclone(m);
	trace(m);
	reply(cp->ifc, m);
	cp->to = 4;
	cp->retries = 3;
	xmit++;
}

static void
senddata(Conn *cp, void *data, int len)
{
	Msgbuf *m;
	Pkt *p;
	
	m = sethdr(cp->ifc, cp->ea, &p, len);
	memmove(p->data, data, len);
	p->len = len;
	p->seq = ++cp->sndseq;
	p->conn = cp->cno;
	p->type = Tdata;
	pktsend(cp, m);
}
	
static void
resend(Conn *cp)
{
	Msgbuf *m;
	
	trace(m = mbclone(cp->m));
	reply(cp->ifc, m);
	cp->to = 4;
	rsnd++;
}

static void
ack(Conn *c)
{
	if(c->m)
		mbfree(c->m);
	c->m = nil;
	c->to = 0;
	c->retries = 0;
}

static void
start(Conn *cp)
{
	char buf[250];
	int n, c;
	
	if(cp->m)
		return;
	ilock(cp);
	for(n = 0; n < sizeof buf; n++){
		if((c = cbget(cp)) == -1)
			break;
		buf[n] = c;
	}
	iunlock(cp);
	if(n != 0)
		senddata(cp, buf, n);
}
	
void
cecputs(char *str, int n)
{
	int i, c, w;
	Conn *cp;

	if(early || predawn)
		return;
	w = 0;
	for(cp = conn; cp < conn+Nconns; cp++){
		ilock(cp);
		if(cp->state == Copen){
			for (i = 0; i < n; i++){
				c = str[i];
				if(c == '\n')
					cbput(cp, '\r');
				cbput(cp, c);
			}
			w = 1;
			cp->stalled = 1;
		}
		iunlock(cp);
	}
	if(w == 1)
		wakeup(&trendez);
}

static void
conputs(Conn *c, char *s)
{
	for(; *s; s++)
		cbput(c, *s);
}

static void
cectimer(void)
{
	Conn *c;
	
	for(;;){
		tsleep(&trendez, no, 0, 250);
		for(c = conn; c < conn+Nconns; c++){
			qlock(c);
			if(c->m != nil){
				if(--c->to <= 0){
					if(--c->retries <= 0){
						mbfree(c->m);
						c->m = nil;
//						c->state = Cunused;
					}else
						resend(c);
				}
			}else if(c->stalled){
				c->stalled = 0;
				start(c);
			}
			qunlock(c);
		}
	}
}

static void
discover(If *ifc, Pkt *p)
{
	uchar *a;
	Msgbuf *m;
	Pkt *q;

	if(p)
		a = p->src;
	else
		a = broadcast;
	m = sethdr(ifc, a, &q, 0);
	q->type = Toffer;
	q->len = snprint((char *)q->data, sizeof q->data, "%d %s", -1, service);
	trace(m);
	reply(ifc, m);
}

static Conn*
findconn(uchar *ea, uchar cno)
{
	Conn *c, *n;

	n = nil;
	for(c = conn; c < &conn[Nconns]; c++){
		if(n == nil && c->state == Cunused)
			n = c;
		if(memcmp(ea, c->ea, Easize) == 0 && cno == c->cno)
			return c;
	}
	return n;
}

static void
checkpw(Conn *cp, char *str, int len)
{
	int i, c;
	
	if(passwd[0] == 0)
		return;
	for(i = 0; i < len; i++){
		c = str[i];
		if(c != '\n' && c != '\r'){
			if(cp->pwi < sizeof cp->passwd-1)
				cp->passwd[cp->pwi++] = c;
			cbput(cp, '#');
			cecprint("%c", c);
			continue;
		}
		// is newline; check password
		cp->passwd[cp->pwi] = 0;
		if(strcmp(cp->passwd, passwd) == 0){
			cp->state = Copen;
			cp->pwi = 0;
			print("\r\n%E logged in\r\n", cp->ea);
		}else{
			conputs(cp, "\r\nBad password\r\npassword: ");
			cp->pwi = 0;
		}
	}
	start(cp);
}

//struct{
//	Rendez;
//	uchar	buf[8192];	// power of 2.
//	ushort	r;
//	ushort	w;
//	ushort	m;
//} ibuf = {
//.m	= 8192-1,
//};

//static void
//cecinputs(uchar *s, int l)
//{
//	int c;
//
//	for(; l != 0; l--){
//		if((ibuf.w+1&ibuf.m) == ibuf.r)
//			// this should sleep but i'm afraid of hanging
//			// the ethernet since we only have one etheri
//			// process per port.
//			break;
//		if((c = *s++) == '\r')
//			c = '\n';
//		ibuf.buf[ibuf.w++] = c;
//		kbdchar(c);
//	}
//}

//int
//cecgetc(void)
//{
//	int c;
//	if(ibuf.r == ibuf.w)
//		return 0;
//	c = ibuf.buf[ibuf.r++];
//	ibuf.r &= ibuf.m;
//	return c;
//}

static struct{
	int	c;
	long	ticks;
} ibuf;

static void
cecinputs(uchar *s, int l)
{
	int c;

	for(; l != 0; l--){
		if((c = *s++) == '\r')
			c = '\n';
		kbdchar(c);
		ibuf.c = c;
		ibuf.ticks = Ticks;
	}
}

int
cecgetc(void)
{
	if(ibuf.c == 0)
		return 0;
	if(Ticks-ibuf.ticks > HZ/3)
		return 0;
	ibuf.ticks = 0;
	return ibuf.c;
}

static void
incoming(Conn *cp, If *ifc, Pkt *p)
{
	Pkt *np;
	Msgbuf *m;
	
	/* ack it no matter what its sequence number */
	m = sethdr(ifc, p->src, &np, 0);
	np->type = Tack;
	np->seq = p->seq;
	np->conn = cp->cno;
	np->len = 0;
	trace(m);
	reply(ifc, m);

	if(cp->state == Cunused){
		/* stale connection */
		discover(ifc, p);
		return;
	}
	if(p->seq == cp->rcvseq)
		return;
	cp->rcvseq = p->seq;
	if(cp->state == Copen)
		cecinputs(p->data, p->len);
	else if(cp->state == Clogin)
		checkpw(cp, (char *)p->data, p->len);
}

static void
inita(Conn *c, If *ifc, Pkt *p)
{
	Pkt *q;
	Msgbuf *m;
	
	c->ifc = ifc;
	c->state = Cinitb;
	memmove(c->ea, p->src, Easize);
	c->cno = p->conn;
	m = sethdr(ifc, p->src, &q, 0);
	q->type = Tinitb;
	q->conn = c->cno;
	q->len = 0;
	pktsend(c, m);
}

static If*
findif(Ifc *f)
{
	int i;

	for(i = 0; i < nelem(iftab); i++)
		if(iftab[i].valid && memcmp(iftab[i].ea, f->ea, Easize) == 0)
			return iftab+i;
	return 0;
}

void
cecreceive(Enpkt *ep, int, Ifc *i)
{
	If *if0;
	Pkt *p;
	Conn *c;
	
	p = (Pkt*)ep;
	pkttrace(p);
	if((if0 = findif(i)) == nil)
		return;
	c = findconn(p->src, p->conn);
	if(c == nil){
		cecprint("cec: out of connection structures\n");
		return;
	}
	qlock(c);
	c->idle = Ticks;
	switch(p->type){
	case Tinita:
		if(c->m){
			cecprint("cec: reset with bp\n");
			mbfree(c->m);
			c->m = 0;
		}
		inita(c, if0, p);
		break;
	case Tinitb:
		cecprint("cec: unexpected initb\n");
		break;
	case Tinitc:
		if(c->state == Cinitb){
			ack(c);
			if(c->passwd[0]){
				c->state = Clogin;
				conputs(c, "password: ");
				start(c);
			}else
				c->state = Copen;
		}
		break;
	case Tdata:
		incoming(c, if0, p);
		break;
	case Tack:
		if(c->state == Clogin || c->state == Copen){
			ack(c);
			start(c);
		}
		break;
	case Tdiscover:
		discover(if0, p);
		break;
	case Toffer:
		// cecprint("cec: unexpected offer\n");  from ourselves.
		break;
	case Treset:
		if(c->m)
			mbfree(c->m);
		c->m = 0;
		c->state = Cunused;
		break;
	default:
		cecprint("bad cec type: %d\n", p->type);
		break;
	}
	qunlock(c);
}

static char *cstate[] = { "unused", "initb", "login", "open" };

static int
hexdigit(int c){
	if(c >= '0' && c <= '9')
		return c-'0';
	if(c >= 'A' && c <= 'Z')
		return c-'A'+10;
	if(c >= 'a' && c <= 'z')
		return c-'a'+10;
	return -1;
}

int
eacvt(uchar *t, char *s){
	int i, c;

	for(i = 0; c = s[i]; i++)
		if(hexdigit(c) == -1)
			break;
	if(i != 12)
		return -1;
	for(i = 0; i < Easize; i++)
		t[i] = hexdigit(s[2*i])<<4|hexdigit(s[2*i+1]);
	return 0;
}

static int
eaqueue(uchar *ea)
{
	int i;

	for(i = 0; i < nether; i++)
		if(memcmp(etherif[i].ea, ea, Easize) == 0)
			return i;
	return -1;
}

static int
cecconfig0(int idx)
{
	Ether *e;
	If *i;

	i = iftab+idx;
	e = etherif+idx;
	if(!e->ifc.reply)
		return -1;
	i->valid ^= 1;
	i->i = idx;
	memmove(i->ea, e->ea, Easize);
	i->reply = e->ifc.reply;
	if(i->valid)
		discover(i, 0);
	return 0;
}

static void
cecconfig(char *eas)
{
	uchar ea[Easize];
	int i;

	if(strcmp(eas, "allow") == 0){
		for(i = 0; i < nelem(iftab); i++)
			iftab[i].valid = 0;
		for(i = 0; i < nether; i++)
			cecconfig0(i);
	}else if(strcmp(eas, "disallow") == 0){
		for(i = 0; i < nelem(iftab); i++)
			iftab[i].valid = 0;
	}else if(strlen(eas) < Easize*2){
		if((i = strtoul(eas, &eas, 10)) >= nether
		|| cecconfig0(i) == -1)
			print("bad interface index %d\n", i);
	}else if(eacvt(ea, eas) == 0){
		if((i = eaqueue(ea)) != -1)
			cecconfig0(i);
		else
			print("no interface claims %E\n", ea);
	}else
		print("bad mac address\n");
}

static Cmdtab *
lookupcmd(char *name, int argc, Cmdtab *t, int n)
{
	int i;

	for(i = 0; i < n; i++)
		if(strcmp(name, t[i].name) == 0)
			goto found;
	return 0;
found:
	t = t+i;
	if(argc != t->narg[0] && argc != t->narg[1]){
		print(t->usage);
		return 0;
	}
	return t;
}

static void
cecusage(void)
{
	print(	"\t"	"cec config [macaddr|allow|disallow]\n"
		"\t"	"cec passwd [passwd]\n"
		"\t"	"stat\n"
		"\t"	"trace\n");
}

static void
ceccmd0(int argc, char **argv)
{
	Cmdtab *t;
	If *i;
	Conn *c;
	int j;

	t = lookupcmd(*argv, argc, cmdtab, nelem(cmdtab));
	if(t == 0){
		cecusage();
		return;
	}
	switch(t->index){
	case CMconfig:
		for(j = 2; j < argc; j++)
			cecconfig(argv[j]);
		for(i = iftab; i < iftab+nelem(iftab); i++)
			if(i->valid)
				print("%d %E\n", i->i, i->ea);
		break;
	case CMpasswd:
		if(argc == 2)
			snprint(passwd, sizeof passwd, "%s", argv[1]);
		print("%s\n", passwd);
		break;
	case CMstat:
		for(c = conn; c < &conn[Nconns]; c++)
			if(c->state != Cunused)
			print("%d %E %3d %-6s %12ld %d %d\n",
				c->ifc->i, c->ea, c->cno, cstate[c->state], Ticks-c->idle,
				c->to, c->retries);
		break;
	case CMtrace:
		tflag ^= 1;
		print("tflag = %d\n", tflag);
		break;
	}
}

void
ceccmd(int c, char **v)
{
	if(c > 1)
		ceccmd0(c-1, v+1);
	else
		cecusage();
}

void
cecinit(void)
{
	Ifc *e;

	cmd_install("cec",	"subcommand -- cec control", ceccmd);
	for(e = enets; e; e = e->next)
		if(e->flag&Fcec)
			cecconfig0(e->idx);
	userinit(cectimer, nil, "cec");
	early = 0;
	cmd_exec("cec config");
}
