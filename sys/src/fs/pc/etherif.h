typedef struct Ether Ether;
typedef struct Etherctlr Etherctlr;

struct Ether {
	ISAConf;			/* hardware info */

	int	ctlrno;
	char	iname[NAMELEN];
	char	oname[NAMELEN];
	int	tbdf;			/* type+busno+devno+funcno */
	int	mbps;			/* Mbps */
	uchar	ea[Easize];

	void	(*attach)(Ether*);		/* filled in by reset routine */
	void	(*transmit)(Ether*);
	void	(*interrupt)(Ureg*, void*);
	void	*ctlr;

	Ifc	ifc;

	Lock	rqlock;
	Msgbuf	*rqhead;
	Msgbuf	*rqtail;
	Rendez	rqr;

	Lock	tqlock;
	Msgbuf	*tqhead;
	Msgbuf	*tqtail;
	Rendez	tqr;
};

struct Etherctlr{
	char	*type;
	int	(*reset)(Ether*);
};

#define NEXT(x, l)	(((x)+1)%(l))
#define PREV(x, l)	(((x) == 0) ? (l)-1: (x)-1)
#define	HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))

extern	Etherctlr	etherctlr[];
extern	int	netherctlr;
extern	Ether	etherif[MaxEther];
extern	int	nether;

void	etheriq(Ether*, Msgbuf*);
Msgbuf	*etheroq(Ether*);

int	etherga620reset(Ether*);
int	ether21140reset(Ether*);
int	etherelnk3reset(Ether*);
int	etheri82557reset(Ether*);
int	igbepnp(Ether*);
int	dp83815reset(Ether*);
int	dp83820pnp(Ether*);
int	rtl8139pnp(Ether*);
int	rtl8169pnp(Ether*);
int	i82563reset(Ether*);
int	m10gpnp(Ether*);
