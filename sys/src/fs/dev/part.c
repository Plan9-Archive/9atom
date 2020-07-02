#include	"all.h"
#include	"mem.h"
#include	"io.h"
#include "../pc/dosfs.h"

#define dprint(...)	if(pdebug)print(__VA_ARGS__)

enum{
	Npart	= 8,
	Ntab	= 8,
	Sblank	= 0,
	Eblank	= 0,
};

typedef struct{
	char	type;
	uchar	rem;			/* unaligned partition */
	Devsize	start;
	Devsize	end;
	char	name[NAMELEN];
}Part;

typedef struct{
	char	devstr[NAMELEN];
	int	n;
	Part	tab[Npart];
}Tab;

static Tab	*tab;
static int		ntab;
static int		pdebug = 0;

static void
initpart(void)
{
	static int done;

	if(done++)
		return;
	tab = ialloc(Ntab*sizeof *tab, 0);
}

/*
 * each Device has one partition table, even if d->dno is different.
 */

Tab*
devtotab(Device *d, int *new)
{
	char *s, buf[NAMELEN];
	int i;
	Tab *t;

	initpart();
	snprint(buf, sizeof buf, "%Z", d);
	for(i = 0; i < Ntab; i++){
		t = tab+i;
		s = t->devstr;
		if(*s == 0){
			memmove(s, buf, sizeof buf);
			*new = 1;
			return t;
		}else if(!strcmp(buf, s))
			return t;
	}
	panic("too many partitioned devices");
	return 0;
}

Devsize
sectooff(Device *d, uvlong sec)
{
	return (sec*devsecsize(d))/RBUFSIZE;
}

uvlong
offtosec(Device *d, Off off)
{
	return ((uvlong)off*RBUFSIZE)/devsecsize(d);
}

Part*
addpart(Device *parent, Device *d, char *s, uvlong a, uvlong b, int sec)
{
	uint sperrb;
	Tab *t;
	Part *p;

	dprint("  %Z %s [%lld, %lld) -> ", d, s, a, b);
	t = parent->private;
	if(t->n+1 == Npart){
		print("too many partitions; part %s %lld %lld dropped\n", s, a, b);
		return t->tab+t->n;
	}
	p = t->tab+t->n++;
	sperrb = offtosec(d, 1);
	if(sperrb > 0)
		p->rem = a%sperrb;
	else
		p->rem = 0;
	if(sec){
		p->type = 's';
		p->start = sectooff(d, a+sperrb-1)+Sblank;		/* round up */
		p->end = sectooff(d, b&~(sperrb-1))-Eblank;	/* round down */
	}else{
		p->type = 'o';
		p->start = a;
		p->end = b;
	}
	if(p->end < p->start)
		print("bad partition %s %lld not < %lld\n", s, p->start, p->end);
	strncpy(p->name, s, NAMELEN);
	dprint("[%lld, %lld)\n", p->start, p->end);
	return p;
}

/*
 * read raw disk; only used for groking partition tables & reading
 * fat during configuration.  very inefficient.
 */
static int
tailio(Device *d, int write, uvlong byte, ulong l, void *buf)
{
	int r;
	ulong rem;
	Devsize off;
	Msgbuf *t;

	t = mballoc(RBUFSIZE, 0, Mxxx);
	rem = byte&(RBUFSIZE-1);
	off = byte/RBUFSIZE;

//	print("tailio(%Z, %c, byte %llud [b %llud], %lud)\n", d, "rw"[write], byte, off, rem);

	r = devread(d, off, t->data);
	if(!r && write){
		memmove(t->data+rem, buf, l);
		r = devwrite(d, off, t->data);
	}else if(!r)
		memmove(buf, t->data+rem, l);
	mbfree(t);
	return r;
}

Off
byteio0(Device *d, int write, uvlong byte, ulong l, void *vbuf)
{
	uchar *buf;
	ulong rem, l0;
	int (*io)(Device*, Off, void*);

	l0 = l;
	buf = vbuf;
	io = write? devwrite: devread;
	rem = RBUFSIZE - (byte%RBUFSIZE);
	if(rem){
		if(rem > l)
			rem = l;
		if(tailio(d, write, byte, rem, buf))
			goto done;
		byte += rem;
		buf += rem;
		l -= rem;
	}
	while(l >= RBUFSIZE){
		if(io(d, byte/RBUFSIZE, buf))
			goto done;
		byte += RBUFSIZE;
		buf += RBUFSIZE;
		l -= RBUFSIZE;
	}
	if(l){
		if(byte%RBUFSIZE != 0)
			panic("byte%%rbufsize");
		if(tailio(d, write, byte, l, buf))
			goto done;
		byte += l;
//		buf += l;
		l -= l;
	}
done:
	return l0-l;
}

int
secio(Device *d, int write, uvlong sec, void *buf)
{
	return byteio0(d, write, sec*512, 512, buf) != 512;
}


/*
 * deal with dos partitions placed on odd-sized boundaries.
 * to further our misfortune, byteio0 can't deal with negative
 * offsets.
 */
static Part *findpart(Device*, char*);
Off
byteio(Device *d, int write, uvlong byte, ulong l, void *vbuf)
{
	uvlong rem;
	Part *p;

	if(d->type == Devpart){
		p = findpart(d, d->part.name);
		rem = p->rem*devsecsize(d);
		if(rem)
			byte -= RBUFSIZE-rem;
		byte -= Sblank*RBUFSIZE;
		byte += d->part.base*RBUFSIZE;
		d = d->part.d;
	}
	return byteio0(d, write, byte, l, vbuf);
}

int
mbrread(Device *d, uvlong sec, void *buf)
{
	uchar *u;

	if(byteio(d, 0, sec*512, 512, buf) != 512)
		return 1;
	u = buf;
	if(u[0x1fe] != 0x55 || u[0x1ff] != 0xaa)
		return 1;
	return 0;
}

static int
p9part(Device *parent, Device *d, uvlong sec, char *buf)
{
	char *field[4], *line[Npart+1];
	uvlong start, end;
	int i, n;

	if(secio(d, 0, sec+1, buf))
		return 1;
	buf[512-1] = '\0';
	if(strncmp(buf, "part ", 5))
		return 1;

	n = getfields(buf, line, Npart+1, 1, "\n");
	dprint("p9part %d lines..", n);
	if(n == 0)
		return -1;
	for(i = 0; i < n; i++){
		if(strncmp(line[i], "part ", 5) != 0)
			break;
		if(getfields(line[i], field, 4, 0, " ") != 4)
			break;
		start = strtoull(field[2], 0, 0);
		end = strtoull(field[3], 0, 0);
		if(start >= end || end > offtosec(d, d->size))
			break;
		addpart(parent, d, field[1], sec+start, sec+end, 1);
	}
	return 0;
}

int
isdos(int t)
{
	return t==FAT12 || t==FAT16 || t==FATHUGE || t==FAT32 || t==FAT32X;
}

int
isextend(int t)
{
	return t==EXTEND || t==EXTHUGE || t==LEXTEND;
}

static int
mbrpart(Device *parent, Device *d, char *mbrbuf, char *partbuf)
{
	char name[10];
	int ndos, i, nplan9;
	ulong sec, start, end;
	ulong firstx, nextx, npart;
	Dospart *dp;
	int (*repart)(Device*, Device*, uvlong, char*);

	sec = 0;
	dp = (Dospart*)&mbrbuf[0x1be];

	/* get the MBR (allowing for DMDDO) */
	if(mbrread(d, sec, mbrbuf))
		return 1;
	for(i=0; i<4; i++)
		if(dp[i].type == DMDDO) {
			dprint("DMDDO %d\n", i);
			sec = 63;
			if(mbrread(d, sec, mbrbuf))
				return 1;
			i = -1;	/* start over */
		}
	/*
	 * Read the partitions, first from the MBR and then
	 * from successive extended partition tables.
	 */
	nplan9 = 0;
	ndos = 0;
	firstx = 0;
	for(npart=0;; npart++) {
		if(mbrread(d, sec, mbrbuf))
			return 1;
		if(firstx)
			print("%Z ext %lud ", d, sec);
		else
			print("%Z mbr ", d);
		nextx = 0;
		for(i=0; i<4; i++) {
			start = sec+GLONG(dp[i].start);
			end = start+GLONG(dp[i].len);
			if(dp[i].type == 0 && start == 0 && end == 0)
				continue;
			dprint("type %x [%ld, %ld)", dp[i].type, start, end);
			repart = 0;
			if(dp[i].type == PLAN9) {
				if(nplan9 == 0)
					strcpy(name, "plan9");
				else
					sprint(name, "plan9.%d", nplan9);
				repart = p9part;
				nplan9++;
			}else if(!ndos && isdos(dp[i].type)){
				ndos = 1;
				strcpy(name, "dos");
			}else
				snprint(name, sizeof name, "%ld", npart);
			if(end != 0){
				dprint(" %s..", name);
				addpart(parent, d, name, start, end, 1);
			}
			if(repart)
				repart(parent, d, start, partbuf);
			
			/* nextx is relative to firstx (or 0), not sec */
			if(isextend(dp[i].type)){
				nextx = start-sec+firstx;
				dprint("link %lud...", nextx);
			}
		}
		dprint("\n");
		if(!nextx)
			break;
		if(!firstx)
			firstx = nextx;
		sec = nextx;
	}	
	return 0;
}

static int
guessparttab(Tab *t)
{
	int i, c;


	for(i = 0; i < t->n; i++){
		c = t->tab[i].type;
		if(c == 's' || c == 'o')
			return 1;
	}
	return 0;
}

static Part*
findpart(Device *d, char *s)
{
	char c;
	int i;
	uvlong l, start, end;
	Part *p;
	Tab *t;

	t = d->private;
	if(s == 0)
		goto mkpart;
	for(i = 0; i < t->n; i++)
		if(!strcmp(t->tab[i].name, s))
			return t->tab+i;
	panic("part %Z not found", d);
mkpart:
	if(guessparttab(t))
		print("warning: ignoring part table on %Z\n", d->part.d);
	if(d->part.base < 101 && d->part.size < 101){
		c = '%';
		l = d->part.d->size / 100;
		start = d->part.base*l;
		end = start + d->part.size*l;
	}else{
		c = 'b';
		start = d->part.base;
		end = d->part.size;
	}
	for(i = 0; i < t->n; i++){
		p = t->tab+i;
		if(start == p->start)
		if(end == p->end)
			return p;
	}
	p = addpart(d, d->part.d, "", start, end, 0);
	if(c)
		p->type = c;
	snprint(p->name, sizeof p->name, "f%ld%ld", t-tab, p-t->tab);	// BOTCH
	return p;
}

void
partition(Device *parent, Device *d)
{
	char *m, *p;
	int new;
	Msgbuf *mbr, *part;
	Part *q;

	new = 0;
	parent->private = devtotab(d, &new);
	if(new){
		mbr = mballoc(RBUFSIZE, 0, Mxxx);
		part = mballoc(RBUFSIZE, 0, Mxxx);
		m = (char*)mbr->data;
		p = (char*)part->data;
		!mbrpart(parent, d, m, p) || p9part(parent, d, 0, p);
		mbfree(mbr);
		mbfree(part);
	}
	q = findpart(parent, parent->part.name);
	parent->part.base = q->start;
	parent->part.size = q->end-q->start;
}

void
cmd_part(int argc, char **argv)
{
	int i, j;
	Part *p;
	Tab *t;

	if(argc == 1 && !strcmp(*argv, "trace")){
		pdebug ^= 1;
		return;
	}
	for(i = 0; i < Ntab; i++){
		t = tab+i;
		if(*t->devstr == 0)
			continue;
		print("%d %s\n", i, t->devstr);
		for(j = 0; j < Npart; j++){
			p = t->tab+j;
			if(*p->name == 0 && p->start == 0 && p->end == 0)
				continue;
			print("  %c\t%s\t%llud\t%llud r %d\n", p->type, p->name, p->start, p->end, p->rem);
		}
	}
}

void
partinit(Device *d)
{
	static int once;

	if(once++ == 0)
		cmd_install("part", "-- partition info", cmd_part);
	devinit(d->part.d);
	d->part.d->size = devsize(d->part.d);
	partition(d, d->part.d);
}

Devsize
partsize(Device *d)
{
	return d->part.size;
}

int
partread(Device *d, Off b, void *c)
{
	if(b < d->part.size)
		return devread(d->part.d, d->part.base+b, c);
	print("partread %llud %llud\n", (Wideoff)b, d->part.size);
	return 1;
}

int
partwrite(Device *d, Off b, void *c)
{
	if(b < d->part.size)
		return devwrite(d->part.d, d->part.base+b, c);
	print("partwrite %llud %llud\n", (Wideoff)b, d->part.size);
	return 1;
}

void
partream(Device *d, int)
{
	devream(d->part.d, 0);
}
