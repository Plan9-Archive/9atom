#include <u.h>
#include <libc.h>
#include <fis.h>
#include "dat.h"

enum{
	Nop,
	Idall,
	Idpkt,
	Smart,
	Id,
	Sig,
	Dlmc,
	Freez,

	Cmdsz	= 18,
	Replysz	= 18,

};

typedef struct Atatab Atatab;
struct Atatab {
	ushort	cc;
	uchar	protocol;
	char	*name;
};

Atatab atatab[] = {
[Nop]	0x00,	Pnd|P28,	"nop",
[Idall]	0xff,	Pin|Ppio|P28,	"identify * device",
[Idpkt]	0xa1,	Pin|Ppio|P28,	"identify packet device",
[Smart]	0xb0,	Pnd|P28,	"smart",
[Id]	0xec,	Pin|Ppio|P28,	"identify device",
[Sig]	0xf000,	Pnd|P28,	"signature",
[Dlmc]	0x92,	Pout|Ppio|P28,	"download microcode",
[Freez]	0xf5,	Pnd|P28,	"security freeze lock",
};

typedef struct Rcmd Rcmd;
struct Rcmd{
	uchar	sdcmd;		/* sd command; 0xff means ata passthrough */
	uchar	ataproto;	/* ata protocol.  non-data, pio, reset, dd, etc. */
	uchar	fis[Fissize];
};

typedef struct Req Req;
struct Req {
	char	haverfis;
	Rcmd	cmd;
	Rcmd	reply;
	uint	count;
	uchar	*data;
	uchar	xdata[0x200];
};

static int
issueata(Req *r, Sdisk *d, int errok)
{
	char buf[ERRMAX];
	int ok, rv;

	if((rv = write(d->fd, &r->cmd, Cmdsz)) != Cmdsz){
		/* handle non-atazz compatable kernels */
		rerrstr(buf, sizeof buf);
		if(rv != -1 || strstr(buf, "bad arg in system call") != 0)
			eprint(d, "fis write error: %r\n");
		return -1;
	}

	werrstr("");
	switch(r->cmd.ataproto & Pdatam){
	default:
		ok = read(d->fd, "", 0) == 0;
		break;
	case Pin:
		ok = read(d->fd, r->data, r->count) == r->count;
		break;
	case Pout:
		ok = write(d->fd, r->data, r->count) == r->count;
		break;
	}
	rv = 0;
	if(ok == 0){
		rerrstr(buf, sizeof buf);
		if(!errok && strstr(buf, "not sata") == 0)
			eprint(d, "xfer error: %.2ux%.2ux: %r\n", r->cmd.fis[0], r->cmd.fis[2]);
		rv = -1;
	}
	if(read(d->fd, &r->reply, Replysz) != Replysz){
		if(!errok)
			eprint(d, "status fis read error: %r\n");
		return -1;
	}
	r->haverfis = 1;
	return rv;
}

int
issueatat(Req *r, int i, Sdisk *d, int e)
{
	uchar *fis;
	Atatab *a;

	a = atatab + i;
	r->haverfis = 0;
	r->cmd.sdcmd = 0xff;
	r->cmd.ataproto = a->protocol;
	fis = r->cmd.fis;
	fis[0] = H2dev;
	if(a->cc & 0xff00)
		fis[0] = a->cc >> 8;
	fis[1] = Fiscmd;
	if(a->cc != 0xff)
		fis[2] = a->cc;
	return issueata(r, d, e);
}

int
ckdl(Sdisk *d, ushort *id)
{
	uint n;

	if((id16(id, 83) & 1) == 0)
	if((id16(id, 86) & 1) == 0){
		werrstr("no dlmicrocode support");
		return -1;
	}
	d->maxxfr = 1<<16;
	if(id16(id, 110) & id16(id, 120) & 0x10){
		d->dlmode = Xseg;

		n = id16(id, 234);		
		d->maxxfr = n;
		if(n==0 || n==0xffff)
			d->maxxfr = 1;
	}else
		d->dlmode = Xfull;

print("xfermode %d maxxfr %d\n", d->dlmode, d->maxxfr);
	return 0;
}

static void
reqprep(Req *r)
{
	memset(r, 0, sizeof *r);
	r->data = r->xdata;
}

int
ataprobe(Sdisk *d)
{
	int rv;
	Req r;

	reqprep(&r);
	if(issueatat(&r, Sig, d, 1) == -1)
		return -1;
	setfissig(d, fistosig(r.reply.fis));
	reqprep(&r);
	r.count = 0x200;
	identifyfis(d, r.cmd.fis);
	if((rv = issueatat(&r, Idall, d, 1)) != -1){
		idfeat(d, (ushort*)r.data);
		if(ckdl(d, (ushort*)r.data) == -1)
			rv = -1;
	}
	return rv;
}

static void
dlfis(Sfis*, uchar *c, uint nsec, uint dlmode)
{
	skelfis(c);
	c[Ffeat] = dlmode, 
	c[Fsc] = nsec;
	c[Flba0] = nsec>>8;		/* ! 28 bit command with nsec8 in lba0 */
	c[Fcmd] = 0x92;
}

static void
freezefis(Sfis*, uchar *c)
{
	skelfis(c);
	c[Fcmd] = 0xf5;
}

static void
prfis(uchar *c)
{
	int i;

	for(i = 0; i < 18; i++)
		print("%.2ux ", c[i]);
	print("\n");
}

int
atadl(Sdisk *d, char *fw, uint length)
{
	int rv;
	uint nsec;
	Req r;

	nsec = length/512;
	if(nsec > d->maxxfr)
		sysfatal("microcode too big %d > %d", length, d->maxxfr*512);
	if(d->dlmode != Xfull)
		sysfatal("dltype %d not supported", d->dlmode);

	/* freezelock? */
	reqprep(&r);
	freezefis(d, r.cmd.fis);
	rv = issueatat(&r, Freez, d, 0);
	if(rv == -1)
		sysfatal("can't security freeze lock");

	reqprep(&r);
	r.data = (uchar*)fw;
	r.count = length;
	dlfis(d, r.cmd.fis, nsec, d->dlmode);
prfis(r.cmd.fis);
	rv = issueatat(&r, Dlmc, d, 0);
prfis(r.reply.fis);
print("nsec = %.2ux\n", r.reply.fis[Fsc]);
	if(rv == -1)
		sysfatal("dlmc fails\n");

	return rv;
}
