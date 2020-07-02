#include	"all.h"
typedef struct Dir Dir;
#include	"fcall.h"

int	call9p2(Chan*, Fcall*, Fcall*, char*);

#define dprint(...)	//print(__VA_ARGS__)

int
fcall(Chan *cp, Fcall *in, Fcall *ou)
{
	char ename[64];
	int err;

	rlock(&mainlock);

	rlock(&cp->reflock);
	call9p2(cp, in, ou, ename);
	runlock(&cp->reflock);

	err = ou->type == Rerror;
	if(CHAT(cp) && ou->ename)
	if(err)
		print("	error: %s\n", ou->ename);

	runlock(&mainlock);

	return err;
}

int
con_session(void)
{
	Fcall in, ou;
	int r;

	dprint("con_session\n");
	in.type = Tauth;
	in.afid = 2112;
	in.aname = "main";
	in.uname = "adm";
	r = fcall(cons.chan, &in, &ou);
	return r;
}

int
con_attach(int fid, char *uid, char *arg)
{
	Fcall in, ou;
	int r;
	static int once;

	dprint("con_attach(uname %s, aname %s)\n", uid, arg);
	if(once++ != 0)
		goto f1;
	in.type = Tversion;
	in.version = VERSION9P;
	in.fid = fid;
	in.msize = MAXDAT+MAXMSG;
	r = fcall(cons.chan, &in, &ou);
	if(r)
		return r;
f1:
	in.type = Tattach;
	in.fid = fid;
	in.afid = 2112;
	in.uname = uid;
	in.aname = arg;
	return fcall(cons.chan, &in, &ou);
}

int
con_clone(int fid1, int fid2)
{
	Fcall in, ou;

	dprint("con_clone(%d, %d)\n", fid1, fid2);
	in.type = Twalk;
	in.fid = fid1;
	in.newfid = fid2;
	in.nwname = 0;
	return fcall(cons.chan, &in, &ou);
}

int
con_walk(int fid, char *name)
{
	Fcall in, ou;

	dprint("con_walk(%s)\n", name);
	in.type = Twalk;
	in.fid = fid;
	in.newfid = fid;				/* old school */
	in.nwname = getfields(name, in.wname, nelem(in.wname), 0, "/");
	return fcall(cons.chan, &in, &ou);
}

int
con_open(int fid, int mode)
{
	Fcall in, ou;

	dprint("con_open(%d, %.4x)\n", fid, mode);
	in.type = Topen;
	in.fid = fid;
	in.mode = mode;
	return fcall(cons.chan, &in, &ou);
}

int
con_read(int fid, char *data, Off offset, int count)
{
	Fcall in, ou;

	dprint("con_read(%d, data=%p, %lld, %d) from %p\n", fid, data, offset, count, getcallerpc(&fid));
	in.type = Tread;
	in.fid = fid;
	in.offset = offset;
	in.count = count;
	in.data = data;
	if(fcall(cons.chan, &in, &ou))
		return -1;
	return ou.count;
}

int
con_write(int fid, char *data, Off offset, int count)
{
	Fcall in, ou;

	dprint("con_write(%d, data %p, %lld, %d)\n", fid, data, offset, count);
	in.type = Twrite;
	in.fid = fid;
	in.data = data;
	in.offset = offset;
	in.count = count;
	if(fcall(cons.chan, &in, &ou))
		return -1;
	return ou.count;
}

int
con_remove(int fid)
{
	Fcall in, ou;

	dprint("con_remove\n");
	in.type = Tremove;
	in.fid = fid;
	return fcall(cons.chan, &in, &ou);
}

int
con_create(int fid, char *name, int uid, int gid, long perm, int mode)
{
	Fcall in, ou;

	dprint("con_create(%s, %d, %d)\n", name, uid, gid);
	in.type = Tcreate;
	in.fid = fid;
	in.name = name;
	in.perm = perm;
	in.mode = mode;
	cons.uid = uid;			/* beyond ugly */
	cons.gid = gid;
	return fcall(cons.chan, &in, &ou);
}

int
doclri(File *f)
{
	Iobuf *p, *p1;
	Dentry *d, *d1;
	int err;

	err = 0;
	p = 0;
	p1 = 0;
	if(f->fs->dev->type == Devro) {
		err = Eronly;
		goto out;
	}
	/*
	 * check on parent directory of file to be deleted
	 */
	if(f->wpath == 0 || f->wpath->addr == f->addr) {
		err = Ephase;
		goto out;
	}
	p1 = getbuf(f->fs->dev, f->wpath->addr, Bread);
	d1 = getdir(p1, f->wpath->slot);
	if(!d1 || checktag(p1, Tdir, QPNONE) || !(d1->mode & DALLOC)) {
		err = Ephase;
		goto out;
	}

	accessdir(p1, d1, FWRITE, 0);
	putbuf(p1);
	p1 = 0;

	/*
	 * check on file to be deleted
	 */
	p = getbuf(f->fs->dev, f->addr, Bread);
	d = getdir(p, f->slot);


	/*
	 * do it
	 */
	memset(d, 0, sizeof(Dentry));
	settag(p, Tdir, QPNONE);
	freewp(f->wpath);
	freefp(f);

out:
	if(p1)
		putbuf(p1);
	if(p)
		putbuf(p);
	return err;
}

void
f_fstat(Chan *cp, Fcall *in, Fcall *ou)
{
	File *f;
	Iobuf *p;
	Dentry *d;
	int i;

	if(CHAT(cp)) {
		print("c_fstat %d\n", cp->chan);
		print("	fid = %d\n", in->fid);
	}

	p = 0;
	f = filep(cp, in->fid, 0);
	if(!f) {
		ou->ename = errstr9p[Efid];
		goto out;
	}
	p = getbuf(f->fs->dev, f->addr, Bread);
	d = getdir(p, f->slot);
	if(d == 0)
		goto out;

	print("name = %.*s\n", NAMELEN, d->name);
	print("uid = %d; gid = %d; muid = %d\n", d->uid, d->gid, d->muid);
	print("size = %lld; qid = %llux/%lux\n", (Wideoff)d->size,
		(Wideoff)d->qid.path, d->qid.version);
	print("atime = %ld; mtime = %ld\n", d->atime, d->mtime);
	print("dblock =");
	for(i=0; i<NDBLOCK; i++)
		print(" %lld", (Wideoff)d->dblock[i]);
	print("; iblock =");
	for (i = 0; i < NIBLOCK; i++)
		print(" %lld", (Wideoff)d->iblocks[i]);
	print("\n");

out:
	if(p)
		putbuf(p);
	ou->fid = in->fid;
	if(f)
		qunlock(f);
}

void
f_clri(Chan *cp, Fcall *in, Fcall *ou)
{
	File *f;

	if(CHAT(cp)) {
		print("c_clri %d\n", cp->chan);
		print("	fid = %d\n", in->fid);
	}

	f = filep(cp, in->fid, 0);
	if(!f) {
		ou->ename =  errstr9p[Efid];
		goto out;
	}
	ou->ename = errstr9p[doclri(f)];

out:
	ou->fid = in->fid;
	if(f)
		qunlock(f);
}

int
con_clri(int fid)
{
	Fcall in, ou;
	Chan *cp;

	in.type = Tremove;
	in.fid = fid;
	cp = cons.chan;

	rlock(&mainlock);
	ou.type = Tremove+1;
	ou.ename = 0;

	rlock(&cp->reflock);
	f_clri(cp, &in, &ou);
	runlock(&cp->reflock);

	cons.work.count++;
	runlock(&mainlock);
	return ou.ename != 0;
}

int
con_fstat(int fid)
{
	Fcall in, ou;
	Chan *cp;

	in.type = Tstat;
	in.fid = fid;
	cp = cons.chan;

	rlock(&mainlock);
	ou.type = Tstat+1;
	ou.ename = 0;

	rlock(&cp->reflock);
	f_fstat(cp, &in, &ou);
	runlock(&cp->reflock);

	cons.work.count++;
	runlock(&mainlock);
	return ou.ename != 0;
}
