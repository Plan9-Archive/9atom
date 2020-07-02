#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <libsec.h>
#include <9p.h>
#include <auth.h>
#include <bio.h>
#include "exif.h"

typedef struct Aux Aux;
typedef struct Vfile Vfile;
typedef struct Ptype Ptype;

static struct Ptype {
	char *ext;
	int (*open)(char *file, int mode);
	long (*pread)(int fd, void *buf, long len, vlong off);
	int (*close)(int fd);
};

struct Vfile {
	char *name;	/* firtual file name */
	int inherit;	/* inherit extension from parent directory */
};

struct Aux {
	char *path;	/* full path fo file */
	int fd;		/* file descriptor of open file */
	Dir *dir;	/* cached directory contents */
	int ndir;	/* number of entries in above */
	int pt;		/* index into photo types or -1 if regular file/dir */
	int vf;		/* index into virtual file table or -1 */
};

extern int chatty9p;

static char *User;
static char *Magic = "exif is over-complex";
static int Debug = 0;
Ptype Ptypes[] = {
	{ ".jpg",	jpgopen,	jpgpread,	jpgclose },
	{ ".jpeg",	jpgopen,	jpgpread,	jpgclose },
	{ ".jfif",	jpgopen,	jpgpread,	jpgclose },
//	{ ".tiff",	tiffopen,	tiffpread,	tiffclose },
//	{ ".tif",	tiffopen,	tiffpread,	tiffclose },
//	{ ".gif",	gifopen,	gifpread,	gifclose },
//	{ ".png",	pngopen,	pngpread,	pngclose },
//	{ ".dca",	dcaopen,	dcapread,	dcaclose },
//	{ ".raw",	rawopen,	rawpread,	rawclose },
};

Vfile Vfiles[] = {
	{ "fullsize",		1 },
	{ "thumbnail",		1 },
	{ "metadata",		0 },
};

static void
dircpy(Dir *d, Dir *s)
{
	int i;
	char *p;
	DigestState *ds;
	uchar digest[SHA1dlen];

	d->name = estrdup9p(s->name);
	d->type = s->type;
	d->dev = s->dev;
	d->uid = estrdup9p(s->uid);
	d->gid = estrdup9p(s->gid);
	d->muid = estrdup9p(s->muid);
	d->atime = s->atime;
	d->mtime = s->mtime;
	d->mode = s->mode;
	d->length = s->length;
	d->qid = s->qid;

	if((p = strrchr(d->name, '.')) == nil)
		return;
	for(i = 0; i < nelem(Ptypes); i++)
		if(cistrcmp(Ptypes[i].ext, p) == 0)
			break;
	if(i >= nelem(Ptypes))
		return;		// not a photo

	ds = sha1((uchar *)Magic, strlen(Magic), nil, nil);
	sha1((uchar *)d->name, strlen(d->name), digest, ds);
	free(d->muid);
	d->muid = smprint("%.*[", 8, digest);
	d->mode = DMDIR|0755;
	d->length = 0;
	d->qid.type |= QTDIR;
}

static Qid 
qidgen(char *path, char *name)
{
	static Qid q;
	DigestState *ds;
	uchar digest[SHA1dlen];

	ds = sha1((uchar *)path, strlen(path), nil, nil);
	sha1((uchar *)name, strlen(name), digest, ds);
	q.vers = 1;
	q.type = 'P';
	q.path = *((uvlong *)digest);
	return q;
}

static char *
newpath(char *path, char *name)
{
	char *p;

	p = smprint("%s/%s", path, name);
	if(p == nil)
		sysfatal("smprint: %r");
	return cleanname(p);
}

static int
dirfake(Dir *d, char *path, int slot)
{
	Dir *s;
	char *ext;

 	if(slot < 0 || slot >= nelem(Vfiles))
		return -1;

	ext = "";
	if(Vfiles[slot].inherit && (ext = strrchr(path, '.')) == nil)
		return -1;
	if((s = dirstat(path)) == nil)
		return -1;
	d->name = smprint("%s%s", Vfiles[slot].name, ext);
	d->uid = estrdup9p(s->uid);
	d->gid = estrdup9p(s->gid);
	d->muid = estrdup9p(s->muid);
	d->atime = s->atime;
	d->mtime = s->mtime;
	d->length = s->length;
	d->mode = s->mode;
	d->qid = qidgen(path, d->name);
	free(s);
	return 0;
}

static int
dirgen(int slot, Dir *d, void *aux)
{
	Aux *a = aux;

	if(a->pt != -1)
		return dirfake(d, a->path, slot);

	if (a->ndir <= 0)
		if ((a->ndir = dirreadall(a->fd, &a->dir)) < 0){
			a->dir = nil;
			return -1;
		}
	if (slot >= a->ndir)
		return -1;
	dircpy(d, a->dir+slot);
	return 0;
}

static void
fsattach(Req *r)
{
	Dir *d;
	Aux *a;
	int fd;

	if(strcmp(r->ifcall.uname, User) != 0){
		fd = open("#c/user", OWRITE);
		if(fd < 0 || write(fd, "none", strlen("none")) < 0)
			sysfatal("can't become none");
		close(fd);
		if(newns("none", nil) < 0)
			sysfatal("can't build namespace");
	}

	if ((d = dirstat(r->ifcall.aname)) == nil){
		responderror(r);
		return;
	}
	a = emalloc9p(sizeof(Aux));
	a->path = estrdup9p(r->ifcall.aname);
	a->fd = -1;
	a->pt = -1;
	a->vf = -1;
	a->ndir = 0;
	a->dir = nil;
	r->fid->qid = d->qid;
	free(d);

	r->ofcall.qid = r->fid->qid;
	r->fid->aux = a;
	respond(r, nil);
}


static char*
fsclone(Fid *ofid, Fid *fid)
{
	Aux *a, *oa = ofid->aux;

	a = emalloc9p(sizeof(Aux));
	fid->aux = a;
	a->fd = -1;
	a->ndir = 0;
	a->dir = nil;
	a->vf = oa->vf;
	a->pt = oa->pt;
	a->path = estrdup9p(oa->path);
	return nil;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qp)
{
	Dir *d;
	int i, n, fd;
	char *ext, *npath;
	static char e[ERRMAX], nbuf[128];
	Aux *a = fid->aux;

	if(strcmp(name, "..") == 0){
		ext = strrchr(a->path, '/');
		if(ext == nil){
			snprint(e, sizeof e, ".. from root");
			return e;
		}
		*ext = 0;
		ext = strrchr(a->path, '/');
		if(ext == nil)
			ext = a->path;
		else
			*ext++ = 0;
		snprint(nbuf, sizeof nbuf, "%s", ext);
		name = nbuf;
		a->pt = -1;
	}

	*e = 0;
	ext = strrchr(name, '.');
	npath = newpath(a->path, name);
	/*
	 * one of the virtual files:  fullsize.jpg, thumbnail.jpg, metadata
	 */
	if(a->pt != -1){
		if(ext == nil)
			ext = strchr(name, 0);
		for(i = 0; i < nelem(Vfiles); i++)
			if(strncmp(Vfiles[i].name, name, ext-name) == 0)
				break;
		if(i >= nelem(Vfiles))
			return "unknown file";
		*qp = qidgen(a->path, name);
		fid->qid = *qp;
		free(a->path);
		a->path = npath;
		a->vf = i;
		return nil;
	}

	if((fd = open(a->path, OREAD)) == -1){
		rerrstr(e, sizeof e);
		free(npath);
		return e;
	}

	if((n = dirreadall(fd, &d)) < 1){
		rerrstr(e, sizeof e);
		close(fd);
		free(npath);
		return e;
	}

	/*
	 * a real photo file which is going to become a directory
	 */
	if(ext && (d->qid.type & QTDIR) == 0)
		for(i = 0; i < nelem(Ptypes); i++)
			if(cistrcmp(ext, Ptypes[i].ext) == 0){
				*qp = d->qid;
				qp->type |= QTDIR;
				fid->qid = *qp;
				a->pt = i;
				free(a->path);
				a->path = npath;
				close(fd);
				free(d);
				return nil;
			}

	/*
	 * a vanilla file or directory
	 */
	for(i = 0; i < n; i++)
		if(strcmp(name, d[i].name) == 0){
			*qp = d[i].qid;
			fid->qid = *qp;
			close(fd);
			free(d);
			free(a->path);
			a->path = npath;
			return nil;
		}
	close(fd);
	free(d);
	free(npath);
	return e;
}

static void
fsstat(Req *r)
{
	Dir *d;
	char *s, *p;
	Aux *a = r->fid->aux;

	if(a->vf != -1){
		s = estrdup9p(a->path);
		if((p = strrchr(s, '/')) != nil)
			*p = 0;
		if(dirfake(&(r->d), s, a->vf) == -1){
			free(s);
			responderror(r);
			return;
		}
		free(s);
		respond(r, nil);
		return;
	}

	if ((d = dirstat(a->path)) == nil){
		responderror(r);
		return;
	}
		dircpy(&(r->d), d);
	free(d);
	respond(r, nil);
}

static void
fsopen(Req *r)
{
	Aux *a = r->fid->aux;
	int (*func)(char *, int);

	if(a->pt != -1 && a->vf == -1){	// virtual dir
		respond(r, nil);
		return;
	}

	if(a->vf != -1 && a->pt != -1)
		func = Ptypes[a->pt].open;
	else
		func = open;
	if ((a->fd = (*func)(a->path, r->ifcall.mode)) < 0){
		responderror(r);
		return;
	}
	respond(r, nil);
}

static void
fsread(Req *r)
{
	long n;
	Aux *a = r->fid->aux;
	long (*func)(int, void *, long, vlong);

	if (r->fid->qid.type & QTDIR){
		dirread9p(r, dirgen, a);
		respond(r, nil);
		return;
	}

	if(a->vf != -1 && a->pt != -1)
		func = Ptypes[a->pt].pread;
	else
		func = pread;
	if ((n = (*func)(a->fd, r->ofcall.data, r->ifcall.count, r->ifcall.offset)) < 0){
		responderror(r);
		return;
	}
	r->ofcall.count = n;
	respond(r, nil);
}


static void
fsdestroyfid(Fid *f)
{
	Aux *a = f->aux;
	int (*func)(int);

	if(a){
		if(a->vf != -1 && a->pt != -1)
			func = Ptypes[a->pt].close;
		else
			func = close;
		if(a->fd != -1)
			(*func)(a->fd);
		if(a->dir && a->ndir > 0)
			free(a->dir);
		free(a->path);
		free(a);
	}
}

Srv fs = {
	.destroyfid =	fsdestroyfid,
	.attach=	fsattach,
	.open=		fsopen,
	.read=		fsread,
	.stat=		fsstat,
	.clone= 	fsclone,
	.walk1= 	fswalk1,
};


void
usage(void)
{
	fprint(2, "usage: exifsrv [-dD] [-m mode] [-s srv]\n");
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	Dir d;
	int mode;
	char *srv, path[128];

	mode = -1;
	srv = "exif";
	User = getuser();
	fmtinstall('[', encodefmt);

	ARGBEGIN{
	case 'm':
		mode = strtoul(EARGF(usage()), nil, 8);
		break;
	case 'd':
		Debug++;
		break;
	case 'D':
		chatty9p++;
		break;
	case 's':
		srv = EARGF(usage());
		break;
	default:
		usage();
		break;
	}ARGEND
	if(argc != 0)
		usage();
	if(!Debug && !chatty9p){
		close(0);
		close(1);
		close(2);
	}

	threadpostmountsrv(&fs, srv, nil, 0);

	if(mode != -1){
		snprint(path, sizeof(path), "/srv/%s", srv);
		nulldir(&d);
		d.mode = mode;
		dirwstat(path, &d);
	}

	exits(nil);
}
