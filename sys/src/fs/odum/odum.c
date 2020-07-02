#include "all.h"
#include "io.h"

#include "../pc/dosfs.h"

Timet	mktime		= DATE;
Startsb	startsb[] ={
	"main",		2,
	0
};

Dos dos;
extern void cmd_part(int, char**);
static Device *bootdev;

/* goo because dos takes an int, not a Device*  */
Off
bootread(int, void *buf, long n, Devsize off)
{
	return byteio(bootdev, 0, off, n, buf);
}

Off
bootwrite(int, void *buf, long n, Devsize off)
{
	return byteio(bootdev, 1, off, n, buf);
}

void
otherinit(void)
{
	char *p, buf[NAMELEN], *v[2];
	int s;

	kbdinit();
	printcpufreq();
	etherinit();
//	apcinit();

	s = spllo();
//	floppyinit();		floppy not currently working.

	if(!(p = getconf("nvr")))
		panic("no nvr");
	strncpy(buf, p, sizeof buf-2);
	buf[sizeof buf-1] = 0;
	if(getfields(buf, v, nelem(v), 0, "!") != 2)
		panic("malformed nvr: %s\n", buf);
	strcpy(nvrfile, v[1]);
	if(!(bootdev = devstr(v[0])))
		panic("bad bootdev: %s", v[0]);
	devinit(bootdev);

	print("%Z ! %s\n", bootdev, nvrfile);

	dos.dev = 0;
	dos.read = bootread;
	dos.write = bootwrite;
	if(dosinit(&dos) < 0)
		panic("can't init dos dosfs on %s\n", p);
	cmd_part(0, 0);
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

	cmd_install("e820", "-- print e820 scan results", cmd_e820);

//	userinit(floppyproc, 0, "floppyproc");

	for(i = 0; i < conf.nrahead; i++)
		userinit(rahead, 0, "rah");
	for(i=0; i<conf.nserve; i++)
		userinit(serve, 0, "srv");
	userinit(wormcopy, 0, "wcp");
	userinit(devcopyproc, 0, "devcopy");
	userinit(rawcopyproc, 0, "devcopy");
	consserve();
	cecinit();

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
	conf.nodump = 0;
	conf.ripoff = 1;
	conf.nlgmsg = 2*1100;		/* 8576 bytes, for packets */
	conf.nsmmsg = 2*500;		/* 128 bytes */
//	conf.nserve = 20;
	conf.idedma = 0;
	conf.fastworm = 1;
//	conf.uartonly = 19200;
}
