#include "imap4d.h"

int
fstreecmp(Avl *va, Avl *vb)
{
	int i;
	Fstree *a, *b;

	a = (Fstree*)va;
	b = (Fstree*)vb;
	i = a->m->id - b->m->id;
	if(i > 0)
		i = 1;
	if(i < 0)
		i = -1;
	return i;
}

Msg*
fstreefind(Box *mb, int id)
{
	Msg m0;
	Fstree t, *p;

	memset(&t, 0, sizeof t);
	m0.id = id;
	t.m = &m0;
	if(p = (Fstree*)lookupavl(mb->fstree, &t))
		return p->m;
	return nil;
}

void
fstreeadd(Box *mb, Msg *m)
{
	Avl *old;
	Fstree *p;

	assert(m->id > 0);
	p = ezmalloc(sizeof *p);
	p->m = m;
	insertavl(mb->fstree, p, &old);
	assert(old == 0);
}

void
fstreedelete(Box *mb, Msg *m)
{
	Fstree t, *p;

	memset(&t, 0, sizeof t);
	t.m = m;
	assert(m->id > 0);
	deleteavl(mb->fstree, &t, (Avl**)&p);
	if(p == nil)
		_assert("fstree delete fails");
	free(p);
}
