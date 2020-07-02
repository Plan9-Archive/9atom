/*
 * allocate uncached memory
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

static void*
ucarena(usize size)
{
	void *uv, *v;

	assert(size == 1*MiB);

	if((v = mallocalign(1*MiB, 1*MiB, 0, 0)) == nil){
		return nil;
	}
	if((uv = mmuuncache(v, 1*MiB)) == nil){
		free(v);
		return nil;
	}
	return uv;
}

void
ucfree(void* v)
{
	if(v == nil)
		return;
}

void*
ucalloc(usize size)
{
	assert(size < 1*MiB);
	panic("ucalloc: implement me");
	return nil;
}

void*
ucallocalign(usize size, int align, int span)
{
	assert(size+align+span < 1*MiB);
	panic("ucallocalign: implement me");
	return nil;
}

void *
ucallocz(uint n, int)
{
	char *p;

	p = ucalloc(n);
	if (p)
		memset(p, 0, n);
	else
		panic("ucallocz: out of memory");
	return p;
}

void *
ucsalloc(uint n)
{
	return ucallocz(n, 1);
}
