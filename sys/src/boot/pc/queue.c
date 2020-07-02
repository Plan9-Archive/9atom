#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

int
qgetc(IOQ *q)
{
	int c, x;

	x = splhi();
	if(q->in == q->out){
		splx(x);
		return -1;
	}
	c = *q->out;
	if(q->out == q->buf+sizeof(q->buf)-1)
		q->out = q->buf;
	else
		q->out++;
	splx(x);

	return c;
}

static int
qputc(IOQ *q, int c)
{
	uchar *nextin;
	int x;

	x = splhi();
	if(q->in >= &q->buf[sizeof(q->buf)-1])
		nextin = q->buf;
	else
		nextin = q->in+1;
	if(nextin == q->out){
		splx(x);
		return -1;
	}
	*q->in = c;
	q->in = nextin;
	splx(x);

	return 0;
}

void
qinit(IOQ *q)
{
	q->in = q->out = q->buf;
	q->getc = qgetc;
	q->putc = qputc;
}
