#include <u.h>
#include <a.out.h>
#include "fns.h"
#include "mem.h"

enum {
	Maxline	= 128,
};

void
memset(void *dst, int v, int n)
{
	uchar *d;

	d = dst;
	while(n > 0){
		*d++ = v;
		n--;
	}
}

void
memmove(void *dst, void *src, int n)
{
	uchar *d, *s;

	d = dst;
	s = src;
	if(d < s){
		while(n-- > 0)
			*d++ = *s++;
	} else if(d > s){
		s += n;
		d += n;
		while(n-- > 0)
			*--d = *--s;
	}
}

int
memcmp(void *src, void *dst, int n)
{
	uchar *d, *s;
	int r;

	d = dst;
	s = src;
	while(n-- > 0)
		if(r = (*d++ - *s++))
			return r;
	return 0;
}

int
strlen(char *s)
{
	char *p;

	for(p = s; *p; p++)
		;
	return p - s;
}

char*
strchr(char *s, int c)
{
	for(; *s; s++)
		if(*s == c)
			return s;
	return 0;
}

void
uartputc(int c)
{
	outb(0x3f8 + 0, c);
	usleep(78);
}

void
putc(int c)
{
	cgaputc(c);
	uartputc(c);
}

int
gotc(void)
{
	uint c;

	if((c = kbdgotc()) >= 1)
		return c;
	c = uartgotc();
	if(c == '\n')
		c = '\r';
	return c;
}

#define	Ctl(c)	((c) - '@')
int
getc(void)
{
	int c;

	for(;;){
		c = gotc();
		if(c <= 0)
			continue;
		if(c == Ctl('P'))
			reboot();
		return c;
	}
}

void
print(char *s)
{
	while(*s){
		if(*s == '\r' && s[1] == '\n')
			s++;
		if(*s == '\n')
			putc('\r');
		putc(*s++);
	}
}

char*
_fmtle(char *buf, uchar *t, int w)
{
	int i, n;

	n = w*2;
	buf[n] = 0;
	for(i = 0; i < w; i++){
		buf[--n] = hex[t[i] & 0xf];
		buf[--n] = hex[t[i]>>4];
	}
	return buf+w*2;
}

char*
fmtle(char *buf, void *v, int w)
{
	return _fmtle(buf, v, w);
}

void
prle(void *v, int w)
{
	char buf[20];
	_fmtle(buf, v, w);
	print(buf);
}

int
readn(void *f, void *data, int len)
{
	uchar *p, *e;

	p = data;
	e = p + len;
	while(p < e){
		if((len = read(f, p, e - p)) <= 0)
			break;
		p += len;
	}

	return p - (uchar*)data;
}

static int
readline(void *f, char buf[Maxline])
{
	char *p;
	static char white[] = "\t ";

	p = buf;
	do{
		if(!f)
			putc('>');
		while(p < buf + Maxline-1){
			if(!f){
				putc(*p = getc());
				if(*p == '\r')
					putc('\n');
				else if(*p == '\b'){
					if(p > buf)
						p--;
					continue;
				}
			}else if(read(f, p, 1) <= 0)
				return 0;
			if(p == buf && strchr(white, *p))
				continue;
			if(strchr(crnl, *p))
				break;
			p++;
		}
		while(p > buf && strchr(white, p[-1]))
			p--;
	}while(p == buf);
	*p = 0;

	return p - buf;
}

static int
timeout(int ms)
{
	while(ms > 0){
		if(gotc())
			return 1;
		usleep(100000);
		ms -= 100;
	}
	return 0;
}

#define BOOTLINE	((char*)CONFADDR)
#define BOOTLINELEN	64
#define BOOTARGS	((char*)(CONFADDR+BOOTLINELEN))
#define	BOOTARGSLEN	(4096-0x200-BOOTLINELEN)

char *confend;

static void e820conf(void);

static char*
getconf(char *s, char *buf)
{
	char *p, *e;
	int n;

	n = strlen(s);
	for(p = BOOTARGS; p < confend; p = e+1){
		for(e = p+1; e < confend; e++)
			if(*e == '\n')
				break;
		if(!memcmp(p, s, n)){
			p += n;
			n = e - p;
			buf[n] = 0;
			memmove(buf, p, n);
			return buf;
		}
	}
	return nil;
}

static int
delconf(char *s)
{
	char *p, *e;

	for(p = BOOTARGS; p < confend; p = e){
		for(e = p+1; e < confend; e++){
			if(*e == '\n'){
				e++;
				break;
			}
		}
		if(!memcmp(p, s, strlen(s))){
			memmove(p, e, confend - e);
			confend -= e - p;
			*confend = 0;
			return 1;
		}
	}
	return 0;
}

char option[] = "optionX";

void
nl(void)
{
	print("\n");
}

void
domenu(char sel[Maxline])
{
	char buf[Maxline];
	char *p, *q;
	int i;

	print("menu:\n");
	for(i = 0; i < 10; i++){
		option[6] = '0'+i;
		p = getconf(option, buf);
		if(p == nil)
			break;
		print(option+6);
		print(". ");
		print(p+1);
		nl();
	}
	if((p = getconf("default", buf)) != nil){
		print("default ");
		print(p+1);
		print("? ");
		if(!timeout(10000)){
			nl();
			memmove(sel, p+1, strlen(p));
			goto clean;
		}
	}
	for(;;){
		print("selection");
		readline(nil, sel);
		option[6] = sel[0];
		q = getconf(option, buf);
		if(q == nil)
			continue;
		memmove(sel, q+1, strlen(q));
		break;
	}
clean:
	option[6] = 0;
	for(i = 0; i < 10; i++)
		delconf(option);
	delconf("default");
}

char*
configure(void *f, char *path)
{
	char line[Maxline], sel[Maxline], *kern, *s, *p;
	int skip, inmenu, nowait, n;

Clear:
	memset(sel, 0, Maxline);
	nowait = 1;
	inmenu = 0;
	skip = 0;

	memset(BOOTLINE, 0, BOOTLINELEN);
	confend = BOOTARGS;
	memset(confend, 0, BOOTARGSLEN);
	e820conf();
	bootdrive();
	confend = typeconf(confend);
	print(BOOTARGS);
Loop:
	while(readline(f, line) > 0){
		if(*line == 0 || strchr("#;=", *line))
			continue;
		if(*line == '['){
			if(inmenu)
				domenu(sel);
			inmenu = memcmp("[menu]", line, 6) == 0;
			skip = !inmenu && memcmp(sel, line+1, strlen(sel)) != 0
				&& memcmp("[common]", line, 8) != 0;
			continue;
		}
		if(skip)
			continue;
		if(!memcmp("boot", line, 5)){
			nowait = 1;
			break;
		}
		if(!memcmp("wait", line, 5)){
			nowait = 0;
			continue;
		}
		if(!memcmp("show", line, 5)){
			*confend = 0;
			print(BOOTARGS);
			continue;
		}
	if(0)	if(!memcmp("clear", line, 5)){
			if(line[5] == 0){
				print("ok\n");
				goto Clear;
			} else if(line[5] == ' ' && delconf(line+6)){
				print("ok\n");
			}
			continue;
		}
		if(memcmp("panic", line, 5) == 0)
			reboot();
		if((p = strchr(line, '=')) == nil)
			continue;
		*p++ = 0;
		delconf(line);

		s = confend;
		memmove(confend, line, n = strlen(line)); confend += n;
		*confend++ = '=';
		memmove(confend, p, n = strlen(p)); confend += n;
		*confend = 0;

		print(s); nl();

		*confend++ = '\n';
		*confend = 0;
	}
	kern = getconf("bootfile=", path);

	if(f){
		close(f);
		f = 0;

		if(kern && (nowait==0 || timeout(1000)))
			goto Loop;
	}

	if(!kern){
		print("no bootfile\n");
		goto Loop;
	}
	while(p = strchr(kern, '!'))
		kern = p+1;

	return kern;
}

void
confappend(char *s, int n)
{
	memmove(confend, s, n);
	confend += n;
	*confend = 0;
}

void
vaddconf(uvlong v, uint base)
{
	char *p, *e, buf[24];

	p = e = buf + sizeof buf - 1;
	for(; v; v >>= 4) // v /= base)	can't use this due to _vasop missing.
		*p-- = hex[(uint)v%base];
	if(p == e)
		*p-- = '0';
	switch(base){
	case 16:
		confappend("0x", 2);
		break;
	}
	confappend(p + 1, e - p);
	confappend(" ", 1);
}

uint e820(uint bx, void *p);

static void
e820conf(void)
{
	uint bx;
	struct {
		uvlong	base;
		uvlong	len;
		uint	type;
		uint	ext;
	} e;

	/* *e820 */
	confappend("*e820=", 6);
	bx = 0;
	do{
		bx = e820(bx, &e);
		vaddconf(e.type, 10);
		vaddconf(e.base, 16);
		vaddconf(e.base+e.len, 16);
	} while(bx);

	*confend++ = '\n';
	*confend = 0;
}

static uvlong border = 0x0001020304050607ull;
uvlong
_getbe(uchar *t, int w)
{
	uint i;
	uvlong r;

	r = 0;
	for(i = 0; i < w; i++)
		r = r<<8 | t[i];
	return r;
}

uvlong
getbe(void *t, int w)
{
	return _getbe(t, w);
}

char*
warp32(Exec ex, void *f)
{
	uchar *e, *d, *t;
	ulong n;

	e = (uchar*)(getbe(&ex.entry, 4) & ~0xF0000000UL);
	t = e;
	n = getbe(&ex.text, 4);

	if(readn(f, t, n) != n)
		goto Error;
	d = (uchar*)PGROUND((ulong)t + n);
	n = getbe(&ex.data, 4);

	if(readn(f, d, n) != n)
		goto Error;
	close(f);
	unload();

	print("warp32\n");

	jump(e);

Error:		
	return "i/o error";
}

char*
warp64(Exec ex, void *f)
{
	uchar *e, *d, *b, *t, ulv[8];
	ulong n;

	if(readn(f, ulv, 8) != 8)
		return "bad header";
	e = (uchar*)(getbe(&ex.entry, 4) & ~0xF0000000UL);
	t = e;
	n = getbe(&ex.text, 4);
	if(readn(f, t, n) != n)
		goto Error;

	d = (uchar*)PGROUND((uint)(t + n));
	n = getbe(&ex.data, 4);
	if(readn(f, d, n) != n)
		goto Error;

	b = (uchar*)PGROUND((uint)(d + n));
	n = getbe(&ex.bss, 4);
	memset(b, 0, n);

	close(f);
	unload();

	print("warp64\n");
	jump(e);

Error:		
	return "i/o error";
}

char*
bootkern(void *f)
{
	Exec ex;
	extern void a20(void);

	a20();

	if(readn(f, &ex, sizeof(ex)) != sizeof(ex))
		return "bad header";

	switch(getbe(&ex.magic, 4)){
	case I_MAGIC:
		return warp32(ex, f);
	case S_MAGIC:
		return warp64(ex, f);
	default:
		return "bad magic";
	}
}
