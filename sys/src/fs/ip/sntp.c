#include "all.h"

#include "../ip/ip.h"

typedef struct Sntppkt {
	uchar	d[Easize];		/* ether header */
	uchar	s[Easize];
	uchar	type[2];

	uchar	vihl;			/* ip header */
	uchar	tos;
	uchar	length[2];
	uchar	id[2];
	uchar	frag[2];
	uchar	ttl;
	uchar	proto;
	uchar	cksum[2];
	uchar	src[Pasize];
	uchar	dst[Pasize];

	uchar	udpsrc[2];		/* Src port */
	uchar	udpdst[2];		/* Dst port */
	uchar	udplen[2];		/* Packet length */
	uchar	udpsum[2];		/* Checksum including header */
	uchar	mode;			/* li:2, vn:3, mode:3 */
	uchar	stratum;			/* level of local clock */
	char	poll;			/* log2(max interval between polls) */
	char	precision;		/* log2(clock precision) -6 => mains, -18 => us */
	uchar	rootdelay[4];		/* round trip delay to reference 16.16 fraction */
	uchar	dispersion[4];		/* max error to reference */
	uchar	clockid[4];		/* reference clock identifier */
	uchar	reftime[8];		/* local time when clock set */
	uchar	orgtime[8];		/* local time when client sent request */
	uchar	rcvtime[8];		/* time request arrived */
	uchar	xmttime[8];		/* time reply sent */
} Sntppkt;

enum {
	Sntpsize = 4 + 3 * 4 + 4 * 8,
	Version = 1,
	Stratum = 0,
	Poll = 0,
	LI = 0,
	Symmetric = 2,
	ClientMode = 3,
	ServerMode = 4,
	Epoch = 86400 * (365 * 70 + 17), /* 1900 to 1970 in seconds */
};

#define dprint(...)	if(cons.flags&sntp.flag)print(__VA_ARGS__);

static struct {
	Lock;
	int	flag;
	int	gotreply;
	int	kicked;
	int	active;
	Rendez	r;
	Rendez	doze;
} sntp;

static int
done(void*)
{
	return sntp.gotreply != 0;
}

static int
kicked(void*)
{
	return sntp.kicked != 0;
}

void
sntprecv(Msgbuf *mb, Ifc*)
{
	int v, li, m;
	Sntppkt *sh;
	Timet now, dt;
	Udppkt *uh;

	uh = (Udppkt*)mb->data;
	dprint("sntp: receive from %I\n", uh->src);
	if(memcmp(uh->src, sntpip, 4) != 0){
		dprint("sntp: wrong IP\n");
		goto overandout;
	}
	if(nhgets(uh->udplen) < Sntpsize){
		dprint("sntp: packet too small\n");
		goto overandout;
	}
	sh = (Sntppkt *)mb->data;
	v = (sh->mode >> 3) & 7;
	li = (sh->mode >> 6);
	m = sh->mode & 7;
	/*
	 * if reply from right place and contains time set gotreply
	 * and wakeup r
	 */
	dprint(	"sntp: LI %d version %d mode %d  stratum %d\n"
		"sntp: poll %d precision %d rootdelay %ld dispersion %ld\n",
		li, v, m, sh->stratum,
		sh->poll, sh->precision, nhgetl(sh->rootdelay), nhgetl(sh->dispersion));
	if(v == 0 || v > 3){
		dprint("sntp: unsupported version\n");
		goto overandout;
	}
	if(m >= 6 || m == ClientMode){
		dprint("sntp: wrong mode\n");
		goto overandout;
	}
	now = nhgetl(sh->xmttime) - Epoch;
	if(li == 3 || now == 0 || sh->stratum == 0){
		print("sntp: time server not synchronized\n");
		goto overandout;
	}
	if(dt = now-time()){
		settime(now);
		setrtc(now);
		if(dt < 0)
			dt = -dt;
		if(dt > 1 || (cons.flags&sntp.flag))
			print("sntp: %T [adjust %+ld]\n", now, (long)dt);
	}
	sntp.gotreply = 1;
	wakeup(&sntp.r);
overandout:
	mbfree(mb);
}

void
sntpsend(void)
{
	uchar tmp[Pasize];
	ulong ip;
	ushort sum;
	Ifc *ifc;
	Msgbuf *mb;
	Sntppkt *s;

	/* find an interface on the same subnet as sntpip, if any */
	ip = nhgetl(sntpip);
	for(ifc = enets; ifc; ifc = ifc->next){
		if(isvalidip(ifc->ipa) &&
		   (nhgetl(ifc->ipa)&ifc->mask) == (ip&ifc->mask))
			break;
	}
	/* if none, find an interface with a default gateway */
	if(ifc == nil)
		for(ifc = enets; ifc; ifc = ifc->next)
			if(isvalidip(ifc->ipa) && isvalidip(ifc->netgate))
				break;
	if(ifc == nil){
		dprint("sntp: can't send to %I; no route\n", sntpip);
		return;
	}

	/* compose a UDP sntp request */
	dprint("sntp: sending to %I on ifc %I\n", sntpip, ifc->ipa);
	mb = mballoc(Ensize+Ipsize+Udpsize+Sntpsize, 0, Mbsntp);
	s = (Sntppkt*)mb->data;
	/* IP fields */	
	memmove(s->src, ifc->ipa, Pasize);
	memmove(s->dst, sntpip, Pasize);
	s->proto = Udpproto;
	s->ttl = 0;
	/* Udp fields */
	hnputs(s->udpsrc, SNTP_LOCAL);
	hnputs(s->udpdst, SNTP);
	hnputs(s->udplen, Sntpsize + Udpsize);
	/* Sntp fields */
	memset(mb->data + Ensize+Ipsize+Udpsize, 0, Sntpsize);
	s->mode = 010 | ClientMode;
	s->poll = 6;
	hnputl(s->orgtime, rtctime() + Epoch);	/* leave 0 fraction */
	/* Compute the UDP sum - form psuedo header */
	hnputs(s->cksum, Udpsize + Sntpsize);
	hnputs(s->udpsum, 0);
	sum = ptclcsum((uchar *)mb->data + Ensize + Ipsize - Udpphsize,
	    Udpsize + Udpphsize + Sntpsize);
	hnputs(s->udpsum, sum);
	/*
	  * now try to send it - cribbed from icmp.c
	  */
	memmove(tmp, s->dst, Pasize);
	if((nhgetl(ifc->ipa)&ifc->mask) != (nhgetl(s->dst)&ifc->mask)){
		dprint("sntp: route via %I\n", ifc->netgate);
		iproute(tmp, s->dst, ifc->netgate);
	}
	ipsend1(mb, ifc, tmp);
}

void
sntptask(void)
{
	int i;

	dprint("sntp: running\n");
	tsleep(&sntp.doze, kicked, 0, 2 * 60 * 1000);
	for(;;){
		sntp.kicked = 0;
		dprint("sntp: poll time\n");
		if(sntp.active && isvalidip(sntpip)){
			sntp.gotreply = 0;
			for (i = 0; i < 3; i++){
				sntpsend();
				tsleep(&sntp.r, done, 0, 1000);
				if(sntp.gotreply)
					break;
			}
			/* clock has been set */
		}
		tsleep(&sntp.doze, kicked, 0, 60 * 60 * 1000);
	}
}

void
cmd_sntp(int argc, char *argv[])
{
	int i;
	uchar tip[Pasize];

	if(argc <= 1){
		print("sntp kick -- check time now\n");
		print("sntp active -- toggle active status\n");
		print("sntp ip -- set sntp ip\n");
		return;
	}
	for(i=1; i<argc; i++){
		if(strcmp(argv[i], "kick") == 0){
		kick:	sntp.active = 1;
			sntp.kicked = 1;
			wakeup(&sntp.doze);
		}else if(strcmp(argv[i], "active") == 0)
			sntp.active ^= 1;
		else if(strcmp(argv[i], "ip") == 0){
			i++;
			if(!argv[i] || chartoip(tip, argv[i])){
				print("bad smtp address\n");
				continue;
			}
			memmove(sntpip, tip, sizeof sntpip);
			goto kick;
		}
	}
	print("active %d\nip %I\n", sntp.active, sntpip);
}

void
sntpinit(void)
{
	cmd_install("sntp", "subcommand -- sntp protocol", cmd_sntp);
	sntp.flag = flag_install("sntp", "-- verbose");
	sntp.active = 1;
	userinit(sntptask, 0, "sntp");
}
