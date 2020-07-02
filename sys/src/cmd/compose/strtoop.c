#include "compose.h"
#include <ctype.h>

enum {
	Or	= -1,
	Bad	= ~0,
};

struct {
	char	*s;
	ulong	op;
} tab[] = {
	"Clear",		Clear,
	"SinD",		SinD,
	"DinS",		DinS,
	"SoutD",		SoutD,
	"DoutS",		DoutS,

	"S",		SinD|SoutD,
	"SoverD",	SinD|SoutD|DoutS,
	"SatopD",	SinD|DoutS,
	"SxorD",		SoutD|DoutS,

	"D",		DinS|DoutS,
	"DoverS",	DinS|DoutS|SoutD,
	"DatopS",	DinS|SoutD,
	"DxorS",		DoutS|SoutD,	/* == SxorD */
};

ulong
strtoop(char *s)
{
	char *s0;
	int i, l;
	ulong op;

	op = 0;
	s0 = s;
	for(;;){
		while(*s && isspace(*s))
			s++;
		if((l = strcspn(s, " |")) == 0)
			break;
		for(i = 0; i < nelem(tab); i++)
			if(cistrncmp(tab[i].s, s, l) == 0)
				break;
		if(i == nelem(tab))
			return Bad;
		op |= tab[i].op;
		s += l;
		while(*s && isspace(*s))
			s++;
		if(*s != '|')
			break;
		s++;
	}
	if(*s != 0 || s0 == s)
		return Bad;
	return op;
}
