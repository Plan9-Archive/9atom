#include "all.h"

enum{
	Dst,
	Src,
	Osrc,
};

int	lineno;
int 	errors;
int	errorexit = 1;
int 	donothing;
int 	verbose;
char 	*root[3];
char 	nbuf[3][10240];
char 	*mkname(char*, int, char*, char*);
int 	copyfile(char*, char*, char*, Dir*, int, int*);

void
chat(char *f, ...)
{
	Fmt fmt;
	char buf[256];
	va_list arg;

	if(!verbose)
		return;

	fmtfdinit(&fmt, 1, buf, sizeof buf);
	va_start(arg, f);
	fmtvprint(&fmt, f, arg);
	va_end(arg);
	fmtfdflush(&fmt);
}

void
fatal(char *f, ...)
{
	char *s;
	va_list arg;

	va_start(arg, f);
	s = vsmprint(f, arg);
	va_end(arg);
	fprint(2, "%d: error: %s\n", lineno, s);
	free(s);
	exits("errors");
}

void
error(char *f, ...)
{
	char *s;
	va_list arg;

	va_start(arg, f);
	s = vsmprint(f, arg);
	va_end(arg);
	fprint(2, "%d: error: %s\n", lineno, s);
	free(s);
	errors = 1;
	if(errorexit)
		exits("errors");
}

void
warn(char *f, ...)
{
	char *s;
	va_list arg;

	va_start(arg, f);
	s = vsmprint(f, arg);
	va_end(arg);
	fprint(2, "%d: warn: %s\n", lineno, s);
	free(s);
}

int
reason(char *s)
{
	char e[ERRMAX];

	rerrstr(e, sizeof e);
	if(strstr(s, e) == 0)
		return 0;
	return -1;
}

char*
mkname(char *buf, int nbuf, char *a, char *b)
{
	if(strlen(a)+strlen(b)+2 > nbuf)
		fatal("name too long");

	strcpy(buf, a);
	if(a[strlen(a)-1] != '/')
		strcat(buf, "/");
	strcat(buf, b);
	return buf;
}

int
isdir(char *s)
{
	ulong m;
	Dir *d;

	if((d = dirstat(s)) == nil)
		return 0;
	m = d->mode;
	free(d);
	return (m&DMDIR) != 0;
}

void
usage(void)
{
	fprint(2, "usage: replica/cphist [-nve] [-l lineno] dstroot newsrcroot oldsrcroot < log\n");
	exits("usage");
}

void
notexists(char *path)
{
	if(access(path, AEXIST) == -1)
		fatal("%r");
}

void
updatestat(char *name, int fd, Dir *t)
{
	if(dirfwstat(fd, t) < 0)
		error("dirfwstat %q: %r", name);
}

long
preadn(int fd, void *av, long n, vlong p)
{
	char *a;
	long m, t;

	a = av;
	t = 0;
	while(t < n){
		m = pread(fd, a+t, n-t, p);
		if(m <= 0){
			if(t == 0)
				return m;
			break;
		}
		t += m;
		p += m;
	}
	return t;
}

char buf[8192];
char obuf[8192];

int
copy(int fdf, int fdt, char *from, char *to, vlong p)
{
	long n;

	while(n = pread(fdf, buf, sizeof buf, p)){
		if(n < 0)
			error("reading %s: %r", from);
		if(pwrite(fdt, buf, n, p) != n)
			error("writing %s: %r\n", to);
		p += n;
	}
	return 0;
}

int
change0(int fd[3], char *name[3], Dir *d[3])
{
	vlong o, sz;
	long n;

	if(d[Osrc]->qid.path != d[Src]->qid.path){
		close(fd[Dst]);
		if(remove(name[Dst]) == -1)
			fatal("can't remove old file %q: %r", name[Dst]);
		if((fd[Dst] = create(name[Dst], ORDWR, 0666)) == -1)
			fatal("create %q: %r", name[Dst]);
		return copy(fd[Src], fd[Dst], name[Src], name[Dst], 0);
	}
	if((d[Osrc]->qid.type&QTAPPEND) && (d[Src]->qid.type&QTAPPEND))
		return copy(fd[Src], fd[Dst], name[Src], name[Dst], d[Osrc]->length);
	/* hard case.  we need to copy only changed blocks */
	sz = d[Src]->length;
	if(sz > d[Osrc]->length)
		sz = d[Osrc]->length;
	for(o = 0; o < sz; o += n){
		n = 8192;
		if(n > sz-o)
			n = sz-o;
		if(preadn(fd[Src], buf, n, o) != n)
			fatal("pread %q: short read", name[Src]);
		if(preadn(fd[Osrc], obuf, n, o) != n)
			fatal("pread %q: short read", name[Src]);
		if(memcmp(buf, obuf, n) != 0){
			if(pwrite(fd[Dst], buf, n, o) != n)
				fatal("pwrite %q: %r", name[Dst]);
		}
	}
	if(sz < d[Src]->length)
		return copy(fd[Src], fd[Dst], name[Src], name[Dst], sz);
	return 0;
}

static char muid[28];

void
assignmuid(int fd, char *name, Dir *rd)
{
	Dir *d;

	if((d = dirfstat(fd)) == 0)
		error("dirfstat %q: %r", name);
	strecpy(muid, muid+sizeof muid-1, d->muid);
	rd->muid = muid;
	free(d);
}

int
change(int fd[3], char *name[3], Dir *rd)
{
	Dir *d[3];
	int n, i;

	for(i = 0; i < 3; i++)
		if((d[i] = dirfstat(fd[i])) == 0)
			error("dirfstat %q: %r", name[i]);
	n = change0(fd, name, d);
	strecpy(muid, muid+sizeof muid-1, d[Src]->muid);
	rd->muid = muid;
	for(i = 0; i < 3; i++)
		free(d[i]);
	return n;
}

char *
basename(char *name)
{
	char *r;

	r = strrchr(name, '/');
	if(r)
		return r+1;
	return name;
}

int mtab[] = {
[Dst]	ORDWR,
[Src]	OREAD,
[Osrc]	OREAD,
};

void
doclose(int fd[3])
{
	for(int i = 0; i < 3; i++){
		close(fd[i]);
		fd[i] = -1;
	}
}

void
fddump(char *file[3], int fd[3])
{
	for(int i = 0; i < 3; i++)
		print(">> %s: %d %d %.3s\n", file[i], fd[i], mtab[i], "dstsrcosr"+3*i);
}

int
doopen(char *file[3], int fd[3])
{
	int i;

	for(i = 0; i < 3; i++){
		if((fd[i] = open(file[i], mtab[i])) == -1){
			if(reason("file is locked") == -1)
				error("open: %r", file[i]);
			else{
				// gruesome hack to get around exclusive mode files.
				if(i == 2)
					fd[2] = dup(fd[1], -1);
				if(fd[i] == -1)
					warn("open %d: %r", mtab[i], file[i]);
				else
					continue;
			}
			fddump(file, fd);
			doclose(fd);
			error("open: %r", file[i]);
			return -1;
		}
	}
	return 0;
}

void
main(int argc, char **argv)
{ 
	char *f[10], *name, *s, *t, verb, *file[3];
	int fd[3], i, nf, line0;
	Dir rd;
	Biobuf bin;

	quotefmtinstall();
	line0 = 0;
	ARGBEGIN{
	case 'n':
		donothing = 1;
		verbose = 1;
		break;
	case 'e':
		errorexit ^= 1;
		break;
	case 'l':
		line0 = atoi(EARGF(usage()));
		break;
	case 'v':
		verbose++;
		break;
	default:
		usage();
	}ARGEND

	if(argc != 3)
		usage();
	for(i = 0; i < 3; i++)
		if(!isdir(root[i] = argv[i]))
			fatal("bad root directory %q", argv[i]);

	Binit(&bin, 0, OREAD);
	for(; s=Brdstr(&bin, '\n', 1); free(s)){
		t = estrdup(s);
		nf = tokenize(s, f, nelem(f));
		if(nf != 10 || strlen(f[2]) != 1){
			error("bad log entry <%s>\n", t);
			free(t);
			continue;
		}
		free(t);
//		now = strtoul(f[0], 0, 0);
		lineno = atoi(f[1]);
		if(lineno < line0)
			continue;
		verb = f[2][0];
		name = f[3];
		file[Dst] = mkname(nbuf[Dst], sizeof nbuf[Dst], root[Dst], name);
		if(strcmp(f[4], "-") == 0)
			f[4] = f[3];
		file[Src] = mkname(nbuf[Src], sizeof nbuf[Src], root[Src], f[4]);
		file[Osrc] = mkname(nbuf[Osrc], sizeof nbuf[Osrc], root[Osrc], f[4]);
		nulldir(&rd);
//		rd.name = basename(f[4]);
		rd.mode = strtoul(f[5], 0, 8);
		rd.uid = f[6];
		rd.gid = f[7];
		rd.mtime = strtoul(f[8], 0, 10);
		rd.length = strtoll(f[9], 0, 10);

		switch(verb){
		case 'd':
			chat("d %q\n", name);
			if(donothing)
				break;
			if(remove(file[Dst]) == -1){
				if(access(file[Dst], AEXIST) == -1)
					warn("remove: %r", file[Dst]);
				else
					error("remove: %r", file[Dst]);
				continue;
			}
			break;
		case 'a':
			chat("a %q %luo %q %q %lud\n", name, rd.mode, rd.uid, rd.gid, rd.mtime);
			if(donothing)
				break;
			if((fd[Src] = open(file[Src], OREAD)) == -1)
				fatal("open %q: %r", file[Src]);
			if(rd.mode&DMDIR)
				i = OREAD|OEXEC;
			else
				i = ORDWR;
			if((fd[Dst] = create(file[Dst], i, rd.mode)) == -1)
				fatal("create: %r");
			if((rd.mode&DMDIR) == 0)
				copy(fd[Src], fd[Dst], file[Src], file[Dst], 0);
			assignmuid(fd[Src], file[Src], &rd);
			updatestat(file[Dst], fd[Dst], &rd);
			close(fd[Src]);
			close(fd[Dst]);
			break;
		case 'c':	/* change contents */
			chat("c %q\n", name);
			if(donothing)
				break;
			werrstr("");
			if(doopen(file, fd) == -1)
				continue;
			change(fd, file, &rd);
			assignmuid(fd[Src], file[Src], &rd);
			updatestat(file[Dst], fd[Dst], &rd);
			doclose(fd);
			break;
		case 'm':	/* change metadata */
			notexists(file[Src]);
			chat("m %q %luo %q %q %lud\n", name, rd.mode, rd.uid, rd.gid, rd.mtime);
			if(donothing)
				break;
			if((fd[Src] = open(file[Src], OREAD)) < 0)
				fatal("open %q: %r", file[Src]);
			if((fd[Dst] = open(file[Dst], OREAD)) < 0)
				fatal("open %q: %r", file[Dst]);
			if(rd.mode&DMDIR)
				rd.length = ~0ULL;
			assignmuid(fd[Src], file[Src], &rd);
			updatestat(file[Dst], fd[Dst], &rd);
			close(fd[Src]);
			close(fd[Dst]);
			break;
		}
	}
	if(errors)
		exits("errors");
	exits(nil);
}
