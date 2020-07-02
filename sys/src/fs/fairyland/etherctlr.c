#include "all.h"
#include "io.h"
#include "mem.h"

#include "../ip/ip.h"
#include "etherif.h"

extern int etherga620reset(Ether*);
extern int ether21140reset(Ether*);
extern int etherelnk3reset(Ether*);
extern int etheri82557reset(Ether*);
extern int igbepnp(Ether *);
extern int dp83815reset(Ether*);
extern int dp83820pnp(Ether*);
extern int rtl8139pnp(Ether*);
extern int rtl8169pnp(Ether*);
extern int i82563reset(Ether*);
extern int m10gpnp(Ether*);
extern int  i82598pnp(Ether*);

Etherctlr etherctlr[] = {
//	{ "21140",	ether21140reset, },
//	{ "2114x",	ether21140reset, },
//	{ "3C509",	etherelnk3reset, },
//	{ "83815",	dp83815reset, },
//	{ "dp83820",	dp83820pnp, },
//	{ "elnk3",	etherelnk3reset, },
//	{ "ga620",	etherga620reset, },
//	{ "i82557",	etheri82557reset, },
//	{ "igbe",  	igbepnp, },
//	{ "i82543",	igbepnp, },
//	{ "rtl8139",	rtl8139pnp, },
//	{ "rtl8169",	rtl8169pnp, },
	{ "i82563",	i82563reset },
//	{ "m10g",	m10gpnp },
	{ "i82598",	i82598pnp },
};

int	netherctlr	= nelem(etherctlr);
