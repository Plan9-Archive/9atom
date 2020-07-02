#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "archtrace.h"

#pragma profile 0

enum {
	Qdir,
	Qctl,
	Qdata,

	TraceFree	= 0,
	TraceEntry, 
	TraceExit,

	Logsize		= 8192,
	Maxpidwatch	= 64,
	Printsize		= 121,
	Cacheline	= 64,
};

typedef struct Trace Trace;
typedef struct Tracelog Tracelog;
typedef struct Lockln Lockln;

struct Trace {
	Archtrace;
	Trace	*next;
	uchar	*start;
	uchar	*end;
	int	enabled;
	char	name[16];
};

/* This represents a trace "hit" or event */
struct Tracelog {
	uvlong	ticks;
	int	info;
	uintptr	pc;
	uintptr	dat[5];		/* depends on type */
};

/* bogus, non-portable experiment to reduce false sharing */
struct Lockln {
	union {
		Lock;
		uchar	pad[Cacheline];
	};
};

static	Rendez	tracesleep;
static	QLock	traceslock;
static	Trace	*traces;		/* all traces */
static	Lockln	loglk;
static	Tracelog	*tracelog;
	int	traceactive;
/*
 * trace indices. These are just ulongs. You mask them 
 * to get an index. This makes fifo empty/full etc. trivial. 
 */
static	uint	pw;
static	uint	pr;
static	int	tracesactive;
static	int	all;
static	long	traceinhits;
static	Lockln	traceinhitslk;
static	long	traceouthits;
static	Lockln	traceouthitslk;
static	ulong	logsize		= Logsize;
static	ulong	logmask		= Logsize - 1;
static	int	pidwatch[Maxpidwatch];
static	int	numpids;
	int	codesize;
static	char	hex[] 		= "0123456789abcdef";

static char eventname[] = {
[TraceEntry] 	'E',
[TraceExit] 	'X',
};

static Dirtab tracedir[]={
	".",		{Qdir, 0, QTDIR},	0,		DMDIR|0555,
	"tracectl",	{Qctl},			0,		0664,
	"trace",	{Qdata},		0,		0440,
};

extern	void	sfence(void);

char*
hex32(ulong l, char *c)
{
	int i;

	for(i = 7; i >= 0; i--){
		c[i] = hex[l&0xf];
		l >>= 4;
	}
	return c + 8;
}

char*
hex64(uvlong l, char *c)
{
	return hex32(l, hex32(l>>32, c));
}

static int
lognonempty(void)
{
	return pw - pr;
}

static int
logfull(void)
{
	return pw - pr == logsize;
}

static uvlong 
idx(uvlong f)
{
	return f & logmask;
}

/*
 * Check if the given trace overlaps any others
 * Returns 1 if there is overlap, 0 if clear.
 */
int
overlapping(Trace *p)
{
	Trace *t;

	for(t = traces; t != nil; t = t->next)
		if(p->start <= t->end && p->end >= t->start)
			return 1;
	return 0;
}

/*
 * Return 1 if pid is being watched or no pids are being watched.
 * Return 0 if pids are being watched and the argument is not
 * among them.
 */
int
watchingpid(int pid)
{
	int *tab, *m, n, i;
	
	if(numpids == 0)
		return 1;

	tab = pidwatch;
	n = numpids;
	while(n > 0){
		i = n/2;
		m = tab+i;
		if(*m == pid)
			return 1;
		if(*m < pid){
			tab += i+1;
			n -= i+1;
		}else
			n = i;
	}
	return 0;			
}

void
removetrace(Trace *p)
{
	Trace *prev, *t;

	prev = nil;
	for(t = traces; t != nil; prev = t, t = t->next)
		if(t == p){
			if(prev != nil)
				prev->next = t->next;
			if(t == traces)
				traces = nil;
			free(t);
		}
}

/* these next two functions assume tracelock is locked */
void
traceon(Trace *p)
{
	if(p->enabled != 1){
		p->enabled = 1;
		archtraceinstall(p);
		tracesactive++;
	}
}

void
traceoff(Trace *p)
{
	if(p->enabled == 1){
		p->enabled = 0;
		archtraceuninstall(p);
		tracesactive--;
	}
}

/*
 * Make a new tracelog (an event)
 */
static Tracelog*
newtracelog(void)
{
	Tracelog *t;

	t = nil;
	ilock(&loglk);
	if(!logfull())
		t = tracelog + idx(pw++);
 	iunlock(&loglk);

	return t;
}

void
tracein(uintptr pc, uintptr a[4])
{	
	Tracelog *t;

//	_xinc(&traceinhits);
	ilock(&traceinhitslk);
	traceinhits++;
	iunlock(&traceinhitslk);
	if(!all)
		if(!up || !watchingpid(up->pid))
			return;
	t = newtracelog();
	if(!t)
		return;
	cycles(&t->ticks);
	t->pc = pc;
	t->dat[0] = -1;
	if(up)
		t->dat[0] = up->pid;
	t->dat[1] = a[0];
	t->dat[2] = a[1];
	t->dat[3] = a[2];
	t->dat[4] = a[3];
	sfence();
	t->info = TraceEntry;
}

void
traceout(uintptr pc, uintptr retval)
{
	Tracelog *t;

//	_xinc(&traceouthits);
	ilock(&traceouthitslk);
	traceouthits++;
	iunlock(&traceouthitslk);
	if(!all)
		if(!up || !watchingpid(up->pid))
			return;

	t = newtracelog();
	if(!t)
		return;
	cycles(&t->ticks);
	t->pc = (uintptr)pc;
	t->dat[0] = -1;
	if(up)
		t->dat[0] = up->pid;
	t->dat[1] = retval;
	t->dat[2] = 0;
	t->dat[3] = 0;
	sfence();
	t->info = TraceExit;
}

/* Create a new trace with the given range */
static Trace*
mktrace(uchar *start, uchar *end)
{
	Trace *p;

	p = malloc(sizeof *p);
	if(mkarchtrace(p, start, &end) == -1){
		free(p);
		return nil;
	}
	p->start = start;
	p->end = end;
	return p;
}

static Chan*
traceattach(char *spec)
{
	return devattach('T', spec);
}

static Walkqid*
tracewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, tracedir, nelem(tracedir), devgen);
}

static int
tracestat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, tracedir, nelem(tracedir), devgen);
}

static Chan*
traceopen(Chan *c, int omode)
{
	if(tracelog == nil)
		tracelog = malloc(sizeof *tracelog * logsize);
	if(tracelog == nil)
		error(Enomem);
 	return devopen(c, omode, tracedir, nelem(tracedir), devgen);
}

static void
traceclose(Chan*)
{
}

static long
traceread(Chan *c, void *a, long n, vlong offset)
{
	char *buf, *e, *s, *s0;
	uint i, l, epr;
	Tracelog *pl;
	Trace *p;
	static QLock gate;

	if(c->qid.type == QTDIR)
		return devdirread(c, a, n, tracedir, nelem(tracedir), devgen);

	switch((int)c->qid.path){
	default:
		error("traceread: bad qid");
	case Qctl:
		buf = s = malloc(READSTR);
		e = buf + READSTR;
		s = seprint(s, e, "logsize %lud\n", logsize); 
		qlock(&traceslock);
		for(p = traces; p != nil; p = p->next){
			s = seprint(s, e, "trace %p %p new %s\n",
				p->start, p->end, p->name);
			s = archtracectlr(p, s, e);
		}
		for(p = traces; p != nil; p = p->next)
			s = seprint(s, e, "#trace %p traced? %d\n",
				p->start, p->enabled);
		for(p = traces; p != nil; p = p->next)
			if(p->enabled)
				s = seprint(s, e, "trace %s on\n", p->name);
		qunlock(&traceslock);
		for(i = 0; i < numpids; i++)
			s = seprint(s, e, "watch %d\n", pidwatch[i]);
		if(traceactive)
			s = seprint(s, e, "start\n"); 
		s = seprint(s, e, "#tracehits %d, in queue %d\n", pw, pw-pr); 
		s = seprint(s, e, "#tracelog %p\n", tracelog);
		s = seprint(s, e, "#traceactive %d\n", traceactive);
		s = seprint(s, e, "#traceinhits %lud\n", traceinhits);
		s = seprint(s, e, "#traceouthits %lud\n", traceouthits);
		USED(s);
		n = readstr(offset, a, n, buf);
		free(buf);
		break;
	case Qdata:
		/*
		 * print is avoided because it might be traced.
		 */
		s = s0 = a;
		qlock(&gate);
		for(epr = pr + n/Printsize; pr != epr && lognonempty(); pr++){
			pl = tracelog + idx(pr);
			if((l = eventname[pl->info]) == TraceFree)
				break;
			pl->info = TraceFree;
			/* simple format */
			*s++ = l;
			*s++ = ' ';
			s = hex64((uvlong)pl->pc, s);
			*s++ = ' ' ;
			s = hex64(pl->ticks, s);
			*s++ = ' ';
			for(i = 0; ; i++){
				s = hex64(pl->dat[i], s);
				if(i == 4)
					break;
				*s++ = ' ';
			}
			*s++ = '\n';
		}
		qunlock(&gate);
		n = s - s0;
		break;
	}
	return n;
}

static char notfound[] = "devtrace: trace not found";
static char badaddr[] = "devtrace: bad address";

static uchar*
getaddr(char *s)
{
	char *e;
	uvlong u;

	u = strtoull(s, &e, 16);
	if(*e)
		error(badaddr);
	if(u < KTZERO)
		u |= KTZERO;
	if((char*)u > etext)
		error(badaddr);
	return (uchar*)u;
}
	
static long
tracewrite(Chan *c, void *a, long n, vlong)
{
	char *tok[6], *ep, *s;
	uchar *start, *end;
	int *w, l, ntok, pid, saveactive;
	Trace *p, *t;
	Tracelog *lg;

	saveactive = traceactive;
	traceactive = 0;
	mfence();
	s = nil;
	qlock(&traceslock);
	if(waserror()){
		qunlock(&traceslock);
		if(s != nil)
			free(s);
		traceactive = saveactive;
		nexterror();
	}
	switch((uintptr)c->qid.path){
	default:
		error("tracewrite: bad qid");
	case Qctl:
		s = malloc(n + 1);
		memmove(s, a, n);
		s[n] = 0;
		ntok = tokenize(s, tok, nelem(tok));
		if(!strcmp(tok[0], "trace")){
			for(p = traces; p != nil; p = p->next)
				if(strcmp(tok[1], p->name) == 0)
					break;
			if(ntok > 3 && !strcmp(tok[3], "new")){
				if(ntok != 5)
					error("devtrace: usage: trace <ktextstart> <ktextend> new <name>");
				start = getaddr(tok[1]);
				end = getaddr(tok[2]);
				if(start > end)
					error("devtrace: invalid address range");
				if(p)
					error("devtrace: trace already exists");

				if((p = mktrace(start, end)) == nil)
					error(Egreg);
				for(t = traces; t != nil; t = t->next)
					if(strcmp(tok[4], t->name) == 0)
						error("devtrace: trace with that name already exists");
				if(overlapping(p))
					error("devtrace: given range overlaps with existing trace");
				if(ntok < 5)
					snprint(p->name, sizeof p->name, "%p", start);
				else
					strncpy(p->name, tok[4], sizeof p->name);
				p->next = traces;
				traces = p;
			}else if(!strcmp(tok[2], "remove")){
				if(ntok != 3)
					error("devtrace: usage: trace <name> remove");
				if(p == nil)
					error(notfound);
				traceoff(p);
				removetrace(p);
			}else if(!strcmp(tok[2], "on")){
				if(ntok != 3)
					error("devtrace: usage: trace <name> on");
				if(p == nil)
					error(notfound);
				traceon(p);
			}else if(!strcmp(tok[2], "off")){
				if(ntok != 3) 
					error("devtrace: usage: trace <name> off");
				if(p == nil)
					error(notfound);
				traceoff(p);
			}else
				error(Ebadarg);
		}else if(!strcmp(tok[0], "size")){
			if(ntok != 2)
				error("devtrace: usage: size <exponent>");	
			l = 1 << strtoul(tok[1], &ep, 0);
			if(*ep || l < 0x10000000)
				error(badaddr);
			lg = malloc(sizeof *lg * l);
			if(lg == nil)
				error(Enomem);
			free(tracelog);
			tracelog = lg;
			logsize = l;
			logmask = l - 1;
			pr = pw = 0;
		}else if(!strcmp(tok[0], "watch")){
			if(ntok != 2)
				error("devtrace: usage: watch [0|pid]");
			pid = atoi(tok[1]);
			if(pid == 0)
				numpids = 0;
			else if(numpids == Maxpidwatch)
				error("pidwatch array full!");
			else{
				for(w=pidwatch+numpids; w > pidwatch && *(w-1) > pid; w--)
					*w = *(w-1);
				*w = pid;
				numpids++;
			}
		}else if(!strcmp(tok[0], "start")){
			if(ntok != 1)
				error("devtrace: usage: start");
			saveactive = 1;
		}else if(!strcmp(tok[0], "stop")){
			if(ntok != 1)
				error("devtrace: usage: stop");
			saveactive = 0;
			all = 0;
		}else if(!strcmp(tok[0], "all")){
			if(ntok != 1)
				error("devtrace: usage: all");
			saveactive = 1;
			all = 1;
		}else
			error("devtrace:  usage: 'trace' [ktextaddr|name] 'on'|'off'|'mk'|'del' [name] or:  'size' buffersize (power of 2)");
		break;
	}
	poperror();
	qunlock(&traceslock);
	if(s != nil)
		free(s);
	traceactive = saveactive;
	return n;
}

Dev tracedevtab = {
	'T',
	"trace",
	devreset,
	devinit,
	devshutdown,
	traceattach,
	tracewalk,
	tracestat,
	traceopen,
	devcreate,
	traceclose,
	traceread,
	devbread,
	tracewrite,
	devbwrite,
	devremove,
	devwstat,
};
