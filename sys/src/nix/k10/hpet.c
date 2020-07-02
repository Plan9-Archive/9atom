#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "adr.h"

typedef struct Hpet Hpet;
typedef struct Tn Tn;

struct Hpet {					/* Event Timer Block */
	u32int	cap;				/* General Capabilities */
	u32int	period;				/* Main Counter Tick Period */
	u32int	_8_[2];
	u32int	cnf;				/* General Configuration */
	u32int	_20_[3];
	u32int	sts;				/* General Interrupt Status */
	u32int	_36_[51];
	u64int	counter;				/* Main Counter Value */
	u32int	_248[2];
	Tn	tn[];				/* Timers */
};

struct Tn {					/* Timer */
	u32int	cnf;				/* Configuration */
	u32int	cap;				/* Capabilities */
	u64int	comparator;			/* Comparator */
	u32int	val;				/* FSB Interrupt Value */
	u32int	addr;				/* FSB Interrupt Address */
	u32int	_24_[2];
};

static Hpet* etb[8];				/* Event Timer Blocks */

void
hpetinit(uint seqno, uintmem pa, int minticks)
{
	Tn *tn;
	int i, n;
	Hpet *hpet;

	print("hpet: seqno %d pa %#p minticks %d\n", seqno, pa, minticks);
	if(seqno >= nelem(etb))
		return;
	adrmapck(pa, 1024, Ammio, Mfree);
	if((hpet = vmap(pa, 1024)) == nil)
		return;
	etb[seqno] = hpet;

	print("HPET: cap %#8.8ux period %#8.8ux\n", hpet->cap, hpet->period);
	print("HPET: cnf %#8.8ux sts %#8.8ux\n",hpet->cnf, hpet->sts);
	print("HPET: counter %#.16llux\n", hpet->counter);

	n = ((hpet->cap>>8) & 0x0F) + 1;
	for(i = 0; i < n; i++){
		tn = &hpet->tn[i];
		print("Tn%d: cnf %#8.8ux cap %#8.8ux\n", i, tn->cnf, tn->cap);
		print("Tn%d: comparator %#.16llux\n", i, tn->comparator);
		print("Tn%d: val %#8.8ux addr %#8.8ux\n", i, tn->val, tn->addr);
	}

	/*
	 * hpet->period is the number of femtoseconds per counter tick.
	 */
}
