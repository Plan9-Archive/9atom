#include "all.h"

#define NO 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

void
noream(Device*, int)
{
}

Devtab devtab[] = {
[Devnone] 'n', 0,	NO

[Devmcat] '(', ')',	mcatread, mcatwrite, mcatsize, 0, 0, mwormream, 0, mcatinit, 0, 0,
[Devmlev] '[', ']',	mlevread, mlevwrite, mlevsize, 0, 0, mwormream, 0, mlevinit, 0, 0,
[Devmirr] '{', '}',	mirrread, mirrwrite, mirrsize, 0, 0, mwormream, 0, mirrinit, 0, 0,

[Devcw]	'c', 0,	cwread,	cwwrite, cwsize, cwsaddr, cwraddr, cwream, cwrecover, cwinit, 0, 0,
[Devro]	'o', 0,	roread,	rowrite, cwsize, cwsaddr, cwraddr, 0, 0, roinit, 0, 0,
[Devia] 'a', 0,	iaread,	iawrite, iasize, 0, 0, noream, 0, iainit, 0, 0,
[Devaoe] 'e', 0,	aoeread,	aoewrite, aoesize, 0, 0, noream, 0, aoeinit, 0, 0,
[Devfworm] 'f', 0,	NO	//fwormread, fwormwrite, fwormsize, 0, 0, fwormream, 0, fworminit, 0, 0,
[Devide] 'h', 0,	ideread, idewrite, idesize, 0, 0, noream, 0, ideinit, idesecsize, 0,
[Devjuke] 'j', 0,	NO	//jukeread, jukewrite, jukesize, 0, 0, 	noream, 0, jukeinit, 0, 0, 
[Devlworm] 'l', 0,	NO	//wormread, wormwrite, wormsize, 0,0, noream, 0, jukeinit, 0, 0, 
[Devmv] 'm', 0,	NO	//mvread, mvwrite,	mvsize, 0, 0, noream, 0, mvinit, 0, 0,
[Devpart] 'p', 0,	partread, partwrite, partsize, 0, 0, noream, 0, partinit, 0, 0,
[Devworm] 'r', 0,	NO	//wormread, wormwrite, wormsize, 0,0, noream, 0, jukeinit, 0, 0, 
[Devwren] 'w', 0,	NO	//wrenread, wrenwrite, wrensize, 0, 0, noream, 0, wreninit, 0, 0,
[Devswab] 'x', 0,	NO	//swabread, swabwrite, swabsize, swabsuper, swabraddr, swabream, swabrecover, swabinit, 0, 0,
};
