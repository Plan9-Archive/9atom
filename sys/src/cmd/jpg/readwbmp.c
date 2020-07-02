#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <ctype.h>
#include "imagefile.h"

static int
Bgetint(Biobufhdr *b)
{
	int c, i;

	i = 0;
	do{
		if((c = Bgetc(b)) == Beof)
			return -1;
		i = i<<7 | (c&0x7f);
	}while(c&0x80);
	return i;
}

static int
Bget00(Biobufhdr *, int)
{
	return 0;
}

static int
Bget11(Biobufhdr *b, int)
{
	int ilen, plen, c;
	char buf[8+16];

	c = Bgetc(b);
	ilen = (c&0x70)>>4;
	plen = c&0xf;

	if(Bread(b, buf, ilen+plen) != ilen+plen)
		return -1;
	return 0;
}

static int
Bgethdr(Biobufhdr *b)
{
	int c;

	do{
		c = Bgetc(b);
		switch((c&0xc0)>>6){
		case 0:
			if(Bget00(b, c&0x3f) < 0)
				return -1;
			break;
		case 3:
			if(Bget11(b, c&0x7f) < 0)
				return -1;
			break;
		default:
			return -1;
		}
	}while(c&0x80);
	return 0;
}

static int
Bexpandbits(Biobuf *b, uchar *p, uint w, uint h)
{
	int i, j, k, n, r, c;

	memset(p, 0, w*h);
	n = w/8;
	r = w%8;

	for(i = 0; i< h; i++){
		for(j = 0; j < n; j++){
			if((c = Bgetc(b)) < 0)
				return -1;
			for(k = 0; k < 8; k++)
				if(c&1<<k)
					p[7-k] = 0xff;
			p += 8;
		}

		if(r){
			if((c = Bgetc(b)) < 0)
				return -1;
			for(k = 0; k < r; k++)
				if(c&1<<7-k)
					p[k] = 0xff;
			p += r;
		}
	}
	return 0;
}

static Rawimage*
Bgetpix(Biobuf *b, Rawimage *a, uint w, uint h)
{
	int sz;
	char *e, buf[ERRMAX];

	//wp = (w+7)&~7;
	sz = w*h;
	a->r = Rect(0, 0, w, h);

	e = "out of memory";
	if((a->chans[0] = malloc(sz)) == 0)
		goto Error;

	a->nchans = 1;
	a->chanlen = sz;
	a->chandesc = CY;

	e = "error reading file";
	if(Bexpandbits(b, a->chans[0], w, h) < 0)
		goto Error;
	return a;

Error:
	errstr(buf, sizeof buf);
	if(buf[0] == 0)
		strcpy(buf, e);
	errstr(buf, sizeof buf);
	free(a->chans[0]);
	return nil;
}

static Rawimage*
Bgetwbmp(Biobuf *b, Rawimage *a)
{
	int w, h;

	if(Bgetint(b) != 0)
		return 0;
	if(Bgethdr(b) < 0)
		return 0;
	w = Bgetint(b);
	h = Bgetint(b);
	if(w < 0 || h < 0)
		return 0;
	//fprint(2, "%d, %d\n", w, h);
	return Bgetpix(b, a, w, h);
}

Rawimage**
readpixmap(int fd, int)
{
	Rawimage **array, *a;
	Biobuf b;
	char buf[ERRMAX];
	int i;
	char *e;

	if(Binit(&b, fd, OREAD) < 0)
		return nil;

	werrstr("");
	e = "out of memory";
	if((array = malloc(sizeof *array)) == nil)
		goto Error;
	if((array[0] = malloc(sizeof *array[0])) == nil)
		goto Error;
	memset(array[0], 0, sizeof *array[0]);

	for(i=0; i<3; i++)
		array[0]->chans[i] = nil;

	e = "bad file format";
	a = Bgetwbmp(&b, array[0]);
	if(a == nil)
		goto Error;
	array[0] = a;

	return array;

Error:
	if(array)
		free(array[0]);
	free(array);

	errstr(buf, sizeof buf);
	if(buf[0] == 0)
		strcpy(buf, e);
	errstr(buf, sizeof buf);

	return nil;
}
