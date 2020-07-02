#include <u.h>
#include <libc.h>
#include "iotrack.h"
#include "dat.h"
#include "fns.h"

void
mlock(MLock *l)
{
	if(l->key != 0 && l->key != 1)
		panic("uninitialized lock");
	if (l->key)
		panic("lock lock %#p unlock %#p", l->lockpc, l->unlockpc);
	l->key = 1;
	l->lockpc = getcallerpc(&l);
}

void
unmlock(MLock *l)
{

	if(l->key != 0 && l->key != 1)
		panic("uninitialized unlock");
	if (!l->key)
		panic("unlock lock %#p unlock %#p", l->lockpc, l->unlockpc);
	l->key = 0;
	l->unlockpc = getcallerpc(&l);
}

int
canmlock(MLock *l)
{
	if(l->key != 0 && l->key != 1)
		panic("uninitialized canlock");
	if (l->key)
		return 0;
	l->key = 1;
	l->lockpc = getcallerpc(&l);
	return 1;
}
