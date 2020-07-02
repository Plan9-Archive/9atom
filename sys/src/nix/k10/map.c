#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

void*
KADDR(uintmem pa)
{
	u8int* va;

	va = UINT2PTR(pa);
	if(pa < TMFM)
		return KSEG0+va;

	assert(pa < KSEG2);
	return KSEG2+va;
}

uintmem
PADDR(void* va)
{
	uintmem pa;

	pa = PTR2UINT(va);
	if(pa >= KSEG0 && pa < KSEG0+TMFM)
		return pa-KSEG0;
	if(pa > KSEG2)
		return pa-KSEG2;

	panic("PADDR: va %#p pa %#P @ %#p", va, mmuphysaddr(PTR2UINT(va)), getcallerpc(&va));
	return 0;
}

KMap*
kmap(Page* page)
{
	DBG("kmap(%#P) @ %#p: %#p %#p\n",
		page->pa, getcallerpc(&page),
		page->pa, KADDR(page->pa));

	return KADDR(page->pa);
}
