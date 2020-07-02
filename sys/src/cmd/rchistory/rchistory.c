/*
 *	emulate byron's emulation of 8th edition history.
 *	this requires some changes to rc to emit history
 *
 *	install this program as "-p" and it will print the last
 *	matching line of history.  a simple match is performed.
 *
 *	bugs:
 *		(a) limited testing; "it works for me".
 *		(b) may have unicode corner cases.
 *		(c) will not work with a prompt other than '; '
 *
 */

#include <u.h>
#include <libc.h>
#include <ctype.h>

enum {
	bufsize	= 8192,
	maxstr	= 512,
	fullbuf	= bufsize + maxstr,
};

vlong	fp;
vlong	fpstop;

static vlong
eseek(int fd, vlong whence, int type)
{
	vlong r;

	r = seek(fd, whence, type);
	if(r == -1)
		sysfatal("seek: %r");
	return r;
}

static char*
memrchr(char *buf, int c, int l)
{
	char *p;

	for(p = buf+l; --p >= buf; )
		if(*p == c)
			return p;
	return nil;
}

static char*
memrstr(char *buf, char *s, int l, int n)
{
	char *p;
	int c, pc;

	if((c = *s)==0)
		return buf+n-1;
	for(p = buf+n; --p>=buf; ) {
		pc = *p;
		if(pc == '\n')
			fpstop = fp+(p-buf);
		if(pc == c && strncmp(p, s, l) == 0) 
			return p;
	}
	return 0;
}

static char*
rcqstrchr(char *s, char c)
{
	int q, sc;

	q = 0;
	while(sc = *s++){
		if(sc == '\''){
			if(q && *s == '\'')
				s++;
			else
				q ^= 1;
			continue;
		}
		if(!q && sc == c)
			return s - 1;
	}
	return nil;
}

static int
ishistory(char *s)
{
	char *t, *u;

	for(t = s; t = rcqstrchr(t, '-'); t++){
		u = t-1;
		while(u > s && isspace(*u))
			u--;
		if(u < s || strchr(";`{|()[]<>", *u))
			return 1;
	}
	return 0;
}

static int
whistory(int fd, char* line)
{
	eseek(fd, 0, 2);
	return write(fd, line, strlen(line));
}

/* 
 * we're going backwards; hard to use bio
 */
char *
scanhistory(int fd, char *s){
	char buf[fullbuf], *p, *line;
	int l, n;
	vlong end, begin;

	line = 0;
	l = strlen(s);
	if(l >= maxstr)
		sysfatal("search too long");

	fp = eseek(fd, 0, 2);
top:	
	fpstop = fp;
	memset(buf, 0, l);
	for(p = 0; fp > 0 && p == 0;){
		n = bufsize;
		if(n > fp)
			n = fp;
		fp = eseek(fd, fp-n, 0);
		if(readn(fd, buf, n+l) < 0)
			sysfatal("read: %r");
		p = memrstr(buf, s, l, n+l);
	}

	if(!p)
		goto lose;
	end = fpstop;

	n = p-buf;
	for(;;){
		if(p = memrchr(buf, '\n', n))
			break;
		if(fp == 0)
			goto lose;
		n = bufsize;
		if(n > fp)
			n = fp;
		fp = eseek(fd, fp-n, 0);
		if(n != readn(fd, buf, n))
			sysfatal("read: %r");
	}

	if(p)
		p++;
	else
		p = buf;

	begin = fp+(p-buf);
	fp = eseek(fd, begin, 0);
	
	n = end-begin+1;
	line = realloc(line, n+1);
	if(line == nil)
		sysfatal("malloc: %r");
	readn(fd, line, n);
	line[n] = 0;

	if(ishistory(line)) {
		if(fp > 0)
			fp--;
		goto top;
	}	
	return line;
lose:
	free(line);
	return 0;
}

void
main(int, char **v)
{
	char *h, *line;
	int fd;
	Fmt f;

	if((h = getenv("history")) == nil)
		sysfatal("no history file");
	if((fd = open(h, ORDWR)) < 0)
		sysfatal("history: %s, %r", h);
	free(h);

	fmtstrinit(&f);
	for(v++; *v; v++)
		fmtprint(&f, "%s ", *v);
	h = fmtstrflush(&f);
	h[strlen(h)-1] = 0;

	
	if((line = scanhistory(fd, h)) != 0){
		fprint(2, "%s", line);
		whistory(fd, line);
		free(line);
	}
	close(fd);
	exits("");
}
