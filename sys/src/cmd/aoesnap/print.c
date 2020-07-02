#include <u.h>
#include <goo.h>

enum{
	Flag	= 1,
	PTR	= sizeof(char*),
	SHORT	= sizeof(int),
	INT	= sizeof(int),
	LONG	= sizeof(long),
	VLONG	= sizeof(vlong),
	IDIGIT	= 64+64/3+2,
	MAXCON	= 30,
};

static	int	convcount  = 15;

static	int	noconv(Fmt*);
static	int	bconv(Fmt*);
static	int	cconv(Fmt*);
static	int	dconv(Fmt*);
static	int	hconv(Fmt*);
static	int	lconv(Fmt*);
static	int	oconv(Fmt*);
static	int	rconv(Fmt*);
static	int	sconv(Fmt*);
static	int	uconv(Fmt*);
static	int	xconv(Fmt*);
static	int	percent(Fmt*);
static	int	enet(Fmt*);
static	int	pconv(Fmt*);
static	int	sharpconv(Fmt*);

static
int	(*fmtconv[MAXCON])(Fmt*) =
{
	noconv,
	bconv, cconv, dconv, hconv, lconv,
	oconv, rconv, sconv, uconv, xconv,
	percent, enet, pconv, sharpconv
};

static
uchar	fmtindex[128] =
{
	['b'] 1,
	['c'] 2,
	['d'] 3,
	['h'] 4,
	['l'] 5,
	['o'] 6,
	['r'] 7,
	['s'] 8,
	['u'] 9,
	['x'] 10,
	['%'] 11,
	['E'] 12,
	['p'] 13,
	['#'] 14,
};

int
fmtinstall(int c, int (*f)(Fmt*))
{
	c &= 0177;
	if(fmtindex[c] == 0) {
		if(convcount >= MAXCON)
			return 1;
		fmtindex[c] = convcount++;
	}
	fmtconv[fmtindex[c]] = f;
	return 0;
}

int
strflush(Fmt *f)
{
	USED(f);
	return -1;
}

int
vsnprint(char *s, int len, char *fmt, va_list args)
{
	Fmt f;
	int n;

	f.buf = s;
	f.p = s;
	f.ep = s+len-1;
	f.flush = strflush;
	n = vfmtprint(&f, fmt, args);
	*f.p = 0;
	return n;
}

int
snprint(char *s, int len, char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	n = vsnprint(s, len, fmt, args);
	va_end(args);

	return n;
}

char*
seprint(char *s, char *e, char *fmt, ...)
{
	int n;
	va_list args;

	if(e <= s)
		return 0;
	va_start(args, fmt);
	n = vsnprint(s, e-s, fmt, args);
	va_end(args);

	return s+n;
}

int
fdflush(Fmt *f)
{
	int n;

	n = f->p-f->buf;
	if(n && write(*(int*)f->farg, f->buf, n) != n)
		return -1;
	f->p = f->buf;
	return n;
}

int
vfprint(int fd, char *fmt, va_list args)
{
	char buf[256];
	int n;
	Fmt f;

	f.buf = buf;
	f.p = buf;
	f.ep = buf+sizeof buf;
	f.flush = fdflush;
	f.farg = &fd;
	n = vfmtprint(&f, fmt, args);
	if(fdflush(&f) == -1)
		return -1;
	return n;
}

int
fprint(int fd, char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	n = vfprint(fd, fmt, args);
	va_end(args);
	return n;
}

int
print(char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	n = vfprint(1, fmt, args);
	va_end(args);
	return n;
}

static int
put(Fmt *f, int c)
{
	if(f->p == f->ep)
	if(f->flush(f) == -1)
		return -1;
	if(f->p == f->ep)
		return -1;
	*f->p++ = c;
	f->n++;
	return 0;
}

int
vfmtprint(Fmt *f, char *fmt, va_list args)
{
	int sf1, c;

	va_copy(f->args, args);
//	f->args = args;
	f->n = 0;
loop:
	c = *fmt++;
	if(c != '%') {
		if(c == 0 || put(f, c) == -1)
			return f->n;
		goto loop;
	}
	f->f1 = 0;
	f->f2 = -1;
	f->f3 = 0;
	c = *fmt++;
	if (c == ',') {
		f->f3 |= FmtComma;
		c = *fmt++;
	}
	if (c == '0') {
		f->f3 |= FmtZero;
		c = *fmt++;
	}
	if (c == '*') {
		f->f1 = *(int*)va_arg(f->args, int*);
		c = *fmt++;
	} else {
		sf1 = 0;
		if(c == '-') {
			sf1 = 1;
			c = *fmt++;
		}
		while(c >= '0' && c <= '9') {
			f->f1 = f->f1*10 + c-'0';
			c = *fmt++;
		}
		if(sf1)
			f->f1 = -f->f1;
	}
	if (f->f3 & FmtZero)
		f->f2 = f->f1;
	if(c != '.')
		goto l1;
	c = *fmt++;
	if (c == '*') {
		f->f1 = *(int*)va_arg(f->args, int*);
		c = *fmt++;
	} else {
		while(c >= '0' && c <= '9') {
			if(f->f2 < 0)
				f->f2 = 0;
			f->f2 = f->f2*10 + c-'0';
			c = *fmt++;
		}
	}
l1:
	if(c == 0)
		fmt--;
	f->verb = c;
	c = fmtconv[fmtindex[c&0177]](f);
	if(c < 0) {
		f->f3 |= -c;
		c = *fmt++;
		goto l1;
	}
	goto loop;
}

int
fmtprint(Fmt *f, char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	n = vfmtprint(f, fmt, args);
	va_end(args);
	return n;
}

void
strconv(char *o, Fmt *f, int f1, int f2)
{
	int n, c;
	char *p;

	if(o == 0)
		o = "<null>";
	n = strlen(o);
	if(f1 >= 0)
		while(n < f1) {
			if(put(f, ' ') == -1)
				return;
			n++;
		}
	for(p = o; c = *p++;)
		if(f2 != 0) {
			if(put(f, c) == -1)
				return;
			f2--;
		}
	if(f1 < 0) {
		f1 = -f1;
		while(n < f1) {
			if(put(f, ' ') == -1)
				return;
			n++;
		}
	}
}

int
numbconv(Fmt *op, int base)
{
	char b[IDIGIT], d[IDIGIT];
	int i, f, n, c, j, k;
	vlong vl;

	f = 0;
	switch(op->f3 & (FmtLong|FmtShort|FmtUnsigned|FmtVLong)) {
	case FmtLong:
		vl = va_arg(op->args, long);
		break;
	case FmtUnsigned|FmtLong:
		vl = (ulong)va_arg(op->args, ulong);
		break;
	case FmtShort:
		vl = (short)va_arg(op->args, int);
		break;
	case FmtUnsigned|FmtShort:
		vl = (ushort)va_arg(op->args, uint);
		break;
	case FmtVLong:
		vl = (vlong)va_arg(op->args, vlong);
		break;
	case FmtUnsigned|FmtVLong:
		vl = (uvlong)va_arg(op->args, uvlong);
		break;
	default:
		vl = (int)va_arg(op->args, int);
		break;
	case FmtUnsigned:
		vl = (uint)va_arg(op->args, uint);
		break;
	}
	if(!(op->f3 & FmtUnsigned) && vl < 0) {
		vl = -vl;
		f = 1;
	}
	b[IDIGIT-1] = 0;
	for (i = IDIGIT-2;; i--) {
		n = (uvlong)vl % base;
		n += '0';
		if(n > '9')
			n += 'a' - '9'+1;
		b[i] = n;
		if(i < 22) // room for commas
			break;
		vl = (uvlong)vl / base;
		if(op->f2 >= 0 && i >= IDIGIT-op->f2)
			continue;
		if(vl <= 0)
			break;
	}
	if (op->f3 & FmtComma) {
		c = 0;
		for (j = k = IDIGIT-2; k >= i; k--) {
			if (c == 3) {
				d[j--] = ',';
				c = 0;
			}
			c++;
			d[j--] = b[k];
		}
		for (i = IDIGIT-2; i > j; i--)
			b[i] = d[i];
		i++;
	}
	if (f)
		b[--i] = '-';
	else if(op->f3&FmtSign)
		b[--i] = '+';

	strconv(b+i, op, op->f1, -1);
	return 0;
}

static int
noconv(Fmt *f)
{
	fmtprint(f, "%%%c", f->verb);
	return 0;
}

static int
bconv(Fmt *f)
{
	return numbconv(f, 2);
}

static int
cconv(Fmt *f)
{
	char b[2];

	b[0] = (int)va_arg(f->args, int);
	b[1] = 0;
	strconv(b, f, f->f1, -1);
	return 0;
}

static int
dconv(Fmt *f)
{
	return numbconv(f, 10);
}

static int
hconv(Fmt *f)
{
	USED(f);
	return -FmtShort;
}

static int
lconv(Fmt *f)
{
	if (f->f3 & FmtLong) {
		f->f3 &= ~FmtLong;
		return -FmtVLong;
	}
	return -FmtLong;
}

static int
oconv(Fmt *f)
{
	return numbconv(f, 8);
}

static int
pconv(Fmt *f)
{
	f->f1 = 8;
	f->f2 = 8;
	return numbconv(f, 16);
}

static int
rconv(Fmt *f)
{
	char buf[ERRMAX];

	errstr(buf, sizeof buf);
	strconv(buf, f, f->f1, f->f2);
	return 0;
}

static int
sconv(Fmt *f)
{
	strconv((char*)va_arg(f->args, char*), f, f->f1, f->f2);
	return 0;
}

static int
uconv(Fmt *f)
{
	USED(f);
	return -FmtUnsigned;
}

static int
xconv(Fmt *f)
{
	return numbconv(f, 16);
}

static int
percent(Fmt *f)
{
	put(f, '%');
	return 0;
}

static int
enet(Fmt *f)
{
	char *p, buf[32];
	uchar *up;
	int i, n;

	up = (uchar*)va_arg(f->args, uchar*);
	p = buf;
	n = sizeof buf;
	for (i = 0; i < 6; i++, n -= 2)
		p += snprint(p, n, "%2.2x", *up++);
	strconv(buf, f, f->f1, f->f2);
	return 0;
}

static int
sharpconv(Fmt *f)
{
	USED(f);
	return -FmtSharp;
}
