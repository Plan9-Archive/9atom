#include	"all.h"
#include	"io.h"
#include	"../ip/ip.h"

enum{
	Mfs	= 1<<0,
	Mip	= 1<<1,		// c.f. ip.h for what these mean.
	Mservice	= 1<<2,
	Msntp	= 1<<3,
};

struct
{
	char*	icharp;
	char*	charp;
	int	error;
	int	newconf;	/* clear before start */
	uint	modconf;	/* modification bit array */
	int	nextiter;
	int	lastiter;
	int	diriter;
	Device*	lastcw;
	Device*	devlist;
} f;

static Device* confdev;

int
devcmpr(Device *d1, Device *d2)
{
	while (d1 != d2) {
		if(d1 == 0 || d2 == 0 || d1->type != d2->type)
			return 1;

		switch(d1->type) {
		default:
			print("can't compare dev: %Z\n", d1);
			panic("devcmp");
			return 1;

		case Devmcat:
		case Devmlev:
		case Devmirr:
			d1 = d1->cat.first;
			d2 = d2->cat.first;
			while(d1 && d2) {
				if(devcmpr(d1, d2))
					return 1;
				d1 = d1->link;
				d2 = d2->link;
			}
			break;

		case Devnone:
			return 0;

		case Devro:
			d1 = d1->ro.parent;
			d2 = d2->ro.parent;
			break;

		case Devjuke:
		case Devcw:
			if(devcmpr(d1->cw.c, d2->cw.c))
				return 1;
			d1 = d1->cw.w;
			d2 = d2->cw.w;
			break;

		case Devfworm:
			d1 = d1->fw.fw;
			d2 = d2->fw.fw;
			break;

		case Devwren:
		case Devworm:
		case Devlworm:
		case Devide:
		case Devmv:
		case Devaoe:
		case Devia:
			if(d1->wren.ctrl == d2->wren.ctrl)
			if(d1->wren.targ == d2->wren.targ)
			if(d1->wren.lun == d2->wren.lun)
				return 0;
			return 1;

		case Devpart:
			if(d1->part.base == d2->part.base)
			if(d1->part.size == d2->part.size) {
				d1 = d1->part.d;
				d2 = d2->part.d;
				break;
			}
			return 1;
		}
	}
	return 0;
}

void
cdiag(char *s, int c1)
{

	f.charp--;
	if(f.error == 0) {
		print("config diag: %s -- <%c>\n", s, c1);
		f.error = 1;
	}
}

static vlong
cnumb(void)
{
	int c;
	vlong n;

	c = *f.charp++;
	if(c == '<') {
		n = f.nextiter;
		if(n >= 0) {
			f.nextiter = n+f.diriter;
			if(n == f.lastiter) {
				f.nextiter = -1;
				f.lastiter = -1;
			}
			do {
				c = *f.charp++;
			} while (c != '>');
			return n;
		}
		n = cnumb();
		if(*f.charp++ != '-') {
			cdiag("- expected", f.charp[-1]);
			return 0;
		}
		c = cnumb();
		if(*f.charp++ != '>') {
			cdiag("> expected", f.charp[-1]);
			return 0;
		}
		f.lastiter = c;
		f.diriter = 1;
		if(n > c)
			f.diriter = -1;
		f.nextiter = n+f.diriter;
		return n;
	}
	if(c < '0' || c > '9') {
		cdiag("number expected", c);
		return 0;
	}
	n = 0;
	while(c >= '0' && c <= '9') {
		n = n*10 + (c-'0');
		c = *f.charp++;
	}
	f.charp--;
	return n;
}

static char*
cstring(void)
{
	char c, *s, *p, *e;

	p = s = ialloc(NAMELEN, 0);
	e = p+NAMELEN-1;

	f.charp++;
	for(; p < e; ){
		c = *f.charp;
		f.charp++;
		if(c == '"'){
			if(*f.charp != '"')
				break;
			f.charp++;
		}
		if(c == 0){
			cdiag("nil in string", '?');
			break;
		}
		*p++ = c;
	}
	*p = 0;
	if(p == s)
		cdiag("nil string", *f.charp);
	return s;
}

static Device*
config1(int c)
{
	Device *d, *t;
	int m;

	d = ialloc(sizeof(Device), 0);
	for(;;) {
		t = config();
		if(d->cat.first == 0)
			d->cat.first = t;
		else
			d->cat.last->link = t;
		d->cat.last = t;
		if(f.error)
			return devnone;
		m = *f.charp;
		if(c == '(' && m == ')') {
			d->type = Devmcat;
			break;
		}
		if(c == '[' && m == ']') {
			d->type = Devmlev;
			break;
		}
		if(c == '{' && m == '}') {
			d->type = Devmirr;
			break;
		}
	}
	f.charp++;
	if(d->cat.first == d->cat.last)
		d = d->cat.first;
	d->dlink = f.devlist;
	f.devlist = d;
	return d;
}

Device*
config(void)
{
	int c, m;
	Device *d;
	char *icp;
	static int seq;

	if(f.error)
		return devnone;
	d = ialloc(sizeof(Device), 0);
	d->dno = seq++;
	c = *f.charp++;
	switch(c) {
	default:
		print("%x\n", c);
		cdiag("unknown type", c);
		return devnone;

	case '(':	/* (d+) one or multiple cat */
	case '[':	/* [d+] one or multiple interleave */
	case '{':	/* {d+} a mirrored device and optional mirrors */
		return config1(c);

	case 'f':	/* fd fake worm */
		d->type = Devfworm;
		d->fw.fw = config();
		break;

	case 'n':
		d->type = Devnone;
		break;

	case 'w':	/* w[#.]#[.#] wren	[ctrl] unit [lun] */
	case 'h':	/* h[#.]# ide		[ctlr] unit */
	case 'm':	/* m[#.]# marvell sata	[ctlr] unit */
	case 'e':	/* e[#.]# aoe		[ctlr] unit */
	case 'a':	/* a[#.]# iasata		[ctlr] unit */
	case 'r':	/* r# worm side */
	case 'l':	/* l# labelled-worm side */
		icp = f.charp;
		if(c == 'm')
			d->type = Devmv;
		else if(c == 'h')
			d->type = Devide;
		else if(c == 'e')
			d->type = Devaoe;
		else if(c == 'a')
			d->type = Devia;
		else
			d->type = Devwren;
		d->wren.ctrl = 0;
		d->wren.targ = cnumb();
		d->wren.lun = 0;
		m = *f.charp;
		if(m == '.') {
			f.charp++;
			d->wren.lun = cnumb();
			m = *f.charp;
			if(m == '.') {
				f.charp++;
				d->wren.ctrl = d->wren.targ;
				d->wren.targ = d->wren.lun;
				d->wren.lun = cnumb();
			}
		}
		if(f.nextiter >= 0)
			f.charp = icp-1;
		if(c == 'r') {	/* worms are virtual and not uniqued */
			d->type = Devworm;
			break;
		}
		if(c == 'l') {
			d->type = Devlworm;
			break;
		}
		break;

	case 'o':	/* o ro part of last cw */
		if(f.lastcw == 0) {
			cdiag("no cw to match", c);
			return devnone;
		}
		f.lastcw->cw.ro->dno = d->dno;
		return f.lastcw->cw.ro;

	case 'j':	/* DD jukebox */
		d->type = Devjuke;
		d->j.j = config();
		d->j.m = config();
		break;

	case 'c':	/* cache/worm */
		d->type = Devcw;
		d->cw.c = config();
		d->cw.w = config();
		d->cw.ro = ialloc(sizeof(Device), 0);
		d->cw.ro->type = Devro;
		d->cw.ro->ro.parent = d;
		f.lastcw = d;
		break;

	case 'p':	/* pd#.# partition base% size% */
		d->type = Devpart;
		d->part.d = config();
		if(*f.charp == '"'){
			d->part.name = cstring();
			break;
		}
		d->part.base = cnumb();
		c = *f.charp++;
		if(c != '.')
			cdiag("dot expected", c);
		d->part.size = cnumb();
		if(d->part.size > 100){
			if(d->part.size <= d->part.base)
				cdiag("partion invalid end", '?');
//			d->part.size -= d->part.base;
		}
		break;

	case 'x':	/* xD swab a device's metadata */
		d->type = Devswab;
		d->swab.d = config();
		break;
	}
	d->dlink = f.devlist;
	f.devlist = d;
	return d;
}

static char*
strdup(char *s)
{
	int n;
	char *s1;

	n = strlen(s);
	s1 = ialloc(n+1, 0);
	strcpy(s1, s);
	return s1;
}

Device*
iconfig(char *s)
{
	Device *d;

	f.nextiter = -1;
	f.lastiter = -1;
	f.error = 0;
	f.icharp = s;
	f.charp = f.icharp;
	d = config();
	if(*f.charp) {
		cdiag("junk on end", *f.charp);
		f.error = 1;
	}
	return d;
}

Device*
devstr(char *s)
{
	Device *d;

	if((d = iconfig(s)) && f.error == 0)
		return d;
	return 0;
}

static int
testconfig(char *s)
{
	iconfig(s);
	return f.error;
}

static int
astrcmp(char *a, char *b)
{
	int n, c;

	n = strlen(b);
	if(memcmp(a, b, n))
		return 1;
	c = a[n];
	if(c == 0) {
		aindex = 0;
		return 0;
	}
	if(a[n+1])
		return 1;
	if(c >= '0' && c <= '9') {
		aindex = c - '0';
		return 0;
	}
	return 1;
}

static char*
getipflag(char *word, char *cp, uchar *f)
{
	*f = 0;
	for(;;){
		cp = getwd(word, cp);
		if(*word == 0)
			break;
		*f |= strtoipflag(word);
		if(*f == 0xff){
			print("bad ip flag\n");
			continue;
		}
	}
	return cp;
}

static void
mergeconf(Iobuf *p)
{
	char word[Maxword+1];
	char *cp;
	Filsys *fs;

	for (cp = p->iobuf; ; cp++) {
		cp = getwd(word, cp);
		if(strcmp(word, "") == 0)
			return;
		else if(strcmp(word, "service") == 0) {
			cp = getwd(word, cp);
			if((f.modconf&Mservice) == 0)
				strcpy(service, word);
		}
		else if(astrcmp(word, "ip") == 0) {
			cp = getwd(word, cp);
			if((ipaddr[aindex].modfl&Msysip) == 0)
			if(chartoip(ipaddr[aindex].sysip, word))
				goto bad;
		} else if(astrcmp(word, "ipgw") == 0) {
			cp = getwd(word, cp);
			if((ipaddr[aindex].modfl&Mdefgwip) == 0)
			if(chartoip(ipaddr[aindex].defgwip, word))
				goto bad;
		} else if(astrcmp(word, "ipsntp") == 0) {
			cp = getwd(word, cp);
			if((f.modconf&Msntp) == 0)
			if(chartoip(sntpip, word))
				goto bad;
		} else if(astrcmp(word, "ipmask") == 0) {
			cp = getwd(word, cp);
			if((ipaddr[aindex].modfl&Mdefmask) == 0)
			if(chartoip(ipaddr[aindex].defmask, word))
				goto bad;
		}else if(astrcmp(word, "ipflag") == 0){
			uchar *p, d;
			if((ipaddr[aindex].modfl&Mflag) == 0)
				p = &ipaddr[aindex].flag;
			else
				p = &d;
			cp = getipflag(word, cp, p);
			if(d== 0xff)
				goto bad;
		} else if(strcmp(word, "filsys") == 0) {
			cp = getwd(word, cp);
			for(fs=filsys; fs->name; fs++)
				if(strcmp(fs->name, word) == 0) {
					if(fs->flags & FEDIT) {
						cp = getwd(word, cp);
						goto loop;
					}
					break;
				}
			fs->name = strdup(word);
			cp = getwd(word, cp);
			fs->conf = strdup(word);
		} else {
bad:
			putbuf(p);
			panic("unknown word in config block: %s", word);
		}
loop:
		if(*cp != '\n')
			goto bad;
	}
}

void
cmd_printconf(int, char *[])
{
	char *p, *s;
	Iobuf *iob;

	iob = getbuf(confdev, 0, Bread);
	if(iob == nil)
		return;
	if(checktag(iob, Tconfig, 0)){
		putbuf(iob);
		return;
	}

	print("config %s\n", nvrgetconfig());
	s = p = iob->iobuf;
	while(*p != 0 && p < iob->iobuf+BUFSIZE){
		if(*p++ != '\n')
			continue;
		print("%.*s", (int)(p-s), s);
		s = p;
	}
	if(p != s)
		print("%.*s", (int)(p-s), s);
	print("end\n");

	putbuf(iob);
}

void
cmd_writeconf(int, char *[])
{
	Iobuf *iob;

	iob = getbuf(confdev, 0, Bread);
	if(iob == nil)
		return;
	if(checktag(iob, Tconfig, 0)){
		putbuf(iob);
		return;
	}
	iob->flags |= Bmod|Bimm;
	putbuf(iob);
	print("written\n");
}

void
sysinit(void)
{
	Filsys *fs;
	int error, i;
	Device *d;
	Iobuf *p;
	char *cp, *s, *e;

	dofilter(&cons.work);
	dofilter(&cons.rate);
	dofilter(&cons.bhit);
	dofilter(&cons.bread);
	dofilter(&cons.brahead);
	dofilter(&cons.binit);
	cons.chan = chaninit(Devcon, 1, 0);

start:
	/*
	 * part 1 -- read the config file
	 */
	devnone = iconfig("n");

	cp = nvrgetconfig();
	print("config %s\n", cp);

	confdev = d = iconfig(cp);
	devinit(d);
	if(f.newconf) {
		p = getbuf(d, 0, Bmod);
		memset(p->iobuf, 0, RBUFSIZE);
		settag(p, Tconfig, 0);
	} else
		p = getbuf(d, 0, Bread|Bmod);
	if(!p || checktag(p, Tconfig, 0))
		panic("config io");
	mergeconf(p);
	if(f.modconf) {
		memset(p->iobuf, 0, BUFSIZE);
		s = p->iobuf;
		e = s+BUFSIZE;
		p->flags |= Bmod|Bimm;
		s = seprint(s, e, "service %s\n", service);
		for(fs=filsys; fs->name; fs++)
			if(fs->conf)
				s = seprint(s, e, "filsys %s %s\n", fs->name, fs->conf);
		if(isvalidip(sntpip))
			s = seprint(s, e, "ipsntp %I\n", sntpip);
		for(i=0; i<10; i++) {
			if(isvalidip(ipaddr[i].sysip))
				s = seprint(s, e, "ip%d %I\n", i, ipaddr[i].sysip);
			if(isvalidip(ipaddr[i].defgwip))
				s = seprint(s, e, "ipgw%d %I\n", i, ipaddr[i].defgwip);
			if(isvalidip(ipaddr[i].defmask))
				s = seprint(s, e, "ipmask%d %I\n", i, ipaddr[i].defmask);
			if(ipaddr[i].flag)
				s = seprint(s, e, "ipflag%d %φ\n", i, ipaddr[i].flag);
		}
		putbuf(p);
		f.modconf = 0;
		f.newconf = 0;
		print("config block written\n");
		goto start;
	}
	putbuf(p);

	print("service    %s\n", service);
	if(isvalidip(sntpip))
		print("ipsntp  %I\n", sntpip);
	for(i=0; i<10; i++)
		if(isvalidip(ipaddr[i].sysip)) {
			print("ip%d     %I\n", i, ipaddr[i].sysip);
			print("ipgw%d   %I\n", i, ipaddr[i].defgwip);
			print("ipmask%d %I\n", i, ipaddr[i].defmask);
			if(ipaddr[i].flag)
				print("ipflag%d %φ\n", i, ipaddr[i].flag);
		}

loop:
	/*
	 * part 2 -- squeeze out the deleted filesystems
	 */
	for(fs=filsys; fs->name; fs++)
		if(fs->conf == 0) {
			for(; fs->name; fs++)
				*fs = *(fs+1);
			goto loop;
		}
	if(filsys[0].name == 0)
		panic("no filsys");

	/*
	 * part 3 -- compile the device expression
	 */
	error = 0;
	for(fs=filsys; fs->name; fs++) {
		print("filsys %s %s\n", fs->name, fs->conf);
		fs->dev = iconfig(fs->conf);
		if(f.error) {
			error = 1;
			continue;
		}
	}
	if(error)
		panic("fs config");

	/*
	 * part 3½ -- start ethernet
	 */
	etherstart();
	delay(1000);		/* hack.  pretend this will get all ifs up before we restart */
	aoeinit(0);

	/*
	 * part 4 -- initialize the devices
	 */
	for(fs=filsys; fs->name; fs++) {
		delay(3000);
		print("sysinit: %s\n", fs->name);
		if(fs->flags & FREAM)
			devream(fs->dev, 1);
		if(fs->flags & FRECOVER)
			devrecover(fs->dev);
		devinit(fs->dev);
	}
}

void
getline(char *line)
{
	char *p;
	int c;

	p = line;
	for(;;) {
		c = rawchar(0);
		if(c == 0 || c == '\n') {
			*p = 0;
			return;
		}
		if(c == '\b') {
			p--;
			continue;
		}
		*p++ = c;
	}
}

void
arginit(void)
{
	int verb, c;
	char line[2*Maxword], word[Maxword+1], *cp;
	uchar ip[Pasize];
	Filsys *fs;

	if(nvrcheck() == 0){
		print("for config mode hit a key within 5 seconds\n");
		c = rawchar(5);
		if(c == 0) {
			print("	no config\n");
			return;
		}
	}

	for (;;) {
		print("config: ");
		getline(line);
		cp = getwd(word, line);
		if (word[0] == '\0' || word[0] == '#')
			continue;
		if(strcmp(word, "end") == 0)
			return;
		if(strcmp(word, "halt") == 0) 
			exit();
		if(strcmp(word, "noattach") == 0) {
			noattach = !noattach;
			continue;
		}
		if(strcmp(word, "readonly") == 0) {
			readonly = 1;
			continue;
		}

		if(strcmp(word, "ream") == 0) {
			verb = FREAM;
			goto gfsname;
		}
		if(strcmp(word, "recover") == 0) {
			verb = FRECOVER;
			goto gfsname;
		}
		if(strcmp(word, "recoversb") == 0){
			getwd(word, cp);
			conf.recovsb = strtoull(word, 0, 0);
			continue;
		}
		if(strcmp(word, "filsys") == 0) {
			verb = FEDIT;
			goto gfsname;
		}

		if(strcmp(word, "nvram") == 0) {
			getwd(word, cp);
			if(testconfig(word))
				continue;
			/* if it fails, it will complain */
			nvrsetconfig(word);
			continue;
		}
		if(strcmp(word, "config") == 0) {
			getwd(word, cp);
			if(!testconfig(word) && nvrsetconfig(word) == 0)
				f.newconf = 1;
			continue;
		}
		if(strcmp(word, "service") == 0) {
			getwd(word, cp);
			strcpy(service, word);
			f.modconf |= Mservice;
			continue;
		}
		if(astrcmp(word, "ipflag") == 0){
			getipflag(word, cp, &ipaddr[aindex].flag);
			if(ipaddr[aindex].flag== 0xff)
				continue;
			f.modconf |= Mip;
			ipaddr[aindex].modfl |= Mflag;
			continue;
		}

		if(astrcmp(word, "ip") == 0)
			verb = Msysip;
		else if(astrcmp(word, "ipgw") == 0)
			verb = Mdefgwip;
		else if(astrcmp(word, "ipmask") == 0)
			verb = Mdefmask;
		else if(astrcmp(word, "ipsntp") == 0)
			verb = Msntp;
		else {
			print("unknown config command\n");
			print("	type end to get out\n");
			continue;
		}

		getwd(word, cp);
		if(chartoip(ip, word)) {
			print("bad ip address\n");
			continue;
		}
		switch(verb) {
		case Msysip:
			memmove(ipaddr[aindex].sysip, ip, Pasize);
			break;
		case Mdefmask:
			memmove(ipaddr[aindex].defmask, ip, Pasize);
			break;
		case Mdefgwip:
			memmove(ipaddr[aindex].defgwip, ip, Pasize);
			break;
		case Msntp:
			memmove(sntpip, ip, Pasize);
			/* BOTCH */
			f.modconf |= Msntp;
			continue;
		}
		f.modconf |= Mip;
		ipaddr[aindex].modfl |= verb;
		continue;

	gfsname:
		cp = getwd(word, cp);
		for(fs=filsys; fs->name; fs++)
			if(strcmp(word, fs->name) == 0)
				break;
		if (fs->name == nil) {
			memset(fs, 0, sizeof(*fs));
			fs->name = strdup(word);
		}
		switch(verb) {
		case FREAM:
		case FRECOVER:
			fs->flags |= verb;
			break;
		case FEDIT:
			f.modconf |= Mfs;
			getwd(word, cp);
			fs->flags |= verb;
			if(word[0] == 0)
				fs->conf = nil;
			else if(!testconfig(word))
				fs->conf = strdup(word);
			break;
		}
	}
}
