/*
 * The most fundamental constant.
 * The code will not compile with RBUFSIZE made a variable;
 * for one thing, RBUFSIZE determines FEPERBUF, which determines
 * the number of elements in a free-list-block array.
 */
#define RBUFSIZE	(8*1024)	/* raw buffer size */

#include "../port/portdat.h"

extern	Mach	mach0;

typedef struct{
	ulong	d0;
	ulong	d1;
}Segdesc;

typedef struct Mbank {
	ulong	base;
	ulong	limit;
} Mbank;

#define MAXBANK		4

typedef struct Mconf {
	Lock;
	Mbank	bank[MAXBANK];
	int	nbank;
	ulong	topofmem;
} Mconf;
extern Mconf mconf;

extern char nvrfile[128];
