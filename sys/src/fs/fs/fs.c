#include "all.h"
#include "mem.h"
#include "io.h"
#include "ureg.h"

#include "../pc/dosfs.h"

Timet	mktime		= DATE;				/* set by mkfile */
Startsb	startsb[] =
{
	"main",		2,
	0
};

Dos dos;

static struct
{
	char	*name;
	Off	(*read)(int, void*, long, Devsize);
	Off	(*write)(int, void*, long, Devsize);
	int	(*part)(int, char*);
	vlong	(*partoff)(int, char*);
} nvrdevs[] = {
	{ "fd", floppyread,	floppywrite, 0, 0, },
	{ "hd", ataread, atawrite, setatapart, getatapartoff},
//	{ "md", mvsataread,	mvsatawrite,	setmv50part, 0, },
//	{ "sd", scsiread,	scsiwrite,	setscsipart, 0, },
};

void apcinit(void);
int sdinit(void);

void
otherinit(void)
{
	int dev, i, nfd, nhd, s;
	char *p, *q, *part, *v[4], buf[sizeof nvrfile+16];

	kbdinit();
	printcpufreq();
	etherinit();
	scsiinit();
//	apcinit();

	s = spllo();
	nhd = atainit();
	sdinit();			// stupid sleezy.
	nfd = 0; //floppyinit();
	dev = 0;
	part = "disk";
	if(p = getconf("nvr")){
		strncpy(buf, p, sizeof buf-2);
		buf[sizeof buf-1] = 0;
		switch(getfields(buf, v, nelem(v), 0, "!")){
		default:
			panic("malformed nvrfile: %s\n", buf);
		case 4:
			p = v[0];
			dev = strtoul(v[1], 0, 0);
			part = v[2];
			strcpy(nvrfile, v[3]);
			break;
		case 3:
			p = v[0];
			dev = strtoul(v[1], 0, 0);
			part = "disk";
			strcpy(nvrfile, v[2]);
			break;
		}	
	} else
	if(p = getconf("bootfile")){
		strncpy(buf, p, sizeof buf-2);
		buf[sizeof buf-1] = 0;
		p = strchr(buf, '!');
		q = strrchr(buf, '!');
		if(p == 0 || q == 0 || strchr(p+1, '!') != q)
			panic("malformed bootfile: %s\n", buf);
		*p++ = 0;
		*q = 0;
		dev = strtoul(p, 0, 0);
		p = buf;
	} else
	if(nfd)
		p = "fd";
	else
	if(nhd)
		p = "hd";
	else
		p = "sd";

	print("p = [%s]; dev=%d; part=[%s]; nvrfile=[%s]\n", p, dev, part, nvrfile);
	for(i = 0; i < nelem(nvrdevs); i++){
		if(strcmp(p, nvrdevs[i].name) != 0)
			continue;
		dos.dev = dev;
		if(nvrdevs[i].part && !nvrdevs[i].part(dos.dev, "data"))
			continue;
		if(nvrdevs[i].partoff)
			dos.start = nvrdevs[i].partoff(dos.dev, part);
		if(dos.start == -1LL)
			continue;
		dos.read = nvrdevs[i].read;
		dos.write = nvrdevs[i].write;
		break;
	}
	if(dos.read == 0)
		panic("no device for nvram\n");
	if(dosinit(&dos) < 0)
		panic("can't init dos dosfs on %s\n", p);
	splx(s);
}

void
touser(void)
{
	int i;

	settime(rtctime());
	boottime = time();

	print("sysinit\n");
	sysinit();

	userinit(floppyproc, 0, "floppyproc");

	/*
	 * read ahead processes
	 */
	for(i = 0; i < conf.nrahead; i++)
		userinit(rahead, 0, "rah");

	/*
	 * server processes
	 */
	for(i=0; i<conf.nserve; i++)
		userinit(serve, 0, "srv");

	/*
	 * worm "dump" copy process
	 */
	userinit(wormcopy, 0, "wcp");

	/*
	 * processes to read the console
	 */
	consserve();

	/*
	 * "sync" copy process
	 * this doesn't return.
	 */
	u->text = "scp";
	synccopy();
}

void
localconfinit(void)
{
//	conf.nfile = 60000;	/* from emelie */
	conf.nodump = 0;
	conf.dumpreread = 1;
//	conf.idedma = 0; 	/* for old machines */
	conf.firstsb = 0;	/* time- & jukebox-dependent optimisation */
	conf.recovsb = 0;
	conf.ripoff = 1;
	conf.nlgmsg = 1100;	/* @8576 bytes, for packets */
	conf.nsmmsg = 500;	/* @128 bytes */
}
