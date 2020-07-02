#include	"all.h"

/* from ../pc/8250.c */
extern int uartcons;

static
struct
{
	Lock;
	uchar	buf[4000];
	uchar	*in;
	uchar	*out;
	int	printing;
} printq;

static
struct
{
	Lock;
	Rendez;
	uchar	buf[500];
	uchar	*in;
	uchar	*out;
	int	reading;
} readq;

int (*consgetc)(void);
void (*consputc)(int);
void (*consputs)(char*, int);

/*
 * Put a string on the console.
 * n bytes of s are guaranteed to fit in the buffer and is ready to print.
 * Must be called splhi() and with printq locked.
 * If early in booting (predawn) poll output.
 * This is the interrupt driven routine used for non- screen-based systems.
 */
static void
puts(char *s, int n)
{

	if(predawn) {
		while(n > 0) {
			(*consputc)(*s++ & 0xFF);
			delay(5);
			n--;
		}
		return;
	}
	if(!printq.printing){
		printq.printing = 1;
		consstart(*s++ & 0xFF);
		n--;
	}
	memmove(printq.in, s, n);
	printq.in += n;
	if(printq.in >= printq.buf+sizeof(printq.buf))
		printq.in = printq.buf;
}

void
printinit(void)
{
	lock(&printq);		/* allocate lock */
	printq.in = printq.buf;
	printq.out = printq.buf;
	unlock(&printq);

	lock(&readq);		/* allocate lock */
	readq.in = readq.buf;
	readq.out = readq.buf;
	unlock(&readq);

	consinit(puts);
}

/*
 * Print a string on the console.  This is the high level routine
 * with a queue to the interrupt handler.  BUG: There is no check against
 * overflow.
 */
void
putstrn(char *str, int n)
{
	int s, m;
	char *t;

	s = splhi();
	lock(&printq);
	while(n > 0){
		if(*str == '\n')
			(*consputs)("\r", 1);
		m = printq.buf+sizeof(printq.buf) - printq.in;
		if(n < m)
			m = n;
		t = memchr(str+1, '\n', m-1);
		if(t)
			if(t-str < m)
				m = t - str;
		(*consputs)(str, m);
		n -= m;
		str += m;
	}
	unlock(&printq);
	splx(s);
}

/*
 * get character to print at interrupt time
 * always called splhi from proc 0
 */
int
conschar(void)
{
	uchar *p;
	int c, s;

	s = splhi();
	lock(&printq);
	p = printq.out;
	if(p == printq.in) {
		printq.printing = 0;
		c = -1;
	} else {
		c = *p++;
		if(p >= printq.buf+sizeof(printq.buf))
			p = printq.buf;
		printq.out = p;
	}
	unlock(&printq);
	splx(s);
	return c;
}

/*
 * dispose of input character at interrupt time
 * always called splhi from proc 0
 */
extern void trapstats(void);
#define Ctl(x)	(x-0100)

void
kbdchar(int c)
{
	int s;
	uchar *p;
	uchar ch;

	switch(c){
	case Ctl('T'):
		dotrace(0);
		return;
	case Ctl('I'):
		trapstats();
		return;
	case Ctl('S'):
		if(u)
			dumpstack(u);
		return;
	}

	s = splhi();
	lock(&readq);
	if(readq.reading)
		goto out;
	if(c == Ctl('U')){
		if(echo)
			putstrn("^U\n", 3);
		readq.in = readq.out;
		goto out;
	}
	if(c == '\r')
		c = '\n';
	ch = c;
	if(echo)
		putstrn((char*)&ch, 1);
	p = readq.in;
	*p++ = c;
	if(p >= readq.buf+sizeof(readq.buf))
		p = readq.buf;
	readq.in = p;
	if(c == '\n' || c == Ctl('D')) {
		readq.reading = 1;
		wakeup(&readq);
	}
out:
	unlock(&readq);
	splx(s);
}

int
rawchar(int seconds)
{
	int c;
	ulong s;

	if(seconds != 0)
		seconds += toytime();

	for(;;) {
		c = consgetc();
		if(c) {
			if(c == '\r')
				c = '\n';
			if(c == '\n') {
				consputc('\r');
				delay(10);
			}
			consputc(c);
			return c;
		}
		if(seconds) {
			/* Allow MACHP(0)->ticks to run */
			s = spllo();
			if(toytime() > seconds) {
				splx(s);
				break;
			}
			splx(s);
		}
		delay(1);
	}
	return 0;
}

int
reading(void*)
{
	return readq.reading;
}


/*
 * get a character from the console
 * this is the high level routine with
 * a queue to the interrupt handler
 */
int
getc(void)
{
	int c, s;
	uchar *p;

	c = 0;

loop:
	sleep(&readq, reading, 0);
	s = splhi();
	lock(&readq);
	p = readq.out;
	if(readq.in == p) {
		readq.reading = 0;
		unlock(&readq);
		splx(s);
		goto loop;
	}
	if(readq.in != p) {
		c = *p++;
		if(p == readq.buf+sizeof(readq.buf))
			p = readq.buf;
		readq.out = p;
	}
	unlock(&readq);
	splx(s);
	return c;
}

int
print(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	va_start(arg, fmt);
	n = vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	if(predawn && conf.uartonly){
		for(int i = 0; i < n; i++)
			uartputc(buf[i]);
	}else
		putstrn(buf, n);
	return n;
}

void
panic(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	lights(Lpanic, 1);
	/* if the only console is vga, conserve it */
	if(uartcons)
		dumpstack(u);
	strcpy(buf, "panic: ");
	va_start(arg, fmt);
	n = vseprint(buf+strlen(buf), buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	buf[n] = '\n';
	putstrn(buf, n+1);
	if(!predawn){
		spllo();
		prflush();
	}
	exit();
}

void
prflush(void)
{
	int i;

	if(predawn || !uartcons)
		return;
	for(i=0; i<50; i++) {
		if(!printq.printing)
			break;
		delay(100);
	}
}
