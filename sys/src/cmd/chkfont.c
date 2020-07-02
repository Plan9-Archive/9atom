#include <u.h>
#include <libc.h>
#include <ctype.h>

#define	WHITESPACE(c)		((c) == ' ' || (c) == '\t' || (c) == '\n')
#define	SPACE(c)			((c) == ' ' || (c) == '\t')

int fflag;

enum {
	P9BITLEN 	= 12,
};

typedef struct {
	char	*b;
	char	*file;
	char	*slash;
	Dir	*d;
} Slurp;

int
slurp(char* fil, int fd, int (*f)(Slurp*))
{
	Slurp S;
	int n;
	vlong l;

	memset(&S, 0, sizeof S);
	S.d = dirfstat(fd);
	if(!S.d)
		return 0;
	l = S.d->length;
	S.b = malloc(S.d->length+1);
	if(!S.b)
		sysfatal("malloc");
	seek(fd, 0, 0);
	if(readn(fd, S.b, l) != l){
		n = 0;
		goto done;
	}
	S.b[l] = 0;
	S.file = fil;
	S.slash = strrchr(S.file, '/');

	n = f(&S);
done:
	free(S.d);
	free(S.b);
	return n;
}

/*
 * pick up a number with
 * syntax _*[0-9]+_
 */
int
p9bitnum(char *bp)
{
	int n, c, len;

	len = P9BITLEN;
	while(*bp == ' ') {
		bp++;
		len--;
		if(len <= 0)
			return -1;
	}
	n = 0;
	while(len > 1) {
		c = *bp++;
		if(!isdigit(c))
			return -1;
		n = n*10 + c-'0';
		len--;
	}
	if(*bp != ' ')
		return -1;
	return n;
}

int
depthof(char *s, int *newp)
{
	char *es;
	int d;

	*newp = 0;
	es = s+12;
	while(s<es && *s==' ')
		s++;
	if(s == es)
		return -1;
	if('0'<=*s && *s<='9')
		return 1<<strtol(s, 0, 0);

	*newp = 1;
	d = 0;
	while(s<es && *s!=' '){
		s++;	/* skip letter */
		d += strtoul(s, &s, 10);
	}
	
	switch(d){
	case 32:
	case 24:
	case 16:
	case 8:
	case 4:
	case 2:
	case 1:
		return d;
	}
	return -1;
}

int
p9subfont(char *p)
{
	int n, h, a;

	n = p9bitnum(p + 0*P9BITLEN);	/* char count */
	if (n < 0)
		return 0;
	h = p9bitnum(p + 1*P9BITLEN);	/* height */
	if (h < 0)
		return 0;
	a = p9bitnum(p + 2*P9BITLEN);	/* ascent */
	if (a < 0)
		return 0;
	return 1;
}

int
cfontlen(char* buf, char* bufe, int, int, int loy, int, int hiy)
{
	int miny, maxy, nb;
	char *p;

	p = buf+5*P9BITLEN;
	for(miny = loy; p+24 <= bufe && miny != hiy; p += 24+nb){
		maxy = atoi(p+0*P9BITLEN);
		nb = atoi(p+1*P9BITLEN);
		if(maxy<=loy || hiy<maxy){
			werrstr("cimage: bady");
			return -1;
		}
		miny = maxy;
	}
	if(miny == hiy)
		return p-buf;
	werrstr("cimage: short file");
	return -1;
}

int
fontlen(char*, char*, int dep, int lox, int loy, int hix, int hiy)
{
	int px, t, len;

	if(dep < 8){
		px = 8/dep;	/* pixels per byte */
		/* set l to number of bytes of data per scan line */
		if(lox >= 0)
			len = (hix+px-1)/px - lox/px;
		else{	/* make positive before divide */
			t = (-lox)+px-1;
			t = (t/px)*px;
			len = (t+hix+px-1)/px;
		}
	}else
		len = (hix-lox)*dep/8;
	return len*(hiy-loy) + 5*P9BITLEN;
}

int
isp9bit(Slurp* S)
{
	int dep, lox, loy, hix, hiy, new;
	long len;
	char *buf, *bufe;
	int (*lenfn)(char*, char*, int, int, int, int, int);

	buf = S->b;
	lenfn = fontlen;
	if(strncmp(buf, "compressed\n", 11) == 0){
		lenfn = cfontlen;
		buf += 11;
	}
	bufe = S->b+S->d->length;
	if(bufe - buf < 5*P9BITLEN){
		werrstr("image: short file");
		return 0;
	}
	dep = depthof(buf + 0*P9BITLEN, &new);
	lox = p9bitnum(buf + 1*P9BITLEN);
	loy = p9bitnum(buf + 2*P9BITLEN);
	hix = p9bitnum(buf + 3*P9BITLEN);
	hiy = p9bitnum(buf + 4*P9BITLEN);
	if(dep < 0 || lox < 0 || loy < 0 || hix < 0 || hiy < 0){
		werrstr("image: bad rectangle");
		return 0;
	}
	if(lox>hix|| loy>hiy){
		werrstr("image: bad rectangle");
		return 0;
	}
	len = lenfn(buf, bufe, dep, lox, loy, hix, hiy);
	if(len < 0){
		werrstr("image: bady");
		return 0;
	}
	if(S->d->length < len+3*P9BITLEN) {
		werrstr("image: no font info");
		return 0;
	}
	if (p9subfont(buf+len))
		return 1;
	werrstr("image: bad font info");
	return 0;
}

int
getfontnum(char *cp, char **rp)
{
	char *p;
	ulong l;

	*rp = cp;
	l = strtoul(cp, &p, 0);
	if(p == cp || !WHITESPACE(*p))
		return 0;
	if(l > Runemax)
		return 0;
	*rp = p;
	return 1;
}

int
fchk(char *p, int m)
{
	int fd, r;

	fd = open(p, m);
	if(-1 == fd)
		return -1;
	if(fflag)
		r = 0;
	else
		r = slurp(p, fd, isp9bit);
	close(fd);
	return r;
}

int
faccess(char *p, int m)
{
	char* s;
	int i, r, err;

	r = fchk(p, m);
	if(r != -1)
		return r;
	s = p + strlen(p);
	r = 0;
	for(i = 0; i < 2; i++) {
		snprint(s, 4, ".%d", i);
		err = fchk(p, m);
		*s = 0;
		if(r != -1)
			r = err;
	}
	return r;
}
	
int
isp9font(Slurp *S)
{
	char *p, *cp;
	int i, n, err;
	char path[1024 + 1 + 3];

	cp = S->b;
	if (!getfontnum(cp, &cp)){	/* height */
		fprint(2, "%s:1 height\n", S->file);
		return 0;
	}
	if (!getfontnum(cp, &cp)){	/* ascent */
		fprint(2, "%s:1 ascent\n", S->file);
		return 0;
	}
	while(WHITESPACE(*cp))
		cp++;
	for (err = 0, i = 0; strchr(cp, '\n'); i++) {
		if (!getfontnum(cp, &cp)){	/* min */
			fprint(2, "%s:%d ascent\n", S->file, i+1);
			return 0;
		}
		if (!getfontnum(cp, &cp)){	/* max */
			fprint(2, "%s:%d ascent\n", S->file, i+1);
			return 0;
		}
		getfontnum(cp, &cp);	/* offset -- not required */
		while(SPACE(*cp))
			cp++;
		for(p = cp; !WHITESPACE(*cp); cp++)
			;
			/* construct a path name, if needed */
		n = 0;
		if (*p != '/' && S->slash) {
			n = S->slash-S->file+1;
			if (n < sizeof path)
				memcpy(path, S->file, n);
			else n = 0;
		}
		if (n+cp-p < sizeof path - sizeof ".00") {
			memcpy(path+n, p, cp-p);
			n += cp-p;
			path[n] = 0;
			if(faccess(path, AEXIST) < 0){
				fprint(2, "%s:%d (%s) %r\n", S->file, i+1, path);
				err++;
			}
		}
		while(WHITESPACE(*cp))
			cp++;
	}
	if(err)
		return 0;
	return i;
}

void
main(int argc, char **argv)
{
	int fd, err, n;

	ARGBEGIN{
	case 'f':
		fflag = 1;
		break;
	default:
		fprint(2, "usage: chkfont [-f] font\n");
		exits("usage");
	}ARGEND

	err = 0;
	if(argc>0)
		for(; *argv; argv++){
			fd = open(*argv, OREAD);
			n = -1;
			if(-1 == fd)
				fprint(2, "%s: %r\n", *argv);
			else
				n = slurp(*argv, fd, isp9font);
			close(fd);
			if(n < 1)
				err++;
		}
	else
		err = slurp("<stdin>", 0, isp9font);
	if(err)
		exits("bad");
	exits("");
}
