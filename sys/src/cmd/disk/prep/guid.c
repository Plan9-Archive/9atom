/*
 * efi compliant (type 1) guid generation
 */
#include <u.h>
#include <libc.h>
#include <ip.h>
#include "efi.h"

/*
 * ebase is the number of 100ns intervals between Epoch and
 * midnight, 15. okt 1582, an obvious choice.
 */
enum{
	Ebase	= 0x01B21DD213814000LL,
	Version	= 1,
	Variant	= 4<<1,
	Clockid	= 0,
};

enum{
	Esystem,
	Elegacy,
};

uchar efisystem[] = {
	0xc1, 0x2a, 0x73, 0x28, 0xf8, 0x1f, 0x11, 0xd2, 
	0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b,
};

uchar efilegacy[] = {
	0x02, 0x4d, 0xee, 0x41, 0x33, 0xe7, 0x11, 0xd3, 
	0x9d, 0x69, 0x00, 0x08, 0xc7, 0x81, 0xf3, 0x9f, 
};

static uvlong
efitime(void)
{
	return nsec()/100+Ebase;
}

uchar*
nilgid(uchar *buf)
{
	memset(buf, 0, 16);
	return buf;
}

uchar*
efigid(uchar *u)
{
	uvlong t;

	t = efitime();
	u[0] = t>>24;
	u[1] = t>>16;
	u[2] = t>>8;
	u[3] = t;
	u[4] = t>>40;
	u[5] = t>>32;
	u[6] = Version<<4|t>>56&0xf;
	u[7] = t>>48;
	u[8] = Variant<<4|Clockid>>8&0xf;
	u[9] = Clockid;
	myetheraddr(u+10, "ether0");
	return u;
}

static char dash[] = "123-5-7-9-bcdef0";

int
Ufmt(Fmt *f)
{
	uchar *u;
	char buf[16*2+4+1], *p, *e;
	int i;

	u = va_arg(f->args, uchar*);
	p = buf;
	e = p+sizeof buf;
	for(i = 0; i<16; i++){
		p = seprint(p, e, "%.2ux", u[i]);
		if(dash[i] == '-')
			p = seprint(p, e, "-");
	}
	return fmtstrcpy(f, buf);
}
