#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

MMap mmap[maxe820];
int nmmap;

Mbi multibootheader;

void
mkmultiboot(void)
{
	if(nmmap != 0){
		multibootheader.cmdline = PADDR(BOOTLINE);
		multibootheader.flags |= Fcmdline;
		multibootheader.mmapaddr = PADDR(mmap);
		multibootheader.mmaplength = nmmap*sizeof(MMap);
		multibootheader.flags |= Fmmap;
		if(debug)
			print("&multibootheader %#p\n", &multibootheader);
	}
}
