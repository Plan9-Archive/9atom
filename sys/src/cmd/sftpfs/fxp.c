/* Copyright © 2008 Fazlul Shahriar*/

#include "fxp.h"

enum{
	MaxPktLen	= 34000,
	FMsgid	= 0x99999999,
};

enum{
	/* Fxp.type */
	SSH_FXP_INIT = 1,
	SSH_FXP_VERSION = 2,	/* reply */
	SSH_FXP_OPEN = 3,
	SSH_FXP_CLOSE = 4,
	SSH_FXP_READ = 5,
	SSH_FXP_WRITE = 6,
	SSH_FXP_LSTAT = 7,
	SSH_FXP_FSTAT = 8,
	SSH_FXP_SETSTAT = 9,
	SSH_FXP_FSETSTAT = 10,
	SSH_FXP_OPENDIR = 11,
	SSH_FXP_READDIR = 12,
	SSH_FXP_REMOVE = 13,
	SSH_FXP_MKDIR = 14,
	SSH_FXP_RMDIR = 15,
	SSH_FXP_REALPATH = 16,
	SSH_FXP_STAT = 17,
	SSH_FXP_RENAME = 18,
	SSH_FXP_READLINK = 19,
	SSH_FXP_SYMLINK = 20,
	SSH_FXP_STATUS = 101,	/* reply */
	SSH_FXP_HANDLE = 102,	/* reply */
	SSH_FXP_DATA = 103,	/* reply */
	SSH_FXP_NAME = 104,	/* reply */
	SSH_FXP_ATTRS = 105,	/* reply */
	SSH_FXP_EXTENDED = 200,
	SSH_FXP_EXTENDED_REPLY = 201,
	
	/* Fxp.status */
	SSH_FX_OK = 0,
	SSH_FX_EOF = 1,
	SSH_FX_NO_SUCH_FILE = 2,
	SSH_FX_PERMISSION_DENIED = 3,
	SSH_FX_FAILURE = 4,
	SSH_FX_BAD_MESSAGE = 5,
	SSH_FX_NO_CONNECTION = 6,
	SSH_FX_CONNECTION_LOST = 7,
	SSH_FX_OP_UNSUPPORTED = 8,
	
	/* Fxp.pflags */
	SSH_FXF_READ = 0x00000001,
	SSH_FXF_WRITE = 0x00000002,
	SSH_FXF_APPEND = 0x00000004,
	SSH_FXF_CREAT = 0x00000008,
	SSH_FXF_TRUNC = 0x00000010,
	SSH_FXF_EXCL = 0x00000020,
	
	/* FAttrs.flags */
	SSH_FILEXFER_ATTR_SIZE = 0x00000001,
	SSH_FILEXFER_ATTR_UIDGID = 0x00000002,
	SSH_FILEXFER_ATTR_PERMISSIONS = 0x00000004,
	SSH_FILEXFER_ATTR_ACMODTIME = 0x00000008,
	SSH_FILEXFER_ATTR_EXTENDED = 0x80000000,
};

typedef struct FAttrs FAttrs;
typedef struct FName FName;
typedef struct Fxp Fxp;

/* File Attributes */
struct FAttrs{
	u32int	flags;
	u64int	size;
	u32int	uid;
	u32int	gid;
	u32int	perm;
	u32int	atime;
	u32int	mtime;
	/*u32int	nextended; */
};

struct Fxp{
	uchar	type;
	u32int	id;	/* except Init and Version */
	union{
		struct{	/* Init,Version */
			u32int	version;
		};
		struct{	/* Open/Read/Write/Data */
			String	filename;	/* Remove */
			u32int	pflags;
			String	handle;	/* Handle,Close,Readdir,Fstat */
			u64int	offset;
			u32int	len;
			String	data;
			/* Rmdir,Opendir,Realpath,Stat,Lstat,Readlink */
			String	path;
			FAttrs	attrs;		/* Fsetstat,Mkdir,Setstat,Attrs */
		};
		struct{	/* Rename */
			String	oldpath;
			String	newpath;
		};
		struct{ /* Symlink */
			String	linkpath;
			String	targetpath;
		};
		struct{	/* Status */
			u32int	status;
			String	errmsg;
			String	lang;
		};
		struct{	/* Name */
			u32int	count;
			String	*filenames;
			String	*longnames;
			FAttrs	*attrsv;
		};
	};
};


enum{
	STACK	= 8192,
};

static int debug = 0;

typedef struct Conn Conn;
struct Conn{
	char	*host;
	char		sshver;
	char	*serverpath;
	Map		*map;
	
	uchar	buf[MaxPktLen];
	Channel	*rdchan;
	Channel	*wrchan;
	int		sshfd;
	int		fd[2];
};

static Conn conn;

static int
put8(u64int v, uchar *a)
{
	a[0] = v>>56;
	a[1] = v>>48;
	a[2] = v>>40;
	a[3] = v>>32;
	a[4] = v>>24;
	a[5] = v>>16;
	a[6] = v>>8;
	a[7] = v;
	return 8;
}

static int
put4(u32int v, uchar *a)
{
	a[0] = v>>24;
	a[1] = v>>16;
	a[2] = v>>8;
	a[3] = v;
	return 4;
}

static int
put1(uchar v, uchar *a)
{
	a[0] = v;
	return 1;
}

static int
putstring(String *s, uchar *buf)
{
	put4(s->len, buf);
	memmove(buf+4, s->s, s->len);
	
	return s->len+4;
}

static int
putattrs(FAttrs *a, uchar *buf)
{
	uchar *bp;
	
	bp = buf;
	bp += put4(a->flags, bp);
	if(a->flags & SSH_FILEXFER_ATTR_SIZE)
		bp += put8(a->size, bp);
	if(a->flags & SSH_FILEXFER_ATTR_UIDGID){
		bp += put4(a->uid, bp);
		bp += put4(a->gid, bp);
	}
	if(a->flags & SSH_FILEXFER_ATTR_PERMISSIONS)
		bp += put4(a->perm, bp);
	if(a->flags & SSH_FILEXFER_ATTR_ACMODTIME){
		bp += put4(a->atime, bp);
		bp += put4(a->mtime, bp);
	}
	return bp-buf;
}

static int
fxpattrslen(FAttrs *a)
{
	int n;
	
	n = 4;
	n += (a->flags & SSH_FILEXFER_ATTR_SIZE) ? 8 : 0;
	n += (a->flags & SSH_FILEXFER_ATTR_UIDGID) ? 8 : 0;
	n += (a->flags & SSH_FILEXFER_ATTR_PERMISSIONS) ? 4 : 0;
	n += (a->flags & SSH_FILEXFER_ATTR_ACMODTIME) ? 8 : 0;
	return n;
}
	
static int
fxpencode(Fxp *m, uchar *buf, int bufsz)
{
	uchar *bp;
	
	if(bufsz < MaxPktLen)
		sysfatal("buffer size (%d) too small", bufsz);
	
	bp = buf;
	switch(m->type){
	default:
		sysfatal("encoding msg of unknown type %d", m->type);
		break;
	
	case SSH_FXP_INIT:
		bp += put4(1+4, bp);
		bp += put1(m->type, bp);
		bp += put4(m->version, bp);
		break;
	
	case SSH_FXP_RMDIR:
	case SSH_FXP_OPENDIR:
	case SSH_FXP_REALPATH:
	case SSH_FXP_STAT:
	case SSH_FXP_LSTAT:
	case SSH_FXP_READLINK:
		bp += put4(1+4+4+m->path.len, bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->path, bp);
		break;
	
	case SSH_FXP_READDIR:
	case SSH_FXP_CLOSE:
	case SSH_FXP_FSTAT:
		bp += put4(1+4+4+m->handle.len, bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->handle, bp);
		break;
	
	case SSH_FXP_OPEN:
		bp += put4(1+4+4+m->filename.len+4+fxpattrslen(&m->attrs), bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->filename, bp);
		bp += put4(m->pflags, bp);
		bp += putattrs(&m->attrs, bp);
		break;
	
	case SSH_FXP_READ:
		bp += put4(1+4+4+m->handle.len+8+4, bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->handle, bp);
		bp += put8(m->offset, bp);
		bp += put4(m->len, bp);
		break;
	
	case SSH_FXP_WRITE:
		bp += put4(1+4+4+m->handle.len+8+4+m->data.len, bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->handle, bp);
		bp += put8(m->offset, bp);
		bp += putstring(&m->data, bp);
		break;
	
	case SSH_FXP_REMOVE:
		bp += put4(1+4+4+m->filename.len, bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->filename, bp);
		break;
	
	case SSH_FXP_RENAME:
		bp += put4(1+4+4+m->oldpath.len+4+m->newpath.len, bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->oldpath, bp);
		bp += putstring(&m->newpath, bp);
		break;
	
	case SSH_FXP_MKDIR:
	case SSH_FXP_SETSTAT:
		bp += put4(1+4+4+m->path.len+fxpattrslen(&m->attrs), bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->path, bp);
		bp += putattrs(&m->attrs, bp);
		break;
	
	case SSH_FXP_FSETSTAT:
		bp += put4(1+4+4+m->handle.len+fxpattrslen(&m->attrs), bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->handle, bp);
		bp += putattrs(&m->attrs, bp);
		break;
	
	case SSH_FXP_SYMLINK:
		bp += put4(1+4+4+m->linkpath.len+4+m->targetpath.len, bp);
		bp += put1(m->type, bp);
		bp += put4(m->id, bp);
		bp += putstring(&m->linkpath, bp);
		bp += putstring(&m->targetpath, bp);
		break;
	}
	return bp-buf;
}

static int
get8(u64int *x, uchar *a)
{
	u64int v;
	
	v = (uvlong)a[0]<<56;
	v |= (uvlong)a[1]<<48;
	v |= (uvlong)a[2]<<40;
	v |= (uvlong)a[3]<<32;
	v |= a[4]<<24;
	v |= a[5]<<16;
	v |= a[6]<<8;
	v |= a[7]<<0;
	*x = v;
	return 8;
}

static int
get4(u32int *x, uchar *a)
{
	*x = (a[0]<<24)|(a[1]<<16)|(a[2]<<8)|(a[3]<<0);
	return 4;
}

static int
get1(uchar *x, uchar *a)
{
	*x = a[0];
	return 1;
}

static int
getstring(String *s, uchar *buf)
{
	uchar *bp;
	
	bp = buf;
	bp += get4(&s->len, bp);
	s->s = emalloc9p(s->len+1);
	memmove(s->s, bp, s->len);
	bp += s->len;
	s->s[s->len] = 0;
	
	return bp-buf;
}

static int
getattrs(FAttrs *a, uchar *buf)
{
	uchar *bp;
	
	bp = buf;
	bp += get4(&a->flags, bp);
	if(a->flags & SSH_FILEXFER_ATTR_SIZE)
		bp += get8(&a->size, bp);
	if(a->flags & SSH_FILEXFER_ATTR_UIDGID){
		bp += get4(&a->uid, bp);
		bp += get4(&a->gid, bp);
	}
	if(a->flags & SSH_FILEXFER_ATTR_PERMISSIONS)
		bp += get4(&a->perm, bp);
	if(a->flags & SSH_FILEXFER_ATTR_ACMODTIME){
		bp += get4(&a->atime, bp);
		bp += get4(&a->mtime, bp);
	}
	return bp-buf;
}

static Fxp*
fxpdecode(uchar *buf)
{
	Fxp *m;
	int i;
	uchar *bp;
	
	bp = buf;
	bp += 4;		/* ignore length */
	m = emalloc9p(sizeof(*m));
	bp += get1(&m->type, bp);
	
	switch(m->type){
	default:
		sysfatal("bad msg type %d when decoding", m->type);
		break;
	
	case SSH_FXP_VERSION:
		bp += get4(&m->version, bp);
		break;
		
	case SSH_FXP_HANDLE:
		bp += get4(&m->id, bp);
		bp += getstring(&m->handle, bp);
		break;
		
	case SSH_FXP_DATA:
		bp += get4(&m->id, bp);
		bp += getstring(&m->data, bp);
		break;
		
	case SSH_FXP_STATUS:
		bp += get4(&m->id, bp);
		bp += get4(&m->status, bp);
		bp += getstring(&m->errmsg, bp);
		bp += getstring(&m->lang, bp);
		break;
	
	case SSH_FXP_NAME:
		bp += get4(&m->id, bp);
		bp += get4(&m->count, bp);
		m->filenames = emalloc9p(m->count*sizeof(*m->filenames));
		m->longnames = emalloc9p(m->count*sizeof(*m->longnames));
		m->attrsv = emalloc9p(m->count*sizeof(*m->attrsv));
		for(i = 0; i < m->count; i++){
			bp += getstring(&m->filenames[i], bp);
			bp += getstring(&m->longnames[i], bp);
			bp += getattrs(&m->attrsv[i], bp);
		}
		break;
	
	case SSH_FXP_ATTRS:
		bp += get4(&m->id, bp);
		bp += getattrs(&m->attrs, bp);
		break;
	}
	USED(bp);
	return m;
}

static void
sshproc(void*)
{
	int *p;
	
	threadsetname("sshproc");
	
	p = conn.fd;
	close(p[0]);
	dup(p[1], 0);
	dup(p[1], 1);
	close(p[1]);
	
	switch(conn.sshver){
	case '1':
		procexecl(nil, "/bin/ssh1", "ssh1", "-P", "-m", "-I", "-f",
			conn.host, conn.serverpath, nil);
		break;
	case '2':
		procexecl(nil, "/bin/ssh", "ssh", "-m", "-I", "-s", "sftp",
			conn.host, nil);
		break;
	case 'o':
		procexecl(nil, "/bin/openssh/ssh", "ssh", "-x", "-a",
			"-oClearAllForwardings=yes", "-2", conn.host, "-s", "sftp", nil);
		break;
	}
	sysfatal("exec ssh: %r");
}

static void
serverproc(void*)
{
	uchar *buf;
	Fxp *req, *reply;
	int fd;
	uint n;
	
	threadsetname("serverproc");
	
	fd = conn.sshfd;
	for(;;){
		req = recvp(conn.rdchan);
		if(req == nil)
			threadexits(nil);
		reply = nil;
		
		n = fxpencode(req, conn.buf, sizeof conn.buf);
		buf = conn.buf;
		if(debug)
			hexdump("sending: ", buf, n);
		if(write(fd, buf, n) != n)
			goto sendreply;
		if(readn(fd, buf, 4) != 4)
			goto sendreply;
		get4(&n, buf);
		if(n >= sizeof(conn.buf))
			sysfatal("reply bigger than buffer (%d > %d)\n", n, sizeof(conn.buf));
		if(debug)
			fprint(2, "response length: %d\n", n);
		if(readn(fd, buf+4, n) != n)
			goto sendreply;
		if(debug)
			hexdump("response: ", buf, n+4);
		reply = fxpdecode(buf);
		
		if(req->type != SSH_FXP_INIT)
		if(req->id != reply->id)
			sysfatal("reply id doesn't match request id");
sendreply:
		sendp(conn.wrchan, reply);
	}
}

static Fxp*
fxpgetreply(Fxp *msg)
{
	Fxp *r;
	
	sendp(conn.rdchan, msg);
	r = recvp(conn.wrchan);
	if(r == nil)	/* probably ssh exited */
		threadexitsall(nil);
	return r;
}

static void
stringinit(String *s, char *t)
{
	s->s = (uchar*)estrdup9p(t);
	s->len = strlen(t);
}

static void
stringcpy(String *dst, String *src)
{
	dst->len = src->len;
	dst->s = emalloc9p(src->len+1);
	memmove(dst->s, src->s, src->len);
	dst->s[dst->len] = 0;
}

static String*
stringdup(String *s)
{
	String *q;
	
	q = emalloc9p(sizeof *q);
	q->len = s->len;
	q->s = emalloc9p(s->len+1);
	memmove(q->s, s->s, s->len);
	q->s[q->len] = 0;
	return q;
}

static void
freestring(String *s)
{
	if(s){
		free(s->s);
		free(s);
	}
}

struct{
	u32int	status;
	char	*str;
	int	iserror;
} errtab[] = {
	SSH_FX_OK, "", 0,
	SSH_FX_EOF, "", 0,
	SSH_FX_NO_SUCH_FILE, Enofile, 1,
	SSH_FX_PERMISSION_DENIED, Eperm, 1,
	SSH_FX_FAILURE, Efail, 1,
	SSH_FX_BAD_MESSAGE, Emsg, 1, 
	SSH_FX_NO_CONNECTION, Enocn, 1,
	SSH_FX_CONNECTION_LOST, Elostcn, 1,
	SSH_FX_OP_UNSUPPORTED, Eunsup, 1,
};

static char*
errlookup(u32int status, int *err)
{
	int i;
	
	for(i = 0; i < nelem(errtab); i++)
		if(errtab[i].status == status){
			if(err)
				*err = errtab[i].iserror;
			return errtab[i].str;
		}
	if(err)
		*err = 1;
	return Ebotch;
}
	
static int
fxpwerrstr(u32int status)
{
	int i;
	
	for(i = 0; i < nelem(errtab); i++)
		if(errtab[i].status == status){
			werrstr(errtab[i].str);
			return errtab[i].iserror;
		}
	werrstr("%s", Ebotch);
	return 1;
}

static void
freefxp(Fxp *m)
{
	int i;
	
	if(m == nil)
		return;
	switch(m->type){
	default:
		fprint(2, "how to free msg of type %d?\n", m->type);
		break;
	
	case SSH_FXP_INIT:
	case SSH_FXP_VERSION:
	case SSH_FXP_ATTRS:
		break;
	
	case SSH_FXP_OPEN:
	case SSH_FXP_REMOVE:
		free(m->filename.s);
		break;
	
	case SSH_FXP_CLOSE:
	case SSH_FXP_READ:
	case SSH_FXP_READDIR:
	case SSH_FXP_FSTAT:
	case SSH_FXP_FSETSTAT:
	case SSH_FXP_HANDLE:
		free(m->handle.s);
		break;
	
	case SSH_FXP_WRITE:
		free(m->handle.s);
		free(m->data.s);
		break;
		
	case SSH_FXP_RENAME:
		free(m->oldpath.s);
		free(m->newpath.s);
		break;
	
	case SSH_FXP_MKDIR:
	case SSH_FXP_RMDIR:
	case SSH_FXP_OPENDIR:
	case SSH_FXP_STAT:
	case SSH_FXP_LSTAT:
	case SSH_FXP_SETSTAT:
	case SSH_FXP_READLINK:
	case SSH_FXP_REALPATH:
		free(m->path.s);
		break;
	
	case SSH_FXP_SYMLINK:
		free(m->linkpath.s);
		free(m->targetpath.s);
		break;
	
	case SSH_FXP_STATUS:
		free(m->errmsg.s);
		free(m->lang.s);
		break;
	
	case SSH_FXP_DATA:
		free(m->data.s);
		break;
	
	case SSH_FXP_NAME:
		for(i = 0; i < m->count; i++){
			free(m->filenames[i].s);
			free(m->longnames[i].s);
		}
		free(m->filenames);
		free(m->longnames);
		free(m->attrsv);
		break;
	}
	free(m);
}

static FHandle*
replyhandle(Fxp *m)
{
	Fxp *r;
	FHandle *h;
	
	r = fxpgetreply(m);
	freefxp(m);
	switch(r->type){
	default:
		werrstr("%s", Ebotch);
		freefxp(r);
		return nil;
	case SSH_FXP_STATUS:
		fxpwerrstr(r->status);
		freefxp(r);
		return nil;
	case SSH_FXP_HANDLE:
		h = stringdup(&r->handle);
		freefxp(r);
		return h;
	}
}

static int
replystatus(Fxp *r)
{
	int err;
	
	switch(r->type){
	default:
		werrstr("%s", Ebotch);
		freefxp(r);
		return 1;
	case SSH_FXP_STATUS:
		err = fxpwerrstr(r->status);
		freefxp(r);
		return err;
	}
}
	
static Fxp*
newfxp(uchar type)
{
	Fxp *m;
	
	m = emalloc9p(sizeof *m);
	m->type = type;
	m->id = FMsgid;
	return m;
}

int
fxpremove(char *file)
{
	Fxp *m, *r;
	
	m = newfxp(SSH_FXP_REMOVE);
	stringinit(&m->filename, file);
	r = fxpgetreply(m);
	freefxp(m);
	
	return replystatus(r) ? -1 : 0;
}

int
fxprmdir(char *path)
{
	Fxp *m, *r;
	
	m = newfxp(SSH_FXP_RMDIR);
	stringinit(&m->path, path);
	r = fxpgetreply(m);
	freefxp(m);
	
	return replystatus(r) ? -1 : 0;
}

int
fxpmkdir(char *path, ulong perm)
{
	Fxp *m, *r;
	
	m = newfxp(SSH_FXP_MKDIR);
	stringinit(&m->path, path);
	m->attrs.flags = SSH_FILEXFER_ATTR_PERMISSIONS;
	m->attrs.perm = perm&0777;
	r = fxpgetreply(m);
	freefxp(m);
	
	return replystatus(r) ? -1 : 0;
}

long
fxpwrite(FHandle *h, void *buf, long nbuf, vlong off)
{
	Fxp *m, *r;
	
	m = newfxp(SSH_FXP_WRITE);
	stringcpy(&m->handle, h);
	m->offset = off;
	m->data.len = nbuf;
	m->data.s = emalloc9p(nbuf+1);
	memmove(m->data.s, buf, nbuf);
	m->data.s[m->data.len] = 0;
	r = fxpgetreply(m);
	freefxp(m);
	
	return replystatus(r) ? -1 : nbuf;
}

int
fxpread(FHandle *h, void *buf, long nbuf, vlong off)
{
	Fxp *m, *r;
	int n;
	
	m = newfxp(SSH_FXP_READ);
	stringcpy(&m->handle, h);
	m->offset = off;
	m->len = nbuf;
	r = fxpgetreply(m);
	freefxp(m);
	
	if(r->type == SSH_FXP_DATA){
		n = r->data.len;
		memmove(buf, r->data.s, n);
		freefxp(r);
		return n;
	}
	return replystatus(r) ? -1 : 0;
}

FHandle*
fxpcreate(char *file, int omode, ulong perm)
{
	int mode;
	Fxp *m;
	
	mode = SSH_FXF_CREAT | SSH_FXF_TRUNC;
	if(omode&OREAD)
		mode |= SSH_FXF_READ;
	if(omode&OWRITE)
		mode |= SSH_FXF_WRITE;
	if(omode&ORDWR)
		mode |= SSH_FXF_READ | SSH_FXF_WRITE;
	
	m = newfxp(SSH_FXP_OPEN);
	stringinit(&m->filename, file);
	m->pflags = mode;
	m->attrs.flags = SSH_FILEXFER_ATTR_PERMISSIONS;
	m->attrs.perm = perm&0777;
	
	return replyhandle(m);
}

FHandle*
fxpopen(char *file, int omode)
{
	Fxp *m;
	int mode;
	
	/*
	 * We can't do any permission checks for OEXEC or
	 * ORCLOSE, as we don't know the UID of the user or what
	 * group(s) the user belongs to.
	 */
	mode = 0;
	if(omode&OREAD)
		mode |= SSH_FXF_READ;
	if(omode&OWRITE)
		mode |= SSH_FXF_WRITE;
	if(omode&ORDWR)
		mode |= SSH_FXF_READ | SSH_FXF_WRITE;
	/*
	 * If file doesn't exists, there should be no Topen,
	 * as Twalk will fail. So, the CREAT flag is here only to
	 * statisfy sftp protocol.
	 */
	if(omode&OTRUNC)
		mode |= SSH_FXF_TRUNC | SSH_FXF_CREAT;
	
	m = newfxp(SSH_FXP_OPEN);
	stringinit(&m->filename, file);
	m->pflags = mode;
	m->attrs.flags = 0;
	
	return replyhandle(m);
}

void
freedir(Dir *d)
{
	if(d){
		free(d->uid);
		free(d->gid);
		free(d->muid);
		free(d->name);
		free(d);
	}
}

enum {
	UIDSz = 20,
	ATDIR = 0040000,
};

static void
attrs2dir(Dir *d, FAttrs *a, char *name)
{
	/* caller should fill in Qid properly later */
	d->qid = (Qid){0, 0, QTFILE};
	d->mode = 0;
	d->atime = 0;
	d->mtime = 0;
	d->length = 0;
	d->muid = estrdup9p("unknown");
	d->name = estrdup9p(name);
	if(conn.map == nil){
		d->uid = estrdup9p("unknown");
		d->gid = estrdup9p("unknown");
	}else{
		d->uid = uidtostr(conn.map, a->uid);
		d->gid = gidtostr(conn.map, a->gid);
	}

	if(a->flags & SSH_FILEXFER_ATTR_SIZE)
		d->length = a->size;
	if(a->flags & SSH_FILEXFER_ATTR_PERMISSIONS){
		d->mode = a->perm&0777;
		if(a->perm & ATDIR){
			d->mode |= DMDIR;
			d->qid.type = QTDIR;
		}
	}
	if(a->flags & SSH_FILEXFER_ATTR_ACMODTIME){
		d->atime = a->atime;
		d->mtime = a->mtime;
	}
}

static char*
basename(char *p)
{
	char *s;
	
	s = strrchr(p, '/');
	if(s == nil)
		s = p;
	else
		s++;
	return estrdup9p(s);
}

Dir*
fxpstat1(char *name, char **err)
{
	Fxp *m, *r;
	Dir *d;
	
	m = newfxp(SSH_FXP_STAT);
	stringinit(&m->path, name);
	r = fxpgetreply(m);
	freefxp(m);
	
	/* walk1 kludge. We can't use responderror there */
	if(err)
		*err = errlookup(r->status, nil);
	
	if(r->type == SSH_FXP_ATTRS){
		d = emalloc9p(sizeof *d);
		name = basename(name);
		attrs2dir(d, &r->attrs, name);
		free(name);
		freefxp(r);
		return d;
	}
	return replystatus(r) ? nil : nil;
}

Dir*
fxpstat(char *name)
{
	return fxpstat1(name, nil);
}

static void
dir2attrs(FAttrs *a, Dir *d, Dir *od)
{
	a->flags = 0;
	if(d->length != ~0){
		a->flags |= SSH_FILEXFER_ATTR_SIZE;
		a->size = d->length;
	}
	if(d->mode != ~0){
		a->flags |= SSH_FILEXFER_ATTR_PERMISSIONS;
		a->perm = d->mode&0777;
	}
	d->atime = od->atime;
	d->mtime = od->mtime;
	if(d->atime != ~0)
		a->atime = d->atime;
	if(d->mtime != ~0)
		a->mtime = d->mtime;
	if(d->atime != ~0 || d->mtime != ~0)
		a->flags |= SSH_FILEXFER_ATTR_ACMODTIME;
}

int
fxpsetstat(char *name, Dir *d)
{
	Fxp *m, *r;
	Dir *od;

	if((od = fxpstat(name)) == nil)
		return -1;
	m = newfxp(SSH_FXP_SETSTAT);
	stringinit(&m->path, name);
	dir2attrs(&m->attrs, d, od);
	r = fxpgetreply(m);
	freefxp(m);
	
	return replystatus(r) ? -1 : 0;
}

int
fxprename(char *old, char *new)
{
	Fxp *m, *r;
	
	m = newfxp(SSH_FXP_RENAME);
	stringinit(&m->oldpath, old);
	stringinit(&m->newpath, new);
	r = fxpgetreply(m);
	freefxp(m);
	
	return replystatus(r) ? -1 : 0;
}

long
fxpreaddir(FHandle *h, Dir ***buf)
{
	Fxp *m, *r;
	int i, j;
	char *name;
	Dir **ds;
	FAttrs *a;
	
	m = newfxp(SSH_FXP_READDIR);
	stringcpy(&m->handle, h);
	r = fxpgetreply(m);
	freefxp(m);

	if(r->type == SSH_FXP_NAME){
		ds = emalloc9p(r->count*sizeof(Dir*));
		j = 0;
		for(i = 0; i < r->count; i++){
			name = (char*)r->filenames[i].s;
			a = &r->attrsv[i];
			if(strcmp(name, ".") != 0 && strcmp(name, "..") != 0){
				ds[j] = emalloc9p(sizeof(Dir));
				attrs2dir(ds[j], a, name);
				j++;
			}
		}
		*buf = ds;
		freefxp(r);
		return j;
	}
	return replystatus(r) ? -1 : 0;
}

int
fxpclose(FHandle *h)
{
	Fxp *m, *r;
	
	m = newfxp(SSH_FXP_CLOSE);
	stringcpy(&m->handle, h);
	freestring(h);
	r = fxpgetreply(m);
	freefxp(m);
	
	return replystatus(r) ? -1 : 0;
}


FHandle*
fxpopendir(char *path)
{
	Fxp *m;
	
	m = newfxp(SSH_FXP_OPENDIR);
	stringinit(&m->path, path);
	
	return replyhandle(m);
}

static int
fxpversion(int ver)
{
	Fxp *m, *r;
	int n;
	
	m = newfxp(SSH_FXP_INIT);
	m->version = ver;
	r = fxpgetreply(m);
	freefxp(m);
	
	if(r->type != SSH_FXP_VERSION){
		freefxp(r);
		return -1;
	}
	n = r->version;
	freefxp(r);
	return n;
}

int
fxpinit(char *host, char ver, char *path)
{
	int *p, n;
	
	p = conn.fd;
	if(pipe(p) < 0)
		sysfatal("pipe: %r");
	conn.host = estrdup9p(host);
	conn.sshver = ver;
	conn.serverpath = estrdup9p(path);
	procrfork(sshproc, nil, STACK, RFFDG);
	close(p[1]);
	conn.sshfd = p[0];
	
	conn.rdchan = chancreate(sizeof(Fxp*), 0);
	conn.wrchan = chancreate(sizeof(Fxp*), 0);
	proccreate(serverproc, nil, STACK);
	
	if((n = fxpversion(3)) != 3){
		werrstr("got protocol version %d; want 3", n);
		return -1;
	}
	return 0;
}

void
fxpreadmap(char *pfile, char *gfile)
{
	conn.map = readmap(pfile, gfile);
}

void
fxpterm(void)
{
	close(conn.sshfd);
	sendp(conn.rdchan, nil); 	/* terminate serverproc */
	chanfree(conn.rdchan);
	chanfree(conn.wrchan);
	free(conn.host);
	free(conn.serverpath);
	if(conn.map)
		closemap(conn.map);
}
