/*
 * translated by paul reed from the oberon
 * translation of the linux utility 1280patch,
 * by andrew tipton & christian zietz
 */
#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum{
	Aport	= 0xcf8,
	Rport	= 0xcfc,

	Clock	= 80140,
	Hdisp	= 1280,
	Hstart	= 1343,
	Hend	= 1479,
	Htotal	= 1679,
	Vdisp	= 768,
	Vstart	= 768,
	Vend	= 771,
	Vtotal	= 779,
};

void
patchvesa(void)
{
	ushort *p;

	if(getconf("patchvesa") == 0)
		return;

	outl(Aport, 1<<31);
	if(inl(Rport) != 0x35808086)
		return;

	outl(Aport, 1<<31|0x58);
	outb(Rport+2, 0x33);

	p = (ushort*)(0xc025b+6+28*0);		/* timing table */
	p = KADDR(p);
	*(ulong*)p = Clock;
	p += 2;
	*p++ = Hdisp-1;
	*p++ = Htotal;
	*p++ = Hdisp-1;
	*p++ = Htotal;
	*p++ = Hstart;
	*p++ = Hend;
	*p++ = Vdisp-1;
	*p++ = Vtotal;
	*p++ = Vdisp-1;
	*p++ = Vtotal;
	*p++ = Vstart;
	*p++ = Vend;

	USED(p);

	outl(Aport, 1<<31|0x58);
	outb(Rport+2, 0x11);
}
