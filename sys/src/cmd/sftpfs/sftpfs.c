/* Copyright © 2008 Fazlul Shahriar*/

#include "fxp.h"

typedef struct FAux FAux;
struct FAux {
	char	*path;
	FHandle *handle;
	Dir	**dir;
	int	ndir;
};

/*
 * http://9fans.net/archive/2003/05/56
 * http://9fans.net/archive/2003/05/57
 */
static uvlong
hash(char *p, char *n, ulong seed)
{
	ulong x;
	uvlong xx;

	x = seed;
	while(*p)
		x = x*1103515245 + 12345 + *p++;
	if(n){
		x = x*1103515245 + 12345 + '/';		/* for neatness only */
		while(*n)
			x = x*1103515245 + 12345 + *n++;
	}
	xx = x;
	xx <<= 32;
	xx |= seed;
	return xx;
}

static Qid
getqid(char *path, char *name, Dir *d)
{
	Qid q;
	
	path = estrdup9p(path);
	/* this isn't quiet right.
		path shouldn't change on every modification */
	q.path = hash(cleanname(path), name, d->mtime);
	free(path);
	q.type = d->mode&DMDIR ? QTDIR : QTFILE;
	q.vers = d->mtime;
	return q;
}

FAux*
newfaux(char *path)
{
	FAux *fa;
	
	fa = emalloc9p(sizeof *fa);
	fa->path = estrdup9p(path);
	fa->handle = nil;
	fa->dir = nil;
	fa->ndir = 0;
	
	return fa;
}

void
freefaux(FAux *fa)
{
	int i;
	
	free(fa->path);
	if(fa->dir != nil){
		for(i = 0; i < fa->ndir; i++)
			freedir(fa->dir[i]);
		free(fa->dir);
	}
	free(fa);
}

static void
fsattach(Req *r)
{
	FAux *fa;
	Dir *d;
	
	if((d = fxpstat("/")) == nil){
		responderror(r);
		return;
	}
	r->fid->qid = getqid("/", "", d);
	freedir(d);
	r->ofcall.qid = r->fid->qid;
	fa = newfaux("/");
	r->fid->aux = fa;
	respond(r, nil);
}

static char*
fsclone(Fid *oldfid, Fid *newfid)
{
	FAux *fa, *ofa;
	
	ofa = oldfid->aux;
	if(ofa->handle != nil)
		return Ehand;
	fa = newfaux(ofa->path);
	newfid->aux = fa;
	
	return nil;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	FAux *fa;
	char *s, *dot, *err;
	Dir *d;
	
	fa = fid->aux;
	if(fa->handle != nil)
		return Ehand;
	if(strcmp(name, "..") == 0){
		s = strrchr(fa->path, '/');
		if(s == nil)
			return Epath;
		if(s != fa->path)	/* not / */
			*s = 0;
	}else
		fa->path = eappend(fa->path, "/", name);
	
	d = fxpstat1(fa->path, &err);
	if(d == nil)
		return err;
	fid->qid = getqid(fa->path, "", d);
	*qid = fid->qid;
	freedir(d);
	
	if(d->mode&DMDIR){
		/*
		 * /some/path/ is executable iff /some/path/. is
		 * stat-able
		 */
		dot = estrstrdup(fa->path, "/.");
		if((d = fxpstat1(dot, &err)) == nil)
			return err;
		freedir(d);
		free(dot);
	}
	return nil;
}

static void
fsdestroyfid(Fid *fid)
{
	FAux *fa;
	
	fa = fid->aux;
	if(fa == nil)
		return;
	fid->aux = nil;
	
	if(fa->handle != nil)
		fxpclose(fa->handle);
	freefaux(fa);
}

static void
fsopen(Req *r)
{
	FAux *fa;
	FHandle *h;
	
	fa = r->fid->aux;
	if(fa->handle != nil){
		respond(r, Eopen);
		return;
	}
	if(r->fid->qid.type & QTDIR)
		h = fxpopendir(fa->path);
	else
		h = fxpopen(fa->path, r->ifcall.mode);
	if(h == nil){
		responderror(r);
		return;
	}
	fa->handle = h;
	respond(r, nil);
}

static void
fscreate(Req *r)
{
	FAux *fa, *dir;
	u32int perm;
	uchar mode;
	char *s;
	Dir *d;
	
	dir = r->fid->aux;
	s = estr3dup(dir->path, "/", r->ifcall.name);
	fa = newfaux(s);
	free(s);
	
	mode = r->ifcall.mode;
	perm = r->ifcall.perm;
	if(perm & DMDIR){
		if(fxpmkdir(fa->path, perm) < 0){
			freefaux(fa);
			responderror(r);
			return;
		}
		fa->handle = fxpopendir(fa->path);
	}else{
		fa->handle = fxpcreate(fa->path, mode, perm);
	}
	if(fa->handle == nil){
		freefaux(fa);
		responderror(r);
		return;
	}
	if((d = fxpstat(fa->path)) == nil){
		freefaux(fa);
		responderror(r);
		return;
	}
	r->fid->qid = getqid(fa->path, "", d);
	freedir(d);
	r->ofcall.qid = r->fid->qid;
	r->fid->aux = fa;
	respond(r, nil);
}

static void
fsremove(Req *r)
{
	FAux *fa;
	int n;
	
	fa = r->fid->aux;
	if(r->fid->qid.type & QTDIR)
		n = fxprmdir(fa->path);
	else
		n = fxpremove(fa->path);
	if(n < 0){
		responderror(r);
		return;
	}
	respond(r, nil);
}

static int
getdirent(int n, Dir *d, void *aux)
{
	FAux *fa;
	Dir *e;

	fa = aux;
	if(n >= fa->ndir)
		return -1;
	e = fa->dir[n];
	d->qid = getqid(fa->path, e->name, e);
	d->mode = e->mode;
	d->atime = e->atime;
	d->mtime = e->mtime;
	d->length = e->length;
	d->name = estrdup9p(e->name);
	d->uid = estrdup9p(e->uid);
	d->gid = estrdup9p(e->gid);
	d->muid = estrdup9p(e->muid);
	
	return 0;
}

static void
fsread(Req *r)
{
	FAux *fa;
	uchar *buf;
	long msz, nbuf;
	int n, nds, i;
	vlong off;
	Dir **d, **ds, **dp;
	
	fa = r->fid->aux;
	if(fa->handle == nil){
		respond(r, Ehand);
		return;
	}
	if(r->fid->qid.type & QTDIR){
		if(fa->dir == nil){
			ds = nil;
			nds = 0;
			while((n = fxpreaddir(fa->handle, &d)) > 0){
				ds = erealloc9p(ds, (nds+n)*sizeof(Dir*));
				dp = ds+nds;
				for(i = 0; i < n; i++)
					dp[i] = d[i];
				free(d);
				nds += n;
			}
			if(n < 0){
				free(ds);
				responderror(r);
				return;
			}
			fa->dir = ds;
			fa->ndir = nds;
		}
		dirread9p(r, getdirent, fa);
	}else{
		nbuf = r->ifcall.count;
		buf = (uchar*)r->ofcall.data;
		off = r->ifcall.offset;
		if((msz = fxpread(fa->handle, buf, nbuf, off)) < 0){
			free(buf);
			responderror(r);
			return;
		}
		r->ofcall.count = msz;
	}
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	FAux *fa;
	long n;
	
	fa = r->fid->aux;
	if(fa->handle == nil){
		respond(r, Ehand);
		return;
	}
	n = fxpwrite(fa->handle, r->ifcall.data,
		r->ifcall.count, r->ifcall.offset);
	if(n < 0){
		responderror(r);
		return;
	}
	r->ofcall.count = n;
	respond(r, nil);
}

static void
fsstat(Req *r)
{
	FAux *fa;
	Dir *d;
	
	fa = r->fid->aux;
	d = fxpstat(fa->path);
	if(d == nil){
		responderror(r);
		return;
	}
	r->d.qid = getqid(fa->path, "", d);
	r->d.mode = d->mode;
	r->d.atime = d->atime;
	r->d.mtime = d->mtime;
	r->d.length = d->length;
	r->d.name = estrdup9p(d->name);
	r->d.uid = estrdup9p(d->uid);
	r->d.gid = estrdup9p(d->gid);
	r->d.muid = estrdup9p(d->muid);
	freedir(d);
	respond(r, nil);
}

static char*
dirname(char *path)
{
	char *d, *s;
	
	d = estrdup9p(path);
	s = strrchr(d, '/');
	if(s != nil)
		*s = 0;
	return d;
}

static void
fswstat(Req *r)
{
	FAux *fa;
	char *newp, *dir;
	
	fa = r->fid->aux;
	if(r->d.name[0] == 0){
		if(fxpsetstat(fa->path, &r->d) < 0){
			responderror(r);
			return;
		}
	}else{
		dir = dirname(fa->path);
		newp = estr3dup(dir, "/", r->d.name);
		free(dir);
		if(strchr(fa->path, '/') == nil || strchr(newp, '/') == nil){
			respond(r, Epath);
			return;
		}
		if(fxprename(fa->path, newp) < 0){
			free(newp);
			responderror(r);
			return;
		}
		free(fa->path);
		fa->path = newp;
	}
	respond(r, nil);
}

static void
fsend(Srv*)
{
	fxpterm();
}

static Srv fs = {
	.attach	= fsattach,
	.walk1	= fswalk1,
	.clone	= fsclone,
	.open	= fsopen,
	.create	= fscreate,
	.read	= fsread,
	.write	= fswrite,
	.remove	= fsremove,
	.wstat	= fswstat,
	.stat	= fsstat,
	.destroyfid	= fsdestroyfid,
	.end	= fsend,
};

void
usage(void)
{
	fprint(2, "usage: sftpfs [-12oDU] [-m mountpoint] [-p serverpath ] [-s srvname] [-u passwd group] [user@]hostname\n");
	threadexitsall("usage");
}

static char *pfile;
static char *gfile;
static char *srvname;
static char *host;
static char *mntpt;
static char *serverpath = "/usr/lib/sftp-server";
static int bigu;
static char sshver = '2';

void
threadmain(int argc, char **argv)
{
	char mpath[50], ppath[50], gpath[50];
	
	ARGBEGIN{
	default:
		usage();
		break;
	case '1':
	case '2':
	case 'o':
		sshver = ARGC();
		break;
	case 'D':
		chatty9p++;
		break;
	case 'm':
		mntpt = EARGF(usage());
		break;
	case 'p':
		serverpath = EARGF(usage());
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	case 'u':
		pfile = EARGF(usage());
		gfile = EARGF(usage());
		break;
	case 'U':
		bigu = 1;
		break;
	}ARGEND;
	
	if(argc < 1)
		usage();
	
	host = argv[0];
	if(fxpinit(host, sshver, serverpath) < 0)
		sysfatal("sftp init: %r");
	
	if(mntpt == nil){
		snprint(mpath, sizeof(mpath), "/n/%s", host);
		mntpt = mpath;
	}
	if(bigu){
		snprint(ppath, sizeof(ppath), "%s/etc/passwd", mntpt);
		pfile = ppath;
		snprint(gpath, sizeof(gpath), "%s/etc/group", mntpt);
		gfile = gpath;
	}
	threadpostmountsrv(&fs, srvname, mntpt, MREPL|MCREATE);
	
	if(gfile != nil && pfile != nil)
		fxpreadmap(pfile, gfile);
	threadexits(nil);
}
