#include "u.h"
#include "lib.h"
#include "dat.h"
#include "fns.h"
#include "mem.h"

typedef struct {
	uvlong	base;
	uvlong	len;
	ulong	type;
} Emap;

static char *etypes[] =
{
	"type=0",
	"memory",
	"reserved",
	"acpi reclaim",
	"acpi nvs",
};

static int
emapcmp(const void *va, const void *vb)
{
	Emap *a, *b;
	
	a = (Emap*)va;
	b = (Emap*)vb;
	if(a->base < b->base)
		return -1;
	if(a->base > b->base)
		return 1;
	if(a->len < b->len)
		return -1;
	if(a->len > b->len)
		return 1;
	return a->type - b->type;
}

void
e820(void)
{
	ulong i, nt, quiet;
	Emap *e, **p, *tab[maxe820];
	MMap *m;

	i = *(uchar*)e820end;
	print("found %lud e820 entries\n", i);
	if(i > maxe820)
		return;
	quiet = getconf("*e820print") == 0 && i>1;
	e = (Emap*)e820tab;
	for(nt = 0; nt < i; nt++){
		if(e[nt].type == 0)
			break;
		for(p = tab+nt; p > tab && emapcmp(p[-1], e+nt) > 0; p--)
			*p = p[-1];
		*p = e+nt;
	}
	changeconf("e820", 0, "");
	changeconf("*e820", 0, "");
	for(p = tab; p < tab+nt; p++){
		e = *p;

		if(e->type == 1)
			changeconf("e820", 1, "%llux %llux ", e->base, e->base+e->len);
		changeconf("*e820", 1, "%ld %#llux %#llux ", e->type, e->base, e->base+e->len);
		if(!quiet){
			print("e820: %.8llux %.8llux ", e->base, e->base+e->len);
			if(e->type < nelem(etypes))
				print("%s\n", etypes[e->type]);
			else
				print("type=%lud\n", e->type);
		}
		if(e->type == 0)
			break;
		m = mmap + nmmap++;
		m->size = 20;
		m->base[0] = e->base;
		m->base[1] = e->base>>32;
		m->length[0] = e->len;
		m->length[1] = e->len>>32;
		m->type = e->type;
	}
}
