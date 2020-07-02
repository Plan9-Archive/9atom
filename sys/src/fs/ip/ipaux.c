#include "all.h"

#include "../ip/ip.h"

int
chartoea(uchar *ea, char *p)
{
	char buf[3];
	int i;

	buf[2] = 0;
	for(i = 0; i < Easize; i++){
		buf[0] = *p++;
		if(buf[0] == 0)
			return -1;
		buf[1] = *p++;
		if(buf[1] == 0)
			return -1;
		*ea++ = strtoul(buf, 0, 16);
		if(*p == ':')
			p++;
	}
	if(*p != 0)
		return -1;
	return 0;
}

int
chartoip(uchar *pa, char *cp)
{
	int i, c, h;

	for(i=0;;) {
		h = 0;
		for(;;) {
			c = *cp++;
			if(c < '0' || c > '9')
				break;
			h = (h*10) + (c-'0');
		}
		*pa++ = h;
		i++;
		if(i == Pasize) {
			if(c != 0)
				return 1;
			return 0;
		}
		if(c != '.')
			return 1;
	}
}

void
getipa(Ifc *ifc, int a)
{
	memmove(ifc->ipa, ipaddr[a].sysip, Pasize);
	memmove(ifc->netgate, ipaddr[a].defgwip, Pasize);
	ifc->ipaddr = nhgetl(ifc->ipa);
	ifc->mask = nhgetl(ipaddr[a].defmask);
	ifc->cmask = ipclassmask(ifc->ipa);
	ifc->flag = ipaddr[a].flag;
	ifc->idx = a;
}

int
isvalidip(uchar ip[Pasize])
{
	if(ip[0] || ip[1] || ip[2] || ip[3])
		return 1;
	return 0;
}
