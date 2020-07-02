#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <keyboard.h>
#include <mouse.h>
#include <thread.h>

Channel	*wc;
Image *i;

typedef struct Rc Rc;
struct Rc{
	int	fd[2];
	char	*cmd;
	uint	nms;
};

void
resize(Point p)
{
	int fd;

	fd = open("/dev/wctl", OWRITE);
	if(fd >= 0){
		fprint(fd, "resize -dx %d -dy %d", p.x+4*2, p.y+4*2);
		close(fd);
	}
}

void
resizeto(Point p)
{
	Point s;

	s = (Point){Dx(screen->r), Dy(screen->r)};
	if(eqpt(p, s))
		return;
	resize(p);
}

void
eresized(int new)
{
	static int delaynew = 1;

	if(new && getwindow(display, Refnone) < 0)
		sysfatal("imgloop: can't reattach to window\n");
	if(new)
		delaynew = 1;
	if(i == nil)
		return;
	if(delaynew){
		resizeto((Point){Dx(i->r), Dy(i->r)});
		delaynew = 0;
	}
//	drawop(screen, r, image, nil, image->r.min, S);
	draw(screen, screen->clipr, i, display->white, ZP);
	flushimage(display, 1);
}

void
runrcproc(void *v)
{
	int nfd;
	Rc *rc;

	rc = v;
	nfd = open("/dev/null", ORDWR);
	dup(nfd, 0);
	dup(rc->fd[1], 1);
	close(rc->fd[0]);
	close(nfd);
	procexecl(wc, "/bin/rc", "rc", "-c", rc->cmd, nil);
}

int
rcproc(void *v)
{
	Rc *rc;
	Image *ni, *oi;
	enum { Flags = RFFDG|RFPROC, };

	rc = v;
	threadsetname("imgloop %s", rc->cmd);
	chancreate(sizeof(ulong), 1);
	for(;;){
		if(pipe(rc->fd) == -1)
			sysfatal("pipe: %r");
		if(procrfork(runrcproc, rc, 8*1024, Flags) == -1)
			return -1;
		close(rc->fd[1]);
		ni = readimage(display, rc->fd[0], 0);
		oi = i;
		i = ni;
		eresized(0);
		freeimage(oi);
		eresized(0);
		close(rc->fd[0]);
//		recvul(wc);		/* eat pid */
		sleep(rc->nms);
	}
}

void
resizedproc(void*)
{
	char buf[128], *f[7];
	int n, fd;

	threadsetname("resizeproc");
	fd = open("/dev/wctl", OREAD);
	for(;;){
		n = read(fd, buf, sizeof buf-1);
		if(n<4*12)
			sysfatal("bad wctl: %r");
		buf[n] = 0;
		n = tokenize(buf, f, nelem(f));
		if(n != 6)
			sysfatal("bad wctl: %d fields", n);
		if(strcmp(f[4], "current") == 0
		&& strcmp(f[5], "visible") == 0)
			eresized(1);
	}
}

void
mouseproc(void *)
{
	void *v;
	Rune r;
	Alt alts[4];
	Keyboardctl *k;
	Mousectl *m;
	Mouse mc;
	enum{Amouse, Akbd, Aresize, Aend};

	m = initmouse("/dev/mouse", nil);
	k = initkeyboard("/dev/cons");

	memset(alts, 0, sizeof alts);
	alts[Amouse].c = m->c;
	alts[Amouse].v = &mc;
	alts[Amouse].op = CHANRCV;

	alts[Akbd].c = k->c;
	alts[Akbd].v = &r;
	alts[Akbd].op = CHANRCV;

	alts[Aresize].c = m->resizec;
	alts[Aresize].v = &v;
	alts[Aresize].op = CHANRCV;

	alts[Aend].op = CHANEND;

	for(;;)
		switch(alt(alts)){
		default:
			sysfatal("mouse!");
		case Amouse:
			break;
		case Akbd:
			switch(r){
			case 'q':
			case 0x7f:
			case 0x04:
				threadexitsall("");
			}
			break;
		case Aresize:
			break;
		}
}

void
derror(Display*, char *s)
{
	sysfatal("%s", s);
}

void
usage(void)
{
	fprint(2, "usage: imgloop [-i framems] [-c cmd]\n");
	threadexits("usage");
}

char*
argstr(int argc, char **argv)
{
	char *s, *p, *e;
	int i, len;

	len = 0;
	for(i = 0; i < argc; i++)
		len += strlen(argv[i])+1;
	s = malloc(len+1);
	e = s + len+1;
	p = s;
	for(i = 0; i < argc; i++)
		p = seprint(p, e, "%s ", argv[i]);
	seprint(p, e, "\n");

	if(p == s)
		usage();
	return s;
}

void
threadmain(int argc, char **argv)
{
	Rc rc;

	memset(&rc, 0, sizeof rc);
	rc.nms = 2*60*1000;
	rc.cmd = "radar -9";

	ARGBEGIN{
	default:
		usage();
	case 'i':
		rc.nms = atoi(EARGF(usage()));
		break;
	case 'c':
		rc.cmd = argstr(argc-1, argv+1);
		goto done;
	}ARGEND
done:
	if(geninitdraw(nil, derror, "/lib/font/bit/cyberbit/mod14.font", "imgloop", nil, Refnone) == -1)
		sysfatal("imgloop: can't open display: %r");
	eresized(0);
	proccreate(mouseproc, nil, 8192);
	proccreate(resizedproc, nil, 8192);
	rcproc(&rc);
	threadexits("");
}
