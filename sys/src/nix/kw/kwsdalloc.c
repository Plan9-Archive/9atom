#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/sd.h"

void*
sdmalloc(usize n)
{
	return mallocalign(n, CACHELINESZ, 0, 0);
}

void
sdfree(void *v)
{
	free(v);
}
