#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#include "../port/netif.h"
#include "etherif.h"

extern int archether(unsigned ctlno, Ether *ether);

static Ether *etherxx[MaxEther];

Chan*
etherattach(char* spec)
{
	int ctlrno;
	char *p;
	Chan *chan;

	ctlrno = 0;
	if(spec && *spec){
		ctlrno = strtoul(spec, &p, 0);
		if((ctlrno == 0 && p == spec) || *p != 0)
			error(Ebadarg);
		if(ctlrno < 0 || ctlrno >= MaxEther)
			error(Ebadarg);
	}
	if(etherxx[ctlrno] == 0)
		error(Enodev);

	chan = devattach('l', spec);
	if(waserror()){
		chanfree(chan);
		nexterror();
	}
	chan->devno = ctlrno;
	if(etherxx[ctlrno]->attach)
		etherxx[ctlrno]->attach(etherxx[ctlrno]);
	poperror();
	return chan;
}

static Walkqid*
etherwalk(Chan* chan, Chan* nchan, char** name, int nname)
{
	return netifwalk(etherxx[chan->devno], chan, nchan, name, nname);
}

static long
etherstat(Chan* chan, uchar* dp, long n)
{
	return netifstat(etherxx[chan->devno], chan, dp, n);
}

static Chan*
etheropen(Chan* chan, int omode)
{
	return netifopen(etherxx[chan->devno], chan, omode);
}

static void
ethercreate(Chan*, char*, int, int)
{
}

static void
etherclose(Chan* chan)
{
	netifclose(etherxx[chan->devno], chan);
}

static long
etherread(Chan* chan, void* buf, long n, vlong offset)
{
	Ether *ether;

	ether = etherxx[chan->devno];
	if((chan->qid.type & QTDIR) == 0 && ether->ifstat){
		/*
		 * With some controllers it is necessary to reach
		 * into the chip to extract statistics.
		 */
		if(NETTYPE(chan->qid.path) == Nifstatqid)
			return ether->ifstat(ether, buf, n, offset);
		else if(NETTYPE(chan->qid.path) == Nstatqid)
			ether->ifstat(ether, buf, 0, offset);
	}

	return netifread(ether, chan, buf, n, offset);
}

static Block*
etherbread(Chan* chan, long n, vlong offset)
{
	return netifbread(etherxx[chan->devno], chan, n, offset);
}

static long
etherwstat(Chan* chan, uchar* dp, long n)
{
	return netifwstat(etherxx[chan->devno], chan, dp, n);
}

Block*
etheriq(Ether* ether, Block* bp, int fromwire)
{
	Etherpkt *pkt;
	ushort type;
	int len, multi, tome, fromme;
	Netfile **ep, *f, **fp, *fx;
	Block *xbp;

	ether->inpackets++;

	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	type = (pkt->type[0]<<8)|pkt->type[1];
	fx = 0;
	ep = &ether->f[Ntypes];

	multi = pkt->d[0] & 1;
	/* check for valid multicast addresses */
	if(multi && memcmp(pkt->d, ether->bcast, sizeof(pkt->d)) != 0 && ether->prom == 0){
		if(!activemulti(ether, pkt->d, sizeof(pkt->d))){
			if(fromwire){
				freeb(bp);
				bp = 0;
			}
			return bp;
		}
	}

	/* is it for me? */
	tome = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	fromme = memcmp(pkt->s, ether->ea, sizeof(pkt->s)) == 0;

	/*
	 * Multiplex the packet to all the connections which want it.
	 * If the packet is not to be used subsequently (fromwire != 0),
	 * attempt to simply pass it into one of the connections, thereby
	 * saving a copy of the data (usual case hopefully).
	 */
	for(fp = ether->f; fp < ep; fp++){
		if(f = *fp)
		if(f->type == type || f->type < 0)
		if(tome || multi || f->prom || f->bridge & 2){
			/* Don't want to hear bridged packets */
			if(f->bridge && !fromwire && !fromme)
				continue;
			if(fromwire && fx == 0)
				fx = f;
			else if(xbp = iallocb(len)){
				memmove(xbp->wp, pkt, len);
				xbp->wp += len;
				if(qpass(f->iq, xbp) < 0)
					ether->soverflows++;
			}
			else
				ether->soverflows++;
		}
	}

	if(fx){
		if(qpass(fx->iq, bp) < 0)
			ether->soverflows++;
		return 0;
	}
	if(fromwire){
		freeb(bp);
		return 0;
	}

	return bp;
}

static int
etheroq(Ether* ether, Block* bp)
{
	int len, loopback, s;
	Etherpkt *pkt;

	ether->outpackets++;

	/*
	 * Check if the packet has to be placed back onto the input queue,
	 * i.e. if it's a loopback or broadcast packet or the interface is
	 * in promiscuous mode.
	 * If it's a loopback packet indicate to etheriq that the data isn't
	 * needed and return, etheriq will pass-on or free the block.
	 * To enable bridging to work, only packets that were originated
	 * by this interface are fed back.
	 */
	pkt = (Etherpkt*)bp->rp;
	len = BLEN(bp);
	loopback = memcmp(pkt->d, ether->ea, sizeof(pkt->d)) == 0;
	if(loopback || memcmp(pkt->d, ether->bcast, sizeof(pkt->d)) == 0 || ether->prom){
		s = splhi();
		etheriq(ether, bp, 0);
		splx(s);
	}

	if(!loopback){
		qbwrite(ether->oq, bp);
		if(ether->transmit != nil)
			ether->transmit(ether);
	} else
		freeb(bp);

	return len;
}

static long
etherwrite(Chan* chan, void* buf, long n, vlong)
{
	Ether *ether;
	Block *bp;
	int nn, onoff;
	Cmdbuf *cb;

	ether = etherxx[chan->devno];
	if(NETTYPE(chan->qid.path) != Ndataqid) {
		nn = netifwrite(ether, chan, buf, n);
		if(nn >= 0)
			return nn;
		cb = parsecmd(buf, n);
		if(cb->f[0] && strcmp(cb->f[0], "nonblocking") == 0){
			if(cb->nf <= 1)
				onoff = 1;
			else
				onoff = atoi(cb->f[1]);
			qnoblock(ether->oq, onoff);
			free(cb);
			return n;
		}
		free(cb);
		if(ether->ctl!=nil)
			return ether->ctl(ether, buf, n);
			
		error(Ebadctl);
	}

	if(n > ether->mtu)
		error(Etoobig);
	if(n < ether->minmtu)
		error(Etoosmall);

	bp = allocb(n);
	if(waserror()){
		freeb(bp);
		nexterror();
	}
	memmove(bp->rp, buf, n);
	if((ether->f[NETID(chan->qid.path)]->bridge & 2) == 0)
		memmove(bp->rp+Eaddrlen, ether->ea, Eaddrlen);
	poperror();
	bp->wp += n;

	return etheroq(ether, bp);
}

static long
etherbwrite(Chan* chan, Block* bp, vlong)
{
	Ether *ether;
	long n;

	n = BLEN(bp);
	if(NETTYPE(chan->qid.path) != Ndataqid){
		if(waserror()) {
			freeb(bp);
			nexterror();
		}
		n = etherwrite(chan, bp->rp, n, 0);
		poperror();
		freeb(bp);
		return n;
	}
	ether = etherxx[chan->devno];

	if(n > ether->mtu){
		freeb(bp);
		error(Etoobig);
	}
	if(n < ether->minmtu){
		freeb(bp);
		error(Etoosmall);
	}

	return etheroq(ether, bp);
}

static struct {
	char*	type;
	int	(*reset)(Ether*);
} cards[MaxEther+1];

void
addethercard(char* t, int (*r)(Ether*))
{
	static int ncard;

	if(ncard == MaxEther)
		panic("too many ether cards");
	cards[ncard].type = t;
	cards[ncard].reset = r;
	ncard++;
}

int
parseether(uchar *to, char *from)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	for(i = 0; i < Eaddrlen; i++){
		if(*p == 0)
			return -1;
		nip[0] = *p++;
		if(*p == 0)
			return -1;
		nip[1] = *p++;
		nip[2] = 0;
		to[i] = strtoul(nip, 0, 16);
		if(*p == ':')
			p++;
	}
	return 0;
}

static void
etherreset(void)
{
	Ether *ether;
	int i, n, ctlrno;
	char name[KNAMELEN], buf[128];

	for(ether = 0, ctlrno = 0; ctlrno < MaxEther; ctlrno++){
		if(ether == 0)
			ether = malloc(sizeof(Ether));
		memset(ether, 0, sizeof(Ether));
		ether->ctlrno = ctlrno;
		ether->mbps = 10;
		ether->minmtu = ETHERMINTU;
		ether->mtu = ETHERMAXTU;
		ether->maxmtu = ETHERMAXTU;

		if(archether(ctlrno, ether) <= 0)
			continue;

		for(n = 0; cards[n].type; n++){
			if(cistrcmp(cards[n].type, ether->type))
				continue;
			for(i = 0; i < ether->nopt; i++){
				if(cistrncmp(ether->opt[i], "ea=", 3) == 0){
					if(parseether(ether->ea, &ether->opt[i][3]) == -1)
						memset(ether->ea, 0, Eaddrlen);
				}
			}
			if(cards[n].reset(ether))
				break;
			snprint(name, sizeof(name), "ether%d", ctlrno);

			if(ether->interrupt != nil)
				intrenable(Irqlo, ether->irq, ether->interrupt,
					ether, name);

			i = snprint(buf, sizeof buf,
				"#l%d: %s: %dMbps mem %#p irq %d tu %d",
				ctlrno, ether->type, ether->mbps, ether->mem,
				ether->irq, ether->mtu);
			snprint(buf+i, sizeof buf - i, ": %E\n", ether->ea);
			print("%s", buf);

			if(ether->mbps >= 1000)
				netifinit(ether, name, Ntypes, 4*1024*1024);
			else if(ether->mbps >= 100)
				netifinit(ether, name, Ntypes, 1024*1024);
			else
				netifinit(ether, name, Ntypes, 65*1024);
			if(ether->oq == 0)
				ether->oq = qopen(ether->limit, Qmsg, 0, 0);
			if(ether->oq == 0)
				panic("etherreset %s", name);
			ether->alen = Eaddrlen;
			memmove(ether->addr, ether->ea, Eaddrlen);
			memset(ether->bcast, 0xFF, Eaddrlen);

			etherxx[ctlrno] = ether;
			ether = 0;
			break;
		}
	}
	if(ether)
		free(ether);
}

static void
ethershutdown(void)
{
	char name[32];
	int i;
	Ether *ether;

	for(i = 0; i < MaxEther; i++){
		ether = etherxx[i];
		if(ether == nil)
			continue;
		if(ether->shutdown == nil) {
			print("#l%d: no shutdown function\n", i);
			continue;
		}
		snprint(name, sizeof(name), "ether%d", i);
		if(ether->irq >= 0){
		//	intrdisable(ether->irq, ether->interrupt, ether, ether->tbdf, name);
		}
		(*ether->shutdown)(ether);
	}
}

#define POLY 0xedb88320

/* really slow 32 bit crc for ethers */
uint
ethercrc(uchar *p, int len)
{
	int i, j;
	u32int crc, b;

	crc = 0xffffffff;
	for(i = 0; i < len; i++){
		b = *p++;
		for(j = 0; j < 8; j++){
			crc = (crc>>1) ^ (((crc^b) & 1) ? POLY : 0);
			b >>= 1;
		}
	}
	return crc;
}

Dev etherdevtab = {
	'l',
	"ether",

	etherreset,
	devinit,
	ethershutdown,
	etherattach,
	etherwalk,
	etherstat,
	etheropen,
	ethercreate,
	etherclose,
	etherread,
	etherbread,
	etherwrite,
	etherbwrite,
	devremove,
	etherwstat,
};
