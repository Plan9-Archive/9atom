#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

static	uchar	log2v[256];

void
log2init(void)
{
	int i;

	for(i=2; i<nelem(log2v); i++)
		log2v[i] = log2v[i/2] + 1;
}

/* ceil(log₂ m) */
int
log2ceil(uintmem m)
{
	uint n;
	int r;

	r = (m & (m-1)) != 0;	/* not a power of two => round up */
	n = (uint)m;
	if(sizeof(uintmem)>sizeof(uint) && n != m){
		n = (u64int)m>>32;
		r += 32;
	}
	if((n>>8) == 0)
		return log2v[n] + r;
	if((n>>16) == 0)
		return 8 + log2v[n>>8] + r;
	if((n>>24) == 0)
		return 16 + log2v[n>>16] + r;
	return 24 + log2v[n>>24] + r;
}
