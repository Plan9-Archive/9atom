#include <u.h>
#include <goo.h>
#include "snap.h"

int	shelf;

enum{
	Rbsize	= 8192,
};

typedef struct{
	int	fd;
	uchar	buf[Rbsize];
	long	blen;
	uvlong	boff;
}Afd;

Afd atab[3] = {{.fd =-1,}, {.fd = -1,}, {.fd = -1,},};

static int
tailio(Afd *t, long (*io)(int, void*, long, vlong), void *buf, ulong l, uvlong byte)
{
	int r;
	ulong rem;

	rem = byte&(Rbsize-1);
	if(l+rem > Rbsize)
		l = Rbsize-rem;
	if(t->boff == byte)
		r = t->blen;
	else{
		r = pread(t->fd, t->buf, Rbsize, byte-rem);
		if(r < 0){
			t->boff = -1;
			return -1;
		}
		t->boff = byte-rem;
		t->blen = r;
	}
	if(l+rem >= r)
		l = r-rem;
	else if(io == pwrite)
		return -1;

	if(io == pwrite){
		memmove(t->buf+rem, buf, l);
		r = io(t->fd, t->buf, r, byte-rem);
		if(r != t->blen)
			return -1;
	}else
		memmove(buf, t->buf+rem, l);
	return l;
}

static int
byteio(Afd *t, long (*d)(int, void*, long, vlong), char *buf, ulong l, uvlong byte)
{
	int r;
	ulong l0;

	l0 = l;
loop:
	if(l == 0)
		return l0-l;
	if((r = tailio(t, d, buf, l, byte)) < 0)
		return -1;
	byte += r;
	buf += r;
	l -= r;
	if(byte%Rbsize)
		return l0-l;

	while(l >= Rbsize){
		if((r = d(t->fd, buf, Rbsize, byte)) < 0)
			return -1;
		byte += r;
		buf += r;
		l -= r;
		if(r < Rbsize)
			return l0-l;
	}
	goto loop;
}

Afd*
afd(int fd)
{
	int i;

	for(i = 0; i < nelem(atab); i++)
		if(atab[i].fd == fd)
			return atab+i;
	return 0;
}

long
aoepread(int fd, void *buf, long n, vlong offset)
{
	return byteio(afd(fd), pread, buf, n, offset);
}

long
aoepwrite(int fd, void *buf, long n, vlong offset)
{
	return byteio(afd(fd), pwrite, buf, n, offset);
}

void
aoeclose(int fd)
{
	Afd *a;

	a = afd(fd);
	if(a == 0)
		sysfatal("bad aoefd %d", fd);
	close(fd);
	a->fd = -1;
}

int
aoeopen(int slot)
{
	char buf[64];
	Afd *a;

	for(a = atab; a < atab+nelem(atab); a++)
		if(a->fd == -1)
			goto found;
	sysfatal("too many open aoe devices");
found:
#ifdef LINUX
	snprint(buf, sizeof buf, "/dev/etherd/e%d.%d", shelf, slot);
#else
	snprint(buf, sizeof buf, "/dev/aoe/%d.%d/data", shelf, slot);
#endif
	a->fd = open(buf, OREAD);
	a->boff = -1;
	if(a->fd == -1)
		sysfatal("aoeopen[%s]: %r", buf);
	return a->fd;
}
