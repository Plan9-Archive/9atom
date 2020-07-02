#include <u.h>
#include <goo.h>

int
Bfildes(Biobufhdr *h)
{
	return h->fid;
}

int
Bflush(Biobufhdr *bp)
{
	int n, c;

	switch(bp->state) {
	case Bwactive:
		n = bp->bsize+bp->ocount;
		if(n == 0)
			return 0;
		c = write(bp->fid, bp->bbuf, n);
		if(n == c) {
			bp->offset += n;
			bp->ocount = -bp->bsize;
			return 0;
		}
		bp->state = Binactive;
		bp->ocount = 0;
		break;
	case Bracteof:
		bp->state = Bractive;
	case Bractive:
		bp->icount = 0;
		bp->gbuf = bp->ebuf;
		return 0;
	}
	return Beof;
}

int
Bgetc(Biobufhdr *bp)
{
	int i;

loop:
	i = bp->icount;
	if(i != 0) {
		bp->icount = i+1;
		return bp->ebuf[i];
	}
	if(bp->state != Bractive) {
		if(bp->state == Bracteof)
			bp->state = Bractive;
		return Beof;
	}
	/*
	 * get next buffer, try to keep Bungetsize
	 * characters pre-catenated from the previous
	 * buffer to allow that many ungets.
	 */
	memmove(bp->bbuf-Bungetsize, bp->ebuf-Bungetsize, Bungetsize);
	i = read(bp->fid, bp->bbuf, bp->bsize);
	bp->gbuf = bp->bbuf;
	if(i <= 0) {
		bp->state = Bracteof;
		if(i < 0)
			bp->state = Binactive;
		return Beof;
	}
	if(i < bp->bsize) {
		memmove(bp->ebuf-i-Bungetsize, bp->bbuf-Bungetsize, i+Bungetsize);
		bp->gbuf = bp->ebuf-i;
	}
	bp->icount = -i;
	bp->offset += i;
	goto loop;
}

int
Bungetc(Biobufhdr *bp)
{

	if(bp->state == Bracteof)
		bp->state = Bractive;
	if(bp->state != Bractive)
		return Beof;
	bp->icount--;
	return 1;
}

static	Biobufhdr*	wbufs[20];
static	int		atexitflag;

static
void
batexit(void)
{
	Biobufhdr *bp;
	int i;

	for(i=0; i<nelem(wbufs); i++) {
		bp = wbufs[i];
		if(bp != 0) {
			wbufs[i] = 0;
			Bflush(bp);
		}
	}
}

static
void
deinstall(Biobufhdr *bp)
{
	int i;

	for(i=0; i<nelem(wbufs); i++)
		if(wbufs[i] == bp)
			wbufs[i] = 0;
}

static
void
install(Biobufhdr *bp)
{
	int i;

	deinstall(bp);
	for(i=0; i<nelem(wbufs); i++)
		if(wbufs[i] == 0) {
			wbufs[i] = bp;
			break;
		}
	if(atexitflag == 0) {
		atexitflag = 1;
		atexit(batexit);
	}
}

int
Binits(Biobufhdr *bp, int f, int mode, uchar *p, int size)
{

	p += Bungetsize;	/* make room for Bungets */
	size -= Bungetsize;

	switch(mode&15) {
	default:
		fprint(2, "Binits: unknown mode %d\n", mode);
		return Beof;

	case OREAD:
		bp->state = Bractive;
		bp->ocount = 0;
		break;

	case OWRITE:
		install(bp);
		bp->state = Bwactive;
		bp->ocount = -size;
		break;
	}
	bp->bbuf = p;
	bp->ebuf = p+size;
	bp->bsize = size;
	bp->icount = 0;
	bp->gbuf = bp->ebuf;
	bp->fid = f;
	bp->flag = 0;
	bp->rdline = 0;
	bp->offset = 0;
	bp->runesize = 0;
	return 0;
}


int
Binit(Biobuf *bp, int f, int mode)
{
	return Binits(&bp->h, f, mode, bp->b, sizeof(bp->b));
}

Biobuf*
Bopen(char *name, int mode)
{
	Biobuf *bp;
	int f;

	switch(mode&15) {
	default:
		fprint(2, "Bopen: unknown mode %#x\n", mode);
		return 0;

	case OREAD:
		f = open(name, mode);
		if(f < 0)
			return 0;
		break;

	case OWRITE:
		f = create(name, mode, 0666);
		if(f < 0)
			return 0;
	}
	bp = malloc(sizeof(Biobuf));
	Binits(&bp->h, f, mode, bp->b, sizeof(bp->b));
	bp->h.flag = Bmagic;
	return bp;
}

int
Bterm(Biobufhdr *bp)
{

	deinstall(bp);
	Bflush(bp);
	if(bp->flag == Bmagic) {
		bp->flag = 0;
		close(bp->fid);
		free(bp);
	}
	return 0;
}

static int
fmtBflush(Fmt *f)
{
	Biobufhdr *bp;

	bp = f->farg;
	bp->ocount = f->p-f->ep;
	if(Bflush(bp) < 0)
		return -1;
	f->ep = (char*)bp->ebuf;
	f->buf = f->ep + bp->ocount;
	f->p = f->buf;
	return 0;
}

int
Bvprint(Biobufhdr *bp, char *fmt, va_list args)
{
	int n;
	Fmt f;

	f.ep = (char*)bp->ebuf;
	f.buf = f.ep + bp->ocount;
	f.p = f.buf;
	f.flush = fmtBflush;
	f.farg = bp;
	n = vfmtprint(&f, fmt, args);
	bp->ocount = f.p-f.ep;
	return n;
}

int
Bprint(Biobufhdr *bp, char *fmt, ...)
{
	va_list args;
	int n;

	va_start(args, fmt);
	n = Bvprint(bp, fmt, args);
	va_end(args);
	return n;
}

int
Bputc(Biobufhdr *bp, int c)
{
	int i;

	for(;;) {
		i = bp->ocount;
		if(i) {
			bp->ebuf[i++] = c;
			bp->ocount = i;
			return 0;
		}
		if(Bflush(bp) == Beof)
			break;
	}
	return Beof;
}

long
Bread(Biobufhdr *bp, void *ap, long count)
{
	long c;
	uchar *p;
	int i, n, ic;

	p = ap;
	c = count;
	ic = bp->icount;

	while(c > 0) {
		n = -ic;
		if(n > c)
			n = c;
		if(n == 0) {
			if(bp->state != Bractive)
				break;
			i = read(bp->fid, bp->bbuf, bp->bsize);
			if(i <= 0) {
				bp->state = Bracteof;
				if(i < 0)
					bp->state = Binactive;
				break;
			}
			bp->gbuf = bp->bbuf;
			bp->offset += i;
			if(i < bp->bsize) {
				memmove(bp->ebuf-i, bp->bbuf, i);
				bp->gbuf = bp->ebuf-i;
			}
			ic = -i;
			continue;
		}
		memmove(p, bp->ebuf+ic, n);
		c -= n;
		ic += n;
		p += n;
	}
	bp->icount = ic;
	if(count == c && bp->state == Binactive)
		return -1;
	return count-c;
}

long
Bwrite(Biobufhdr *bp, void *ap, long count)
{
	long c;
	uchar *p;
	int i, n, oc;
	char errbuf[ERRMAX];

	p = ap;
	c = count;
	oc = bp->ocount;

	while(c > 0) {
		n = -oc;
		if(n > c)
			n = c;
		if(n == 0) {
			if(bp->state != Bwactive)
				return Beof;
			i = write(bp->fid, bp->bbuf, bp->bsize);
			if(i != bp->bsize) {
				errstr(errbuf, sizeof errbuf);
				if(strstr(errbuf, "interrupt") == 0)
					bp->state = Binactive;
				errstr(errbuf, sizeof errbuf);
				return Beof;
			}
			bp->offset += i;
			oc = -bp->bsize;
			continue;
		}
		memmove(bp->ebuf+oc, p, n);
		oc += n;
		c -= n;
		p += n;
	}
	bp->ocount = oc;
	return count-c;
}
