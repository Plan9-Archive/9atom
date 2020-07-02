#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

void
warp32(ulong entry)
{
	print("warp32(%#lux) %d\n", entry, nmmap);
	impulse();
	/*
	 * This is where to push things on the stack to
	 * boot *BSD systems, e.g.
	(*(void(*)(void*, void*, void*, void*, ulong, ulong))(PADDR(entry)))(0, 0, 0, 0, 8196, 640);
	 * will enable NetBSD boot (the real memory size needs to
	 * go in the 5th argument).
	 */
	(*(void(*)(void))(PADDR(entry)))();
}
