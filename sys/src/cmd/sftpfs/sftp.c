#include "fxp.h"

enum {
	MaxArg = 20,
};

static Biobuf *bin, *bout;

static void
execls(int argc, char *argv[])
{
	FHandle *h;
	Dir **d;
	int i, n;
	
	if(argc <= 1){
		fprint(2, "usage: ls dir\n");
		return;
	}
	h = fxpopendir(argv[1]);
	if(h == nil){
		fprint(2, "opendir failed: %r\n");
		return;
	}
	hexdump("handle = ", h->s, h->len);
	while((n = fxpreaddir(h, &d)) > 0){
		for(i = 0; i < n; i++){
			Bprint(bout, "Dir=%D\n", d[i]);
			free(d[i]);
		}
		free(d);
	}
	Bflush(bout);
	if(n < 0)
		fprint(2, "readdir failed: %r\n");
	if(fxpclose(h) < 0)
		fprint(2, "close failed: %r\n");
}

static void
execcat(int argc, char *argv[])
{
	FHandle *h;
	int n, off;
	char buf[1024];
	
	if(argc <= 1){
		fprint(2, "usage: cat file\n");
		return;
	}
	h = fxpopen(argv[1], OREAD);
	if(h == nil){
		fprint(2, "open failed: %r\n");
		return;
	}
	off = 0;
	while((n = fxpread(h, buf, sizeof buf, off)) > 0){
		Bwrite(bout, buf, n);
		off += n;
	}
	Bflush(bout);
	if(n < 0)
		fprint(2, "read: %r\n");
	if(fxpclose(h) < 0)
		fprint(2, "close failed: %r\n");
}

static void
execwrite(int argc, char *argv[])
{
	FHandle *h;
	char buf[1024];
	int nbuf;
	
	if(argc <= 2){
		fprint(2, "usage: write file string\n");
		return;
	}
	h = fxpcreate(argv[1], OWRITE, 0644);
	if(h == nil){
		fprint(2, "open failed: %r\n");
		return;
	}
	snprint(buf, sizeof(buf), "%s\n", argv[2]);
	nbuf = strlen(buf);
	if(fxpwrite(h, buf, nbuf, 0) != nbuf)
		fprint(2, "write failed: %r\n");
	if(fxpclose(h) < 0)
		fprint(2, "close failed: %r\n");
}

static void
execmkdir(int argc, char *argv[])
{
	ulong mode;
	
	if(argc <= 1){
		fprint(2, "usage: mkdir dir [mode]\n");
		return;
	}
	if(argc > 2)
		mode = strtoul(argv[2], nil, 8);
	else
		mode = 0755;
	if(fxpmkdir(argv[1], mode) < 0)
		fprint(2, "mkdir: %r\n");
}

static void
execrmdir(int argc, char *argv[])
{
	if(argc <= 1){
		fprint(2, "usage: rmdir\n");
		return;
	}
	if(fxprmdir(argv[1]) < 0)
		fprint(2, "rmdir: %r\n");
}

static void
execstat(int argc, char *argv[])
{
	Dir *d;
	
	if(argc <= 1){
		fprint(2, "usage: stat file\n");
		return;
	}
	if((d = fxpstat(argv[1])) == nil){
		fprint(2, "stat failed: %r\n");
		return;
	}
	Bprint(bout, "Dir=%D\n", d);
}

static void
execrm(int argc, char *argv[])
{
	if(argc <= 1){
		fprint(2, "usage: rm file\n");
		return;
	}
	if(fxpremove(argv[1]) < 0)
		fprint(2, "remove failed: %r\n");
}

static void
execmv(int argc, char *argv[])
{
	if(argc <= 2){
		fprint(2, "usage: mv oldfile newfile\n");
		return;
	}
	if(fxprename(argv[1], argv[2]) < 0)
		fprint(2, "rename failed: %r\n");
}

static void
execchmod(int argc, char *argv[])
{
	Dir d;
	
	if(argc <= 2){
		fprint(2, "usage: chmod mode file\n");
		return;
	}
	nulldir(&d);
	d.mode = strtol(argv[1], nil, 8);
	if(fxpsetstat(argv[2], &d) < 0)
		fprint(2, "setstat failed: %r\n");
}

struct Cmd {
	char *name;
	void (*fn)(int, char**);
} cmdtab[] = {
	"mkdir", execmkdir,
	"rmdir", execrmdir,
	"cat", execcat,
	"ls", execls,
	"write", execwrite,
	"stat", execstat,
	"rm", execrm,
	"mv", execmv,
	"chmod", execchmod,
};

void
threadmain(int argc, char *argv[])
{
	char *arg[MaxArg];
	char *line;
	int i, n;
	
	fmtinstall('D', dirfmt);
	
	ARGBEGIN{
	}ARGEND;
	
	bin = emalloc9p(sizeof *bin);
	bout = emalloc9p(sizeof *bout);
	Binit(bin, 0, OREAD);
	Binit(bout, 1, OWRITE);
	
	if(fxpinit("k7", 2, "/dev/null") < 0)
		sysfatal("fxpinit: %r");
	print("init success\n");
	
	for(;;){
		Bprint(bout, "sftp> ");
		Bflush(bout);
		
		line = Brdline(bin, '\n');
		if(line == nil)
			break;
		line[Blinelen(bin)-1] = 0;
		
		if((n = tokenize(line, arg, nelem(arg))) == 0)
			continue;
		
		for(i = 0; i < nelem(cmdtab); i++)
			if(strcmp(cmdtab[i].name, arg[0]) == 0)
			if(cmdtab[i].fn != nil){
				cmdtab[i].fn(n, arg);
				break;
			}
		if(i == nelem(cmdtab))
			fprint(2, "command not found\n");
		Bflush(bout);
	}
		
	fxpterm();
}
