enum
{
	Eaddrlen	= 6,
	ETHERMINTU	= 60,		/* minimum transmit size */
	ETHERMAXTU	= 1514,		/* maximum transmit size */
	ETHERHDRSIZE	= 14,		/* size of an ethernet header */

	MaxEther	= 48,
	Ntypes		= 8,
};

typedef struct Ether Ether;
struct Ether {
	Hwconf;			/* hardware info */
	Netif;

	int	ctlrno;
	uchar	ea[Eaddrlen];

	void	(*attach)(Ether*);	/* filled in by reset routine */
	void	(*detach)(Ether*);
	void	(*transmit)(Ether*);
	void	(*interrupt)(Ureg*, void*);
	long	(*ifstat)(Ether*, void*, long, usize);
	long 	(*ctl)(Ether*, void*, long); /* custom ctl messages */
	void	(*power)(Ether*, int);	/* power on/off */
	void	(*shutdown)(Ether*);	/* shutdown hardware before reboot */
	void	*ctlr;
};

typedef struct Etherpkt Etherpkt;
struct Etherpkt
{
	uchar	d[Eaddrlen];
	uchar	s[Eaddrlen];
	uchar	type[2];
	uchar	data[1500];
};

Block*	etheriq(Ether*, Block*, int);
void	addethercard(char*, int(*)(Ether*));
uint	ethercrc(uchar*, int);
int	parseether(uchar*, char*);

#define NEXT(x, l)	(((x)+1)%(l))
#define PREV(x, l)	(((x) == 0) ? (l)-1: (x)-1)
