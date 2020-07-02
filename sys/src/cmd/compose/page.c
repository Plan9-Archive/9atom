#include "compose.h"

void
intimg(C *c, char *path, int fd)
{
	memset(c, 0, sizeof *c);
	c->path = path;
	c->fd = fd;

	if(c->fd == -1)
		c->fd = open(c->path, OREAD);
	if(c->fd == -1)
		sysfatal("open: %r");
}

Memimage*
pageimg(C *c, ulong chan)
{
	char *args[10];
	int i, p[2];
	Memimage *t;

	if(c->im)
		return c->im;
	if(pipe(p) == -1)
		sysfatal("pipe: %r");
	switch(fork()){
	case -1:
		sysfatal("fork: %r");
	case 0:
		i = 0;
		args[i++] = "/bin/imgtype";
		args[i++] = "imgtype";
		if(flag['t'])
			args[i++] = "-t";
		args[i++] = "-e";
		args[i] = nil;
		close(p[0]);
		close(0);
		dup(c->fd, 0);
		dup(p[1], 1);
		exec(args[0], args+1);
		sysfatal("exec: %r");
	}
	close(p[1]);
	c->im = readmemimage(p[0]);
	if(c->im == nil)
		sysfatal("readmemimage: %r");
	if(chan != 0){
		t = allocmemimage(c->im->r, chan);
		if(t == nil)
			sysfatal("readmemimage: %r");
		memfillcolor(t, DTransparent);
		memdraw(t, t->r, c->im, ZP, nil, ZP, SoverD);
		freememimage(c->im);
		c->im = t;
	}
	close(p[0]);
	return c->im;
}

void
closeimg(C *c)
{
	freememimage(c->im);
	c->im = nil;
	if(c->path)
		close(c->fd);
}
