#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/sd.h"

void*
sdmalloc(usize sz)
{
	return malloc(sz);
}

void
sdfree(void *v)
{
	free(v);
}
