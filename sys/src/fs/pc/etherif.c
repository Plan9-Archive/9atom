#include "all.h"
#include "io.h"
#include "mem.h"

#include "../ip/ip.h"
#include "../dev/aoe.h"
#include "etherif.h"

#define dprint(...)	/* print(__VA_ARGS__) */

Ether etherif[MaxEther];
int nether;

void
etheriq(Ether* ether, Msgbuf* mb)
{
	ilock(&ether->rqlock);
	if(ether->rqhead)
		ether->rqtail->next = mb;
	else
		ether->rqhead = mb;
	ether->rqtail = mb;
	mb->next = 0;
	iunlock(&ether->rqlock);

	wakeup(&ether->rqr);
}

static int
isinput(void* arg)
{
	return ((Ether*)arg)->rqhead != 0;
}

static void
etheri(void)
{
	Ether *ether;
	Ifc *ifc;
	Msgbuf *mb;
	Enpkt *p;

	ether = u->arg;
	ifc = &ether->ifc;
	print("ether%di: %E %I\n", ether->ctlrno, ether->ifc.ea, ether->ifc.ipa);	
	ether->attach(ether);

	for(;;) {
		sleep(&ether->rqr, isinput, ether);

		ilock(&ether->rqlock);
		if(ether->rqhead == 0) {
			iunlock(&ether->rqlock);
			continue;
		}
		mb = ether->rqhead;
		ether->rqhead = mb->next;
		iunlock(&ether->rqlock);

		p = (Enpkt*)mb->data;
		switch(nhgets(p->type)){
		case Arptype:
			arpreceive(p, mb->count, ifc);
			break;
		case Cectype:
			cecreceive(p, mb->count, ifc);
			break;
		case Aoetype:
			aoereceive(p, mb->count, ifc);
			break;
		case Iptype:
			ipreceive(p, mb->count, ifc);
			break;
		default:
			goto done;
		}
		ifc->rxpkt++;
		ifc->work.count++;
		ifc->rate.count += mb->count;
	done:
		mbfree(mb);
	}
}

#ifdef no
static void
ethero(void)
{
	Ether *ether;
	Ifc *ifc;
	Msgbuf *mb;
	int len;

	ether = u->arg;
	ifc = &ether->ifc;
	print("ether%do: %E %I\n", ether->ctlrno, ifc->ea, ifc->ipa);	

	for(;;) {
		mb = recv(ifc->reply, 1);
		if(mb == nil)
			continue;

		len = mb->count;
		if(len > ether->ifc.maxmtu){
			print("ether%do: pkt too big - %d\n", ether->ctlrno, len);
			mbfree(mb);
			continue;
		}
		if(len < ETHERMINTU) {
			memset(mb->data+len, 0, ETHERMINTU-len);
			mb->count = len = ETHERMINTU;
		}
		memmove(((Enpkt*)(mb->data))->s, ifc->ea, sizeof(ifc->ea));

		ilock(&ether->tqlock);
		if(ether->tqhead)
			ether->tqtail->next = mb;
		else
			ether->tqhead = mb;
		ether->tqtail = mb;
		mb->next = 0;
		iunlock(&ether->tqlock);

		ether->transmit(ether);

		ifc->work.count++;
		ifc->rate.count += len;
		ifc->txpkt++;
	}
}

Msgbuf*
etheroq(Ether* ether)
{
	Msgbuf *mb;

	mb = nil;
	ilock(&ether->tqlock);
	if(ether->tqhead){
		mb = ether->tqhead;
		ether->tqhead = mb->next;
	}
	iunlock(&ether->tqlock);

	return mb;
}
#endif

/*
 * look, ma.  no extra queue.
 */
static void
ethero(void)
{
	Ether *e;

	e = u->arg;
	print("ether%do: %E %I\n", e->ctlrno, e->ifc.ea,  e->ifc.ipa);	

	for(;;){
		recv(e->ifc.reply, 0);	// wait for something to do.
		e->transmit(e);
	}
}

Msgbuf*
etheroq(Ether* e)
{
	Msgbuf *m;
	Enpkt *p;
	Ifc *f;
	int len;

	f = &e->ifc;
loop:
	if(f->reply->count == 0)
		return 0;
	m = recv(f->reply, 1);
	len = m->count;
	if(len > f->maxmtu){
		print("ether%do: pkt too big - %d\n", e->ctlrno, len);
		mbfree(m);
		goto loop;
	}
	if(len < ETHERMINTU){
		memset(m->data+len, 0, ETHERMINTU-len);
		m->count = len = ETHERMINTU;
	}
	p = (Enpkt*)m->data;
	memmove(p->s, f->ea, sizeof f->ea);

	f->work.count++;
	f->rate.count += len;
	f->txpkt++;

	return m;
}

static void
cmd_state(int, char*[])
{
	int i;
	Ifc *ifc;

	for(i = 0; i < nether; i++){
		if(etherif[i].mbps == 0)
			continue;

		ifc = &etherif[i].ifc;
		print("ether stats %d %E\n", etherif[i].ctlrno, etherif[i].ea);
		print("	work =%9W pkts\n", &ifc->work);
		print("	rate =%9W Bps\n", &ifc->rate);
		print("	err  =    %3ld rc %3ld sum\n", ifc->rcverr, ifc->sumerr);
	}
}

void
etherstart(void)
{
	int i;
	Ifc *ifc, *tail;
	char buf[100], *p;

	nether = 0;
	tail = 0;
	for(i = 0; i < MaxEther; i++){
		if(etherif[i].mbps == 0)
			continue;

		ifc = &etherif[i].ifc;
		lock(ifc);
		getipa(ifc, etherif[i].ctlrno);
		if(!isvalidip(ifc->ipa)){
			unlock(ifc);
			etherif[i].mbps = 0;
			continue;
		}
		if(ifc->reply == 0){
			dofilter(&ifc->work);
			dofilter(&ifc->rate);
			ifc->reply = newqueue(Nqueue);
		}
		unlock(ifc);

		sprint(etherif[i].oname, "ether%do", etherif[i].ctlrno);
		userinit(ethero, etherif+i, etherif[i].oname);
		sprint(etherif[i].iname, "ether%di", etherif[i].ctlrno);
		userinit(etheri, etherif+i, etherif[i].iname);

		ifc->next = nil;
		if(enets != nil)
			tail->next = ifc;
		else
			enets = ifc;
		tail = ifc;
		nether++;
	}

	if(nether){
		cmd_install("state", "-- ether stats", cmd_state);
		arpstart();
		if(p = getconf("route")){
			snprint(buf, sizeof buf, "route %s", p);
			cmd_exec(buf);
		}
	}
}

static int
parseether(uchar *to, char *from)
{
	char nip[4];
	char *p;
	int i;

	p = from;
	while(*p == ' ')
		++p;
	for(i = 0; i < 6; i++){
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

void
etherinit(void)
{
	Ether *e;
	int i, n, ctlrno;

	for(ctlrno = 0; ctlrno < MaxEther; ctlrno++){
		e = etherif+ctlrno;
		memset(e, 0, sizeof *e);
		if(!isaconfig("ether", ctlrno, e)){
			dprint("%d: !isaconfig\n", ctlrno);
			continue;
		}
		for(n = 0; n < netherctlr; n++){
			if(cistrcmp(etherctlr[n].type, e->type))
				continue;
			dprint("%d: FOUND ether %s\n", ctlrno, etherctlr[n].type);
			e->ctlrno = ctlrno;
			e->tbdf = BUSUNKNOWN;
			e->ifc.maxmtu = ETHERMAXTU;
			for(i = 0; i < e->nopt; i++){
				if(strncmp(e->opt[i], "ea=", 3))
					continue;
				if(parseether(e->ea, &e->opt[i][3]) == -1)
					memset(e->ea, 0, Easize);
			}
			dprint("  reset ... ");
			if(etherctlr[n].reset(e)){
				dprint("fail\n");
				break;
			}
			dprint("okay\n");
			if(e->irq == 2)
				e->irq = 9;
			setvec(Int0vec + e->irq, e->interrupt, e);
			memmove(e->ifc.ea, e->ea, sizeof e->ea);

			print("ether%d: %s: %dMbps port %#p irq %ld mtu %d",
				ctlrno, e->type, e->mbps, e->port, e->irq, e->ifc.maxmtu);
			print(": %E\n", e->ea);
			break;
		}
	}
}
