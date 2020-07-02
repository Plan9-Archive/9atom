#include	"all.h"

#ifdef OLD
#define swaboff swab4
#else
#define swaboff swab8
#endif

int
swabread(Device *d, Off b, void *c)
{
	int e;

	e = devread(d->swab.d, b, c);
	if(e == 0)
		swab(c, 0);
	return e;
}

int
swabwrite(Device *d, Off b, void *c)
{
	int e;

	swab(c, 1);
	e = devwrite(d->swab.d, b, c);
	swab(c, 0);
	return e;
}

Devsize
swabsize(Device *d)
{
	return devsize(d->swab.d);
}

Off
swabsuper(Device *d)
{
	return superaddr(d->swab.d);
}

Off
swabraddr(Device *d)
{
	return getraddr(d->swab.d);
}

void
swabream(Device *d, int top)
{
	devream(d->swab.d, top);
}

void
swabrecover(Device *d)
{
	devrecover(d->swab.d);
}

void
swabinit(Device *d)
{
	devinit(d->swab.d);
}

void
swab2(void *c)
{
	uchar *p;
	int t;

	p = c;

	t = p[0];
	p[0] = p[1];
	p[1] = t;
}

void
swab4(void *c)
{
	uchar *p;
	int t;

	p = c;

	t = p[0];
	p[0] = p[3];
	p[3] = t;

	t = p[1];
	p[1] = p[2];
	p[2] = t;
}

void
swab8(void *c)
{
	uchar *p;
	int t;

	p = c;

	t = p[0];
	p[0] = p[7];
	p[7] = t;

	t = p[1];
	p[1] = p[6];
	p[6] = t;

	t = p[2];
	p[2] = p[5];
	p[5] = t;

	t = p[3];
	p[3] = p[4];
	p[4] = t;
}

/*
 * swab a block
 *	flag = 0 -- convert from foreign to native
 *	flag = 1 -- convert from native to foreign
 */
void
swab(void *c, int flag)
{
	uchar *p;
	Tag *t;
	int i, j;
	Dentry *d;
	Cache *h;
	Bucket *b;
	Superb *s;
	Fbuf *f;
	Off *l;

	/* swab the tag */
	p = (uchar*)c;
	t = (Tag*)(p + BUFSIZE);
	if(!flag) {
		swab2(&t->pad);
		swab2(&t->tag);
		swaboff(&t->path);
	}

	/* swab each block type */
	switch(t->tag) {

	default:
		print("no swab for tag=%G rw=%d\n", t->tag, flag);
		for(j=0; j<16; j++)
			print(" %.2x", p[BUFSIZE+j]);
		print("\n");
		for(i=0; i<16; i++) {
			print("%.4x", i*16);
			for(j=0; j<16; j++)
				print(" %.2x", p[i*16+j]);
			print("\n");
		}
		panic("swab");
		break;

	case Tsuper:
		s = (Superb*)p;
		swaboff(&s->fbuf.nfree);
		for(i=0; i<FEPERBUF; i++)
			swaboff(&s->fbuf.free[i]);
#ifdef AUTOSWAB
		swaboff(&s->magic);
#endif
		swaboff(&s->fstart);
		swaboff(&s->fsize);
		swaboff(&s->tfree);
		swaboff(&s->qidgen);
		swaboff(&s->cwraddr);
		swaboff(&s->roraddr);
		swaboff(&s->last);
		swaboff(&s->next);
		break;

	case Tdir:
		for(i=0; i<DIRPERBUF; i++) {
			d = (Dentry*)p + i;
			swab2(&d->uid);
			swab2(&d->gid);
			swab2(&d->mode);
			swab2(&d->muid);
			swaboff(&d->qid.path);
			swab4(&d->qid.version);
			swaboff(&d->size);
			for(j=0; j<NDBLOCK; j++)
				swaboff(&d->dblock[j]);
			for (j = 0; j < NIBLOCK; j++)
				swaboff(&d->iblocks[j]);
			swab4(&d->atime);
			swab4(&d->mtime);
		}
		break;

	case Tind1:
	case Tind2:
#ifndef OLD
	case Tind3:
	case Tind4:
	/* add more Tind tags here ... */
#endif
		l = (Off *)p;
		for(i=0; i<INDPERBUF; i++) {
			swaboff(l);
			l++;
		}
		break;

	case Tfree:
		f = (Fbuf*)p;
		swaboff(&f->nfree);
		for(i=0; i<FEPERBUF; i++)
			swaboff(&f->free[i]);
		break;

	case Tbuck:
		for(i=0; i<BKPERBLK; i++) {
			b = (Bucket*)p + i;
			swab4(&b->agegen);
			for(j=0; j<CEPERBK; j++) {
				swab2(&b->entry[j].age);
				swab2(&b->entry[j].state);
				swaboff(&b->entry[j].waddr);
			}
		}
		break;

	case Tcache:
		h = (Cache*)p;
		swaboff(&h->maddr);
		swaboff(&h->msize);
		swaboff(&h->caddr);
		swaboff(&h->csize);
		swaboff(&h->fsize);
		swaboff(&h->wsize);
		swaboff(&h->wmax);
		swaboff(&h->sbaddr);
		swaboff(&h->cwraddr);
		swaboff(&h->roraddr);
		swab4(&h->toytime);
		swab4(&h->time);
		break;

	case Tnone:	// unitialized
	case Tfile:	// someone elses problem
	case Tvirgo:	// bit map -- all bytes
	case Tconfig:	// configuration string -- all bytes
		break;
	}

	/* swab the tag */
	if(flag) {
		swab2(&t->pad);
		swab2(&t->tag);
		swaboff(&t->path);
	}
}
