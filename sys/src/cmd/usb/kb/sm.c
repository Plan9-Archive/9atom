#include <u.h>
#include <libc.h>
#include <thread.h>
#include "usb.h"
#include "hid.h"

enum
{
	Awakemsg= 0xdeaddead,
	Diemsg	= 0xbeefbeef,
};

typedef struct KDev KDev;
typedef struct Kin Kin;

struct KDev
{
	Dev*	dev;		/* usb device*/
	Dev*	ep;		/* endpoint to get events */
	HidRepTempl templ;
};

static int
setfirstconfig(KDev *f, int eid, uchar *desc, int descsz)
{
	int nr, r, id, i;

	fprint(2, "setting first config\n");
	if(desc == nil){
		sysfatal("nil first desc");
	}
print("f->dev %d\n", f->dev != nil);
print("f->dev->usb %d\n", f->dev->usb != nil);
print("f->dev->usb->ep[eid]->iface %d\n", f->dev->usb->ep[eid]->iface != nil);

	id = f->dev->usb->ep[eid]->iface->id;
	r = Rh2d | Rstd | Rdev;
	nr =usbcmd(f->dev,  r, Rsetconf, 1, id, nil, 0);
	if(nr < 0)
		return -1;
	r = Rh2d | Rclass | Riface;
	nr=usbcmd(f->dev,   r, Setidle,  0, id, nil, 0);
	if(nr < 0)
		return -1;
	r = Rd2h | Rstd | Riface;
	nr=usbcmd(f->dev,  r, Rgetdesc, Dreport<<8, id, desc, descsz);
	if(nr < 0)
		return -1;
	if(1 && nr > 0){
		fprint(2, "report descriptor: ");
		for(i = 0; i < nr; i++){
			fprint(2, " %#2.2ux ", desc[i]);
			if(i!= 0 && i%8 == 0)
				fprint(2, "\n");
		}
		fprint(2, "\n");
	}
	return nr;
}

int
·parsereportdesc(HidRepTempl *temp, uchar *repdesc, int repsz)
{
	int i, j, l, n, max, isptr, hasxy, hasbut, nk;
	int ks[MaxVals];
	HidInterface *ifs;

	ifs = temp->ifcs;
	isptr = 0;
	hasxy = hasbut = 0;
	n = 0;
	nk = 0;
	memset(ifs, 0, sizeof *ifs * MaxIfc);
	for(i = 0; i < repsz / 2; i += 2){
		if(n == MaxIfc || repdesc[i] == HidEnd)
			break;

		switch(repdesc[i]){
default:
	ks[nk++] = 1773;
	print("unknown %.2ux %.2ux  nk=%d\n", repdesc[i], repdesc[i+1], nk);
	break;
case 0x85:	/* MainMenu */
	print("main menu (0x85)\n");
	ks[nk++] = 1773;
	break;

		case HidTypeUsg:
			switch(repdesc[i+1]){
			case HidX:
				hasxy++;
				ks[nk++] = KindX;
				break;
			case HidY:
				hasxy++;
				ks[nk++] = KindY;
				break;
			case HidWheel:
				ks[nk++] = KindWheel;
				break;
			case HidPtr:
				isptr++;
				break;
			}
			break;
		case HidTypeUsgPg:
			switch(repdesc[i+1]){
			case HidPgButts:
				hasbut++;
				ks[nk++] = KindButtons;
				break;
			}
			break;
		case HidTypeRepSz:
			ifs[n].nbits = repdesc[i+1];
			break;
		case HidTypeCnt:
			ifs[n].count = repdesc[i+1];
			break;
		case HidInput:
print("hidinput nk %d\n", nk);
			for(j = 0; j <nk; j++)
				ifs[n].kind[j] = ks[j];
			max = ifs[n].count;
			if(max > MaxIfc)
				max = MaxIfc;
			if(nk != 0 && nk < ifs[n].count)
				for(l = j; l < max; l++)
{
print("ifs[%d].kind[%d] = ks[%d-1];\n", n, l, j);
					ifs[n].kind[l] = ks[j-1];
}
			n++;
			nk = 0;
			break;
		}
	}
	temp->nifcs = n;
	if(isptr && hasxy && hasbut)
		return 0;
	fprint(2, "bad report: isptr %d, hasxy %d, hasbut %d\n",
		isptr, hasxy, hasbut);
	return -1;
}

static void
start(Dev *d, Ep *ep, KDev *kd)
{
	uchar desc[128];
	int res;

	d->free = nil;
	kd->dev = d;
print("setfirstconfig?\n");
	res = setfirstconfig(kd, ep->id, desc, sizeof desc);
	if(res > 0)
		res = ·parsereportdesc(&kd->templ, desc, sizeof desc);
print("res %d\n", res);
	dumpreport(&kd->templ);

	sysfatal("done");

	kd->ep = openep(d, ep->id);
	if(kd->ep == nil){
		fprint(2, "sm: %s: openep %d: %r\n", d->dir, ep->id);
		return;
	}
	if(opendevdata(kd->ep, OREAD) < 0){
		fprint(2, "sm: %s: opendevdata: %r\n", kd->ep->dir);
		closedev(kd->ep);
		kd->ep = nil;
		return;
	}

	incref(d);
//	proccreate(f, kd, Stack);
}

static int
usage(void)
{
	werrstr("usage: usb/sm [-bdkm] [-a n] [-N nb]");
	return -1;
}

int smdebug;

int
smmain(Dev *d, int argc, char* argv[])
{
	int i;
	Ep *ep;
	KDev *kd;
	Usbdev *ud;

	ARGBEGIN{
	case 'd':
		smdebug++;
		break;
	default:
		return usage();
	}ARGEND;
	if(argc != 0)
		return usage();

	ud = d->usb;
	d->aux = nil;
	ep = nil;
	fprint(2, "sm: main: dev %s ref %ld\n", d->dir, d->ref);
	for(i = 0; i < nelem(ud->ep); i++)
		if((ep = ud->ep[i]) == nil)
			break;
		else if(ep->iface->csp == 3)
			break;
	if(ep && ep->iface->csp != 3)
		return 0;

	d->aux = nil;
fprint(2, "sm: main: dev %s ref %ld\n", d->dir, d->ref);

	kd = d->aux = emallocz(sizeof(KDev), 1);
	start(d, ep, kd);
	return 0;
}

static void
tusage(void)
{
	fprint(2, "usage: %s [dev...]\n", argv0);
	threadexitsall("usage");
}

enum {
	Arglen	= 80,
};

void
threadmain(int argc, char **argv)
{
	char args[Arglen];
	char *as, *ae;
	int csps[] = { 3, 0 };

	quotefmtinstall();
	ae = args+sizeof(args);
	as = seprint(args, ae, "sm");
	ARGBEGIN{
	case 'd':
		usbdebug++;
		as = seprint(as, ae, " -d");
		break;
	default:
		tusage();
	}ARGEND;

	rfork(RFNOTEG);
	fmtinstall('U', Ufmt);
	threadsetgrp(threadid());
	startdevs(args, argv, argc, matchdevcsp, csps, smmain);
	threadexits(nil);
}
