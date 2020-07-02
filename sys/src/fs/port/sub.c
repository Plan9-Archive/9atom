#include	"all.h"
#include	"io.h"
#include 	"mem.h"
#include	"../ip/ip.h"

Filsys*
fsstr(char *p)
{
	Filsys *fs;

	for(fs=filsys; fs->name; fs++)
		if(strcmp(fs->name, p) == 0)
			return fs;
	return 0;
}

Filsys*
dev2fs(Device *dev)
{
	Filsys *fs;

	for(fs=filsys; fs->name; fs++)
		if(fs->dev == dev)
			return fs;
	return 0;
}

/*
 * allocate 'count' contiguous channels
 * of type 'type' and return pointer to base
 */
Chan*
chaninit(int type, int count, int data)
{
	uchar *p;
	Chan *cp, *icp;
	int i;
	static Lock lk;

	p = ialloc(count * (sizeof(Chan)+data), 0);
	icp = (Chan*)p;
	for(i=0; i<count; i++) {
		cp = (Chan*)p;
		cp->next = chans;
		chans = cp;
		cp->type = type;
		cp->chan = cons.chano;
		lock(&lk);
		cons.chano++;
		unlock(&lk);
		strncpy(cp->whoname, "<none>", sizeof(cp->whoname));
		dofilter(&cp->work);
		dofilter(&cp->rate);
		snprint(cp->rname, sizeof cp->rname, "rch%d/%d", type, cp->chan);
		snprint(cp->wname, sizeof cp->wname, "wch%d/%d", type, cp->chan);
		cp->reflock.rd.name = cp->rname;
		cp->reflock.wr.name = cp->wname;
		wlock(&cp->reflock);
		wunlock(&cp->reflock);
		rlock(&cp->reflock);
		runlock(&cp->reflock);

		p += sizeof(Chan);
		if(data){
			cp->pdata = p;
			p += data;
		}
	}
	return icp;
}

void
fileinit(Chan *cp)
{
	File *f, *prev;
	Tlock *t;
	int h;

loop:
	lock(&flock);
	for (h=0; h<nelem(flist); h++)
		for (prev=0, f=flist[h]; f; prev=f, f=f->next) {
			if(f->cp != cp)
				continue;
			if(prev) {
				prev->next = f->next;
				f->next = flist[h];
				flist[h] = f;
			}
			flist[h] = f->next;
			unlock(&flock);

			qlock(f);
			if(t = f->tlock) {
				if(t->file == f)
					t->time = 0;	/* free the lock */
				f->tlock = 0;
			}
			if(f->open & FREMOV)
				doremove(f);
			freewp(f->wpath);
			f->open = 0;
			authfree(f->auth);
			f->auth = 0;
			f->cp = 0;
			qunlock(f);
			goto loop;
		}
	unlock(&flock);
}

#define NOFID (ulong)~0

/*
 * returns a locked file structure
 */
File*
filep(Chan *cp, ulong fid, int flag)
{
	File *f;
	int h;

	if(fid == NOFID)
		return 0;

	h = (long)(uintptr)cp + fid;
	if(h < 0)
		h = ~h;
	h %= nelem(flist);

loop:
	lock(&flock);
	for(f=flist[h]; f; f=f->next)
		if(f->fid == fid && f->cp == cp){
			/*
			 * Already in use is an error
			 * when called from attach or clone (walk
			 * in 9P2000). The console uses FID[12] and
			 * never clunks them so catch that case.
			 */
			if(flag == 0 || cp == cons.chan)
				goto out;
			unlock(&flock);
			return 0;
		}

	if(flag) {
		f = newfp();
		if(f) {
			f->fid = fid;
			f->cp = cp;
			f->wpath = 0;
			f->tlock = 0;
			f->doffset = 0;
			f->dslot = 0;
			f->auth = 0;
			f->next = flist[h];
			flist[h] = f;
			goto out;
		}
	}
	unlock(&flock);
	return 0;

out:
	unlock(&flock);
	qlock(f);
	if(f->fid == fid && f->cp == cp)
		return f;
	qunlock(f);
	goto loop;
}

/*
 * always called with flock locked
 */
File*
newfp(void)
{
	static int first;
	File *f;
	int start, i;

	i = first;
	start = i;
	do {
		f = &files[i];
		i++;
		if(i >= conf.nfile)
			i = 0;
		if(f->cp)
			continue;
		first = i;
		return f;
	} while(i != start);

	print("out of files\n");
	return 0;
}

void
freefp(File *fp)
{
	Chan *cp;
	File *f, *prev;
	int h;

	if(!fp || !(cp = fp->cp))
		return;

	h = (long)(uintptr)cp + fp->fid;
	if(h < 0)
		h = ~h;
	h %= nelem(flist);

	lock(&flock);
	for(prev=0,f=flist[h]; f; prev=f,f=f->next)
		if(f == fp) {
			if(prev)
				prev->next = f->next;
			else
				flist[h] = f->next;
			break;
		}
	fp->cp = 0;
	unlock(&flock);
}

int
iaccess(File *f, Dentry *d, int m)
{

	/* uid none gets only other permissions */
	if(f->uid != 0) {
		/*
		 * owner
		 */
		if(f->uid == d->uid)
			if((m<<6) & d->mode)
				return 0;
		/*
		 * group membership
		 */
		if((m<<3) & d->mode)
		if(ingroup(f->uid, d->gid))
				return 0;
	}

	/*
	 * other
	 */
	if(m & d->mode) {
		if((d->mode & DDIR) && (m == DEXEC))
			return 0;
		if(!ingroup(f->uid, 9999))
			return 0;
	}

	/*
	 * various forms of superuser
	 */
	if(f->cp == cons.chan)
		return 0;
	if(ALLOW(f->cp))
		return 0;
	if(duallow != 0 && duallow == f->uid)
		if((d->mode & DDIR) && (m == DREAD || m == DEXEC))
			return 0;

	return 1;
}

Tlock*
tlocked(Iobuf *p, Dentry *d)
{
	Tlock *t, *t1;
	Off qpath;
	Timet tim;
	Device *dev;

	tim = toytime();
	qpath = d->qid.path;
	dev = p->dev;

again:
	t1 = 0;
	for(t=tlocks+NTLOCK-1; t>=tlocks; t--) {
		if(t->qpath == qpath)
		if(t->time >= tim)
		if(t->dev == dev)
			return nil;		/* its locked */
		if(t1 != nil && t->time == 0)
			t1 = t;			/* remember free lock */
	}
	if(t1 == 0) {
		// reclaim old locks
		lock(&tlocklock);
		for(t=tlocks+NTLOCK-1; t>=tlocks; t--)
			if(t->time < tim) {
				t->time = 0;
				t1 = t;
			}
		unlock(&tlocklock);
	}
	if(t1) {
		lock(&tlocklock);
		if(t1->time != 0) {
			unlock(&tlocklock);
			goto again;
		}
		t1->dev = dev;
		t1->qpath = qpath;
		t1->time = tim + TLOCK;
		unlock(&tlocklock);
	}
	/* botch
	 * out of tlock nodes simulates
	 * a locked file
	 */
	return t1;
}

Wpath*
newwp(void)
{
	static int si = 0;
	int i;
	Wpath *w, *sw, *ew;

	i = si + 1;
	if(i < 0 || i >= conf.nwpath)
		i = 0;
	si = i;
	sw = &wpaths[i];
	ew = &wpaths[conf.nwpath];
	for(w=sw;;) {
		w++;
		if(w >= ew)
			w = &wpaths[0];
		if(w == sw) {
			print("out of wpaths\n");
			return 0;
		}
		if(w->refs)
			continue;
		lock(&wpathlock);
		if(w->refs) {
			unlock(&wpathlock);
			continue;
		}
		w->refs = 1;
		w->up = 0;
		unlock(&wpathlock);
		return w;
	}

}

void
freewp(Wpath *w)
{
	lock(&wpathlock);
	for(; w; w=w->up)
		w->refs--;
	unlock(&wpathlock);
}

Off
qidpathgen(Device *dev)
{
	Iobuf *p;
	Superb *sb;
	Off path;

	p = getbuf(dev, superaddr(dev), Bread|Bmod);
	if(!p || checktag(p, Tsuper, QPSUPER))
		panic("newqid: super block");
	sb = (Superb*)p->iobuf;
	sb->qidgen++;
	path = sb->qidgen;
	putbuf(p);
	return path;
}

/* truncating to length > 0 */
static void
truncfree(Truncstate *ts, Device *dev, int d, Iobuf *p, int i)
{
	int pastlast;
	Off a;

	pastlast = ts->pastlast;
	a = ((Off *)p->iobuf)[i];
	if (d > 0 || pastlast)
		buffree(dev, a, d, ts);
	if (pastlast) {
		((Off *)p->iobuf)[i] = 0;
		p->flags |= Bmod|Bimm;
	} else if (d == 0 && ts->relblk == ts->lastblk)
		ts->pastlast = 1;
	if (d == 0)
		ts->relblk++;
}

/*
 * free the block at `addr' on dev.
 * if it's an indirect block (d [depth] > 0),
 * first recursively free all the blocks it names.
 *
 * ts->relblk is the block number within the file of this
 * block (or the first data block eventually pointed to via
 * this indirect block).
 */
void
buffree(Device *dev, Off addr, int d, Truncstate *ts)
{
	Iobuf *p;
	Off a;
	int i, pastlast;

	if(!addr)
		return;
	pastlast = (ts == nil? 1: ts->pastlast);
	/*
	 * if this is an indirect block, recurse and free any
	 * suitable blocks within it (possibly via further indirect blocks).
	 */
	if(d > 0) {
		d--;
		p = getbuf(dev, addr, Bread);
		if(p) {
			if (ts == nil)		/* common case: create */
				for(i=INDPERBUF-1; i>=0; i--) {
					a = ((Off *)p->iobuf)[i];
					buffree(dev, a, d, nil);
				}
			else			/* wstat truncation */
				for (i = 0; i < INDPERBUF; i++)
					truncfree(ts, dev, d, p, i);
			putbuf(p);
		}
	}
	if (!pastlast)
		return;
	/*
	 * having zeroed the pointer to this block, add it to the free list.
	 * stop outstanding i/o
	 */
	p = getbuf(dev, addr, Bprobe);
	if(p) {
		p->flags &= ~(Bmod|Bimm);
		putbuf(p);
	}
	/*
	 * dont put written worm
	 * blocks into free list
	 */
	if(dev->type == Devcw) {
		i = cwfree(dev, addr);
		if(i)
			return;
	}
	p = getbuf(dev, superaddr(dev), Bread|Bmod);
	if(!p || checktag(p, Tsuper, QPSUPER))
		panic("buffree: super block");
	addfree(dev, addr, (Superb*)p->iobuf);
	putbuf(p);
}

Off
bufalloc(Device *dev, int tag, long qid, int uid)
{
	Iobuf *bp, *p;
	Superb *sb;
	Off a, n;

	p = getbuf(dev, superaddr(dev), Bread|Bmod);
	if(!p || checktag(p, Tsuper, QPSUPER)) {
		print("bufalloc: super block\n");
		if(p)
			putbuf(p);
		return 0;
	}
	sb = (Superb*)p->iobuf;

loop:
	n = --sb->fbuf.nfree;
	sb->tfree--;
	if(n < 0 || n >= FEPERBUF) {
		print("bufalloc: %Z: bad freelist\n", dev);
		n = 0;
		sb->fbuf.free[0] = 0;
	}
	a = sb->fbuf.free[n];
	if(n <= 0) {
		if(a == 0) {
			sb->tfree = 0;
			sb->fbuf.nfree = 1;
			if(dev->type == Devcw) {
				n = uid;
				if(n < 0 || n >= nelem(growacct))
					n = 0;
				growacct[n]++;
				if(cwgrow(dev, sb, uid))
					goto loop;
			}
			putbuf(p);
			print("fs %Z full uid=%d\n", dev, uid);
			return 0;
		}
		bp = getbuf(dev, a, Bread);
		if(!bp || checktag(bp, Tfree, QPNONE)) {
			if(bp)
				putbuf(bp);
			putbuf(p);
			return 0;
		}
		sb->fbuf = *(Fbuf*)bp->iobuf;
		putbuf(bp);
	}

	bp = getbuf(dev, a, Bmod);
	memset(bp->iobuf, 0, RBUFSIZE);
	settag(bp, tag, qid);
	if(tag == Tdir || (tag >= Tind1 && tag <= Tmaxind))
		bp->flags |= Bimm;
	putbuf(bp);
	putbuf(p);
	return a;
}

/*
 * what are legal characters in a name?
 * only disallow control characters.
 * a) utf avoids control characters.
 * b) '/' may not be the separator
 */
int
checkname(char *n)
{
	int i, c;

	for(i=0; i<NAMELEN; i++) {
		c = *n & 0xff;
		if(c == 0) {
			if(i == 0)
				return 1;
			memset(n, 0, NAMELEN-i);
			return 0;
		}
		if(c <= 040)
			return 1;
		n++;
	}
	return 1;	/* too long */
}

void
addfree(Device *dev, Off addr, Superb *sb)
{
	int n;
	Iobuf *p;

	n = sb->fbuf.nfree;
	if(n < 0 || n > FEPERBUF)
		panic("addfree: bad freelist");
	if(n >= FEPERBUF) {
		p = getbuf(dev, addr, Bmod|Bimm);
		if(p == 0)
			panic("addfree: getbuf");
		*(Fbuf*)p->iobuf = sb->fbuf;
		settag(p, Tfree, QPNONE);
		putbuf(p);
		n = 0;
	}
	sb->fbuf.free[n++] = addr;
	sb->fbuf.nfree = n;
	sb->tfree++;
	if(addr >= sb->fsize)
		sb->fsize = addr+1;
}

static int
Yfmt(Fmt* fmt)
{
	Chan *cp;
	char s[20];

	cp = va_arg(fmt->args, Chan*);
	sprint(s, "C%d.%.3d", cp->type, cp->chan);

	return fmtstrcpy(fmt, s);
}

static int
Zfmt(Fmt* fmt)
{
	Device *d;
	int c, c1;
	char s[100];

	d = va_arg(fmt->args, Device*);
	if(d == 0) {
		sprint(s, "Z***");
		goto out;
	}
	switch(d->type) {
	default:
		sprint(s, "D%d", d->type);
		break;
	case Devwren:
	case Devide:
	case Devmv:
	case Devia:
	case Devworm:
	case Devlworm:
		c = devtab[d->type].c;
		if(d->wren.ctrl == 0 && d->wren.lun == 0)
			sprint(s, "%c%d", c, d->wren.targ);
		else
			sprint(s, "%c%d.%d.%d", c, d->wren.ctrl, d->wren.targ, d->wren.lun);
		break;
	case Devaoe:
		if(d->wren.ctrl != 0)
			panic("nonzero aoe controller");
		sprint(s, "e%d.%d", d->wren.targ, d->wren.lun);
		break;
	case Devmcat:
	case Devmlev:
	case Devmirr:
		c = devtab[d->type].c;
		c1 =devtab[d->type].c1;
		if(d->cat.first == d->cat.last)
			sprint(s, "%c%Z%c", c, d->cat.first, c1);
		else if(d->cat.first->link == d->cat.last)
			sprint(s, "%c%Z%Z%c", c, d->cat.first, d->cat.last, c1);
		else
			sprint(s, "%c%Z-%Z%c", c, d->cat.first, d->cat.last, c1);
		break;
	case Devro:
		sprint(s, "o%Z%Z", d->ro.parent->cw.c, d->ro.parent->cw.w);
		break;
	case Devcw:
		sprint(s, "c%Z%Z", d->cw.c, d->cw.w);
		break;
	case Devjuke:
		sprint(s, "j%Z%Z", d->j.j, d->j.m);
		break;
	case Devfworm:
		sprint(s, "f%Z", d->fw.fw);
		break;
	case Devpart:
		if(d->part.name)
			sprint(s, "p%Z\"%s\"", d->part.d, d->part.name);
		else if(d->part.base < 101)
			sprint(s, "p(%Z)%ulld.%ulld", d->part.d, d->part.base, d->part.size);
		else
			sprint(s, "p(%Z)%ulld.%ulld", d->part.d, d->part.base, d->part.base+d->part.size);
		break;
	case Devswab:
		sprint(s, "x%Z", d->swab.d);
		break;
	case Devnone:
		sprint(s, "n");
		break;
	}
out:
	return fmtstrcpy(fmt, s);
}

static int
Wfmt(Fmt* fmt)
{
	Filter* a;
	char s[60];

	a = va_arg(fmt->args, Filter*);
	snprint(s, sizeof s, "%9lud %9lud %9lud", 
		fdf(a->filter[0], 60),
		fdf(a->filter[1], 600),
		fdf(a->filter[2], 6000));
	return fmtstrcpy(fmt, s);
}

static int
wfmt(Fmt *fmt)
{
	Filter *a;
	char s[12];
	int n;

	n = 60;
	if(fmt->flags&FmtPrec)
		n = fmt->prec;
	a = va_arg(fmt->args, Filter*);
	snprint(s, sizeof s, "%9lud", fdf(a->filter[0], n));
	return fmtstrcpy(fmt, s);
}
	
static int
Gfmt(Fmt* fmt)
{
	int t;
	char *s;

	t = va_arg(fmt->args, int);
	s = "<badtag>";
	if(t >= 0 && t < MAXTAG)
		s = tagnames[t];
	return fmtstrcpy(fmt, s);
}

static int
Efmt(Fmt* fmt)
{
	char s[64];
	uchar *p;

	p = va_arg(fmt->args, uchar*);
	sprint(s, "%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux",
		p[0], p[1], p[2], p[3], p[4], p[5]);

	return fmtstrcpy(fmt, s);
}

static int
Ifmt(Fmt* fmt)
{
	char s[64];
	uchar *p;

	p = va_arg(fmt->args, uchar*);
	sprint(s, "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);

	return fmtstrcpy(fmt, s);
}

static char *ipflagtab[] = {
[0]	"cec",
[1]	"aoe",
[2]	"aoejumbo",
};

int
φfmt(Fmt *f)
{
	int flag, i;
	char buf[32], *p, *e;

	flag = va_arg(f->args, int);

	p = buf;
	e = p+sizeof buf;
	for(i = 0; i < nelem(ipflagtab); i++)
		if(flag&1<<i)
		if(ipflagtab[i])
			p = seprint(p, e, "%s ", ipflagtab[i]);
	if(p != buf)
		*--p = 0;
	return fmtstrcpy(f, buf);
}

int
strtoipflag(char *p)
{
	int i;

	for(i = 0; i < nelem(ipflagtab); i++)
		if(ipflagtab[i])
		if(strcmp(ipflagtab[i], p) == 0)
			return 1<<i;
	return -1;
}

void
formatinit(void)
{
	quotefmtinstall();
	fmtinstall('Y', Yfmt);	/* print channels */
	fmtinstall('Z', Zfmt);	/* print devices */
	fmtinstall('W', Wfmt);	/* print filters */
	fmtinstall('w', wfmt);	/* " */
	fmtinstall('G', Gfmt);	/* print tags */
	fmtinstall('T', Tfmt);	/* print times */
	fmtinstall('E', Efmt);	/* print ether addresses */
	fmtinstall('I', Ifmt);	/* print ip addresses */
	fmtinstall(L'φ', φfmt);	/* print ip flags */
}

void
rootream(Device *dev, Off addr)
{
	Iobuf *p;
	Dentry *d;

	p = getbuf(dev, addr, Bmod|Bimm);
	memset(p->iobuf, 0, RBUFSIZE);
	settag(p, Tdir, QPROOT);
	d = getdir(p, 0);
	strcpy(d->name, "/");
	d->uid = -1;
	d->gid = -1;
	d->mode = DALLOC | DDIR |
		((DREAD|DEXEC) << 6) |
		((DREAD|DEXEC) << 3) |
		((DREAD|DEXEC) << 0);
	d->qid = QID9P1(QPROOT|QPDIR,0);
	d->atime = time();
	d->mtime = d->atime;
	d->muid = 0;
	putbuf(p);
}

void
superream(Device *dev, Off addr)
{
	Iobuf *p;
	Superb *s;
	Off i;

	p = getbuf(dev, addr, Bmod|Bimm);
	memset(p->iobuf, 0, RBUFSIZE);
	settag(p, Tsuper, QPSUPER);

	s = (Superb*)p->iobuf;
	s->fstart = 2;
	s->fsize = devsize(dev);
	s->fbuf.nfree = 1;
	s->qidgen = 10;
#ifdef AUTOSWAB
	s->magic = 0x123456789abcdef0;
#endif
	print("superream %lld bufs\n", s->fsize-1);
	for(i=s->fsize-1; i>=addr+2; i--){
		if((i&(512*1024-1)) == 0)
			print("%llud  ", i);
		addfree(dev, i, s);
	}
	print("done\n");
	putbuf(p);
}

struct
{
	Lock;
	Msgbuf	*smsgbuf;
	Msgbuf	*lmsgbuf;
} msgalloc;

/*
 * pre-allocate some message buffers at boot time.
 * if this supply is exhausted, more will be allocated as needed.
 */
void
mbinit(void)
{
	Msgbuf *mb;
	Rabuf *rb;
	int i;

	lock(&msgalloc);
	unlock(&msgalloc);
	msgalloc.lmsgbuf = 0;
	msgalloc.smsgbuf = 0;
	for(i=0; i<conf.nlgmsg; i++) {
		mb = ialloc(sizeof(Msgbuf), 0);
		mb->xdata = ialloc(LARGEBUF+256, 256);
		mb->flags = LARGE;
		mb->free = 0;
		mbfree(mb);
		cons.nlarge++;
	}
	for(i=0; i<conf.nsmmsg; i++) {
		mb = ialloc(sizeof(Msgbuf), 0);
		mb->xdata = ialloc(SMALLBUF+256, 256);
		mb->flags = 0;
		mb->free = 0;
		mbfree(mb);
		cons.nsmall++;
	}
	memset(mballocs, 0, sizeof(mballocs));

	lock(&rabuflock);
	unlock(&rabuflock);
	rabuffree = 0;
	for(i=0; i<1000; i++) {
		rb = ialloc(sizeof(*rb), 0);
		rb->link = rabuffree;
		rabuffree = rb;
	}
}

Msgbuf*
mballoc(int count, Chan *cp, int category)
{
	Msgbuf *mb;

	ilock(&msgalloc);
	if(count > SMALLBUF) {
		if(count > LARGEBUF)
			panic("msgbuf count");
		mb = msgalloc.lmsgbuf;
		if(mb == 0) {
			mb = ialloc(sizeof(Msgbuf), 0);
			mb->xdata = ialloc(LARGEBUF+256, 256);
			cons.nlarge++;
		} else
			msgalloc.lmsgbuf = mb->next;
		mb->flags = LARGE;
	} else {
		mb = msgalloc.smsgbuf;
		if(mb == 0) {
			mb = ialloc(sizeof(Msgbuf), 0);
			mb->xdata = ialloc(SMALLBUF+256, 256);
			cons.nsmall++;
		} else
			msgalloc.smsgbuf = mb->next;
		mb->flags = 0;
	}
	mballocs[category]++;
	iunlock(&msgalloc);
	mb->count = count;
	mb->chan = cp;
	mb->next = 0;
	mb->param = 0;
	mb->category = category;
	mb->data = mb->xdata+256;
	mb->free = 0;
	return mb;
}

#define Round(s, n)	(((s)+(n-1))&~(n-1))
void
mballocpool(int n, int sz, int align, int category, void (*f)(Msgbuf*))
{
	int i;
	Msgbuf *a, *mb;

	/*
	 * put the Msgbuf in the tail of the allocation if it fits.  otherwise
	 * don't waste a whole align
	 */
	if(align != 0 && Round(sz, align)-sz >= sizeof *a && sizeof *a < align)
		a = 0;
	else
		a = ialloc(n*sizeof *a, 0);
	ilock(&msgalloc);
	for(i = 0; i < n; i++){
		if(a)
			mb = a+i;
		else
			mb =  ialloc(sizeof(Msgbuf), 0);
		mb->xdata = ialloc(sz, align);
		mb->free = f;
		mb->flags = 0;
		mballocs[category]++;
		mb->count = sz;
		mb->chan = 0;
		mb->next = 0;
		mb->param = 0;
		mb->category = category;
		mb->data = (uchar*)Round((uintptr)mb->xdata, align);
		mb->free(mb);
	}
	iunlock(&msgalloc);
}


void
mbfree(Msgbuf *mb)
{
	if(mb == nil)
		return;
	if(mb->flags & BTRACE)
		print("mbfree: BTRACE cat=%d flags=%ux, caller 0x%lux\n",
			mb->category, mb->flags, getcallerpc(&mb));
	if(mb->flags & FREE)
		panic("mbfree already free");

	/*
	 * drivers which perform non cache coherent DMA manage their own buffer
	 * pool of uncached buffers and provide their own free routine.
	 * this is provided mainly for ethernet drivers ported from cpu kernel.
	 */
	if(mb->free){
		mb->free(mb);
		return;
	}

	ilock(&msgalloc);
	mballocs[mb->category]--;
	mb->flags |= FREE;
	if(mb->flags & LARGE) {
		mb->next = msgalloc.lmsgbuf;
		msgalloc.lmsgbuf = mb;
	} else {
		mb->next = msgalloc.smsgbuf;
		msgalloc.smsgbuf = mb;
	}
	mb->data = 0;
	mb->free = 0;
	iunlock(&msgalloc);
}

/*
 * returns 1 if n is prime
 * used for adjusting lengths
 * of hashing things.
 * there is no need to be clever
 */
int
prime(vlong n)
{
	long i;

	if((n%2) == 0)
		return 0;
	for(i=3;; i+=2) {
		if((n%i) == 0)
			return 0;
		if((vlong)i*i >= n)
			return 1;
	}
}

char*
getwd(char *word, char *line)
{
	int c, n;

	while(*line == ' ')
		line++;
	for(n=0; n<Maxword; n++) {
		c = *line;
		if(c == ' ' || c == 0 || c == '\n')
			break;
		line++;
		*word++ = c;
	}
	*word = 0;
	return line;
}

void
hexdump(void *a, int n)
{
	char s1[30], s2[4];
	uchar *p;
	int i;

	p = a;
	s1[0] = 0;
	for(i=0; i<n; i++) {
		sprint(s2, " %.2ux", p[i]);
		strcat(s1, s2);
		if((i&7) == 7) {
			print("%s\n", s1);
			s1[0] = 0;
		}
	}
	if(s1[0])
		print("%s\n", s1);
}

void*
recv(Queue *q, int ret)
{
	User *p;
	void *a;
	int i, c;
	long s;

	if(q == 0)
		panic("recv null q");
	for(;;) {
		ilock(q);
		c = q->count;
		if(c > 0) {
			if(ret == 0){
				iunlock(q);
				return 0;
			}
			i = q->loc;
			a = q->args[i];
			i++;
			if(i >= q->size)
				i = 0;
			q->loc = i;
			q->count = c-1;
			p = q->whead;
			if(p) {
				q->whead = p->qnext;
				if(q->whead == 0)
					q->wtail = 0;
				iunlock(q);
				ready(p);
			}else
				iunlock(q);
			return a;
		}
		p = q->rtail;
		if(p == 0)
			q->rhead = u;
		else
			p->qnext = u;
		q->rtail = u;
		s = splhi();
		u->qnext = 0;
		u->state = Recving;
		splx(s);
		iunlock(q);
		sched();
	}
}

void
send(Queue *q, void *a)
{
	User *p;
	int i, c;
	long s;

	if(q == 0)
		panic("send null q %#p", getcallerpc(q));
	if(a == 0)
		panic("send null a %#p", getcallerpc(q));
	if(u == nil)
		panic("send with no u %#p", getcallerpc(q));
	if(u->nlock){
		print("send with locks %#p", getcallerpc(q));
		printlocks(u);
	}
	for(;;) {
		ilock(q);
		c = q->count;
		if(c < q->size) {
			i = q->loc + c;
			if(i >= q->size)
				i -= q->size;
			q->args[i] = a;
			q->count = c+1;
			p = q->rhead;
			if(p) {
				q->rhead = p->qnext;
				if(q->rhead == 0)
					q->rtail = 0;
				iunlock(q);
				ready(p);
			}else
				iunlock(q);
			return;
		}
		if(u->nlock)
			print("%d: q blocking nlock %ld\n", u->pid, u->nlock);
		p = q->wtail;
		if(p == 0)
			q->whead = u;
		else
			p->qnext = u;
		q->wtail = u;
		s = splhi();
		u->qnext = 0;
		u->state = Sending;
		splx(s);
		iunlock(q);
		sched();
	}
}

Queue*
newqueue(int size)
{
	Queue *q;

	q = ialloc(sizeof(Queue) + (size-1)*sizeof(void*), 0);
	q->size = size;
	lock(q);
	unlock(q);
	return q;
}

no(void*)
{
	return 0;
}
