#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "mp.h"
#include "apic.h"
#include "acpi.h"

extern	Madt	*apics;

int
mpacpi(int maxmach)
{
	char *already;
	int np, bp;
	Apic *apic;
	Apicst *st;

	if(apics == nil)
		return maxmach;

	print("apic lapic paddr %#.8P, flags %#.8ux\n",
		apics->lapicpa, apics->pcat);
	np = 0;
	for(st = apics->st; st != nil; st = st->next){
		already = "";
		switch(st->type){
		case ASlapic:
			if(st->lapic.id > Napic || np == maxmach)
				break;
			apic = xlapic + st->lapic.id;
			bp = (np++ == 0);
			if(apic->useable){
				already = "(mp)";
				goto pr;
			}
			lapicinit(st->lapic.id, apics->lapicpa, bp);
		pr:
			USED(already);
			DBG("lapic: mach %d/%d lapicid %d %s\n", np-1, apic->machno, st->lapic.id, already);
			break;
		case ASioapic:
			if(st->ioapic.id > Napic)
				break;
			apic = xioapic + st->ioapic.id;
			if(apic->useable){
				apic->ibase = st->ioapic.ibase;	/* gnarly */
				already = "(mp)";
				goto pr1;
			}
			ioapicinit(st->ioapic.id, st->ioapic.ibase, st->ioapic.addr);
		pr1:
			print("ioapic: %d ", st->ioapic.id);
			print("addr %p base %d %s\n", apic->paddr, apic->ibase, already);
			break;
		}
	}
	return maxmach - np;
}
