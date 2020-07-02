/*
 * The most fundamental constant.
 * The code will not compile with RBUFSIZE made a variable;
 * for one thing, RBUFSIZE determines FEPERBUF, which determines
 * the number of elements in a free-list-block array.
 */
enum{
	RBUFSIZE	= 8*1024,	/* raw buffer size */
	MAXBANK	= 4,		/* discontiguous memory banks. */
};

typedef ulong uintmem;

#include "../port/portdat.h"

typedef struct{
	ulong	d0;
	ulong	d1;
}Segdesc;

typedef struct Mbank {
	ulong	base;
	ulong	limit;
} Mbank;

typedef struct Mconf {
	Lock;
	Mbank	bank[MAXBANK];
	int	nbank;
	ulong	topofmem;
} Mconf;

extern	Mach	mach0;
extern	Mconf	mconf;
extern	char	nvrfile[128];
