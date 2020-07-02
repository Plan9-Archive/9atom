#include <u.h>
#include <libc.h>
#include <mp.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <auth.h>
#include <authsrv.h>
#include <libsec.h>
#include <ip.h>
#include "sshtun.h"

void stend(Srv *);
void server(char *, char *);
void stopen(Req *);
void stlisten1(void *);
void stlisten2(void *);
void stread(Req *);
void readreqrem(void *);
void readdata(void *);
void stwrite(Req *);
void writectl(void *);
void writereqrem(void *);
void writedata(void *);
void stclunk(Fid *);
void stflush(Req *);
void filedup(Req *, File *);
Conn *alloc_conn(void);
SSHChan *alloc_chan(Conn *);
int dohandshake(Conn *, char *);
void send_kexinit(Conn *);
void reader(void *);
int validatekex(Conn *, Packet *);
int validatekexs(Packet *);
int validatekexc(Packet *);
int auth_req(Packet *, Conn *);
int client_auth(Conn *, Ioproc *);
char *factlookup(int, int, char *[]);
void shutdown(Conn *);

Srv sshtunsrv = {
	.open = stopen,
	.read = stread,
	.write = stwrite,
	.flush = stflush,
	.destroyfid = stclunk,
	.end = stend,
};

Cipher *cryptos[] = {
	&cipheraes128,
	&cipheraes192,
	&cipheraes256,
//	&cipherblowfish,
	&cipher3des,
	&cipherrc4,
};

Kex *kexes[] = {
	&dh1sha1,
	&dh14sha1,
};

PKA *pkas[3];

#define Ctl(x)	(1+(x)-'A')

static uchar stty_cooked[] = {
	0x01, 0, 0, 0, 0x7f, 	/* interrupt = DEL */
	0x02, 0, 0, 0, Ctl('Q'), 	/* quit */
	0x03, 0, 0, 0, Ctl('H'), 	/* backspace */
	0x04, 0, 0, 0, Ctl('U'), 	/* line kill */
	0x05, 0, 0, 0, Ctl('D'), 	/* EOF */
	0x35, 0, 0, 0, 0, 		/* echo */
	0x48, 0, 0, 0, 0, 		/* opost */
	0, 			/* end of list */
};

char *macnames[] = {
	"hmac-sha1",
};

char *st_names[] = {
[Empty]	"Empty",
[Allocated]	"Allocated",
[Initting]	"Initting",
[Listening]	"Listening",
[Opening]	"Opening",
[Negotiating]	"Negotiating",
[Authing]	"Authing",
[Established]	"Established",
[Eof]	"Eof",
[Closing]	"Closing",
[Closed]	"Closed",
};

File *rootfile, *clonefile, *ctlfile, *keysfile;
Conn *connections[MAXCONN];
char *mntpt = "/net";
int debug;
int kflag;
int slfd;
char uid[32];
MBox keymbox;
QLock availlck;
Rendez availrend;

void
usage(void)
{
	fprint(2, "usage: sshtun [-d] [-k] [-m mntpt] [-s srvpt]\n");
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	char *srvpt = nil;
	int fd, n;

	slfd = open("/dev/syslog", OWRITE);
	ARGBEGIN {
	case '9':
		chatty9p = 1;
		break;
	case 'd':
		debug++;
		break;
	case 'k':
		kflag = 1;
		break;
	case 'm':
		mntpt = EARGF(usage());
		break;
	case 's':
		srvpt = EARGF(usage());
		break;
	default:
		usage();
		break;
	} ARGEND;

	fd = open("/dev/user", OREAD);
	if (fd < 0)
		strcpy(uid, "none");
	else {
		n = read(fd, uid, 31);
		if (n < 0)
			strcpy(uid, "none");
		else
			uid[n] = '\0';
		close(fd);
	}

	keymbox.mchan = chancreate(4, 0);
	availrend.l = &availlck;
	dh_init(pkas);

	if (rfork(RFNOTEG) < 0)
		fprint(2, "Failed to set process attributes: %r\n");

	/* isolate us from any output pipe, e.g. "ssh fred | conswdir" pipeline */
	close(0);
	close(1);

	server(mntpt, srvpt);
}

Ioproc *io9p;

int
read9pmsg(int fd, void *abuf, uint n)
{
	int m, len;
	uchar *buf;

	if (io9p == nil)
		io9p = ioproc();

	buf = abuf;

	/* read count */
	m = ioreadn(io9p, fd, buf, BIT32SZ);
	if(m != BIT32SZ){
		if(m < 0)
			return -1;
		return 0;
	}

	len = GBIT32(buf);
	if(len <= BIT32SZ || len > n){
		werrstr("bad length in 9P2000 message header");
		return -1;
	}
	len -= BIT32SZ;
	m = ioreadn(io9p, fd, buf+BIT32SZ, len);
	if(m < len)
		return 0;
	return BIT32SZ+m;
}

void
stend(Srv *)
{
	closeioproc(io9p);
	threadkillgrp(threadgetgrp());
}

void
server(char *mntpt, char *srvpt)
{
	Dir d;
	char *p;
	int fd;

	sshtunsrv.tree = alloctree(uid, uid, 0777, nil);
	rootfile = createfile(sshtunsrv.tree->root, "ssh", uid, 0555|DMDIR, (void*)RootFile);
	clonefile = createfile(rootfile, "clone", uid, 0666, (void*)CloneFile);
	ctlfile = createfile(rootfile, "ctl", uid, 0666, (void*)CtlFile);
	keysfile = createfile(rootfile, "keys", uid, 0600, (void *)ReqRemFile);
	threadpostmountsrv(&sshtunsrv, srvpt, mntpt, MAFTER);
	p = smprint("%s/cs", mntpt);
	fd = open(p, OWRITE);
	free(p);
	if (fd >= 0) {
		fprint(fd, "add ssh");
		close(fd);
	}
	if (srvpt) {
		nulldir(&d);
		d.mode = 0666;
		p = smprint("/srv/%s", srvpt);
		dirwstat(p, &d);
		free(p);
	}
}

void
stopen(Req *r)
{
	Conn *c;
	SSHChan *sc;
	char *p;
	int lev, xconn, fnum, fd;
	char buf[10];

	fnum = (uintptr)r->fid->file->aux;
	lev = fnum >> LEVSHIFT;
	switch (fnum & FileMask) {
	case CloneFile:
		switch (lev) {
		case 0:
			p = smprint("%s/tcp/clone", mntpt);
			fd = open(p, ORDWR);
			free(p);
			if (fd < 0) {
				responderror(r);
				return;
			}
			c = alloc_conn();
			if (c == nil) {
				respond(r, "No more connections");
				return;
			}
			c->ctlfd = fd;
			filedup(r, c->ctlfile);
			if (debug)
				fprint(2, "new connection: %d\n", c->id);
			break;
		case 1:
			xconn = (fnum >> CONNSHIFT) & ConnMask;
			c = connections[xconn];
			if (c == nil) {
				respond(r, "Invalid connection");
				return;
			}
			sc = alloc_chan(c);
			if (sc == nil) {
				respond(r, "No more channels");
				return;
			}
			filedup(r, sc->ctl);
			break;
		default:
			snprint(buf, 10, "bad %d", lev);
			readstr(r, buf);
			break;
		}
		respond(r, nil);
		break;
	case ListenFile:
		switch (lev) {
		case 1:
			r->aux = (void *)threadcreate(stlisten1, r, 8192);
			break;
		case 2:
			r->aux = (void *)threadcreate(stlisten2, r, 8192);
			break;
		default:
			respond(r, "not possible");
			break;
		}
		break;
	default:
		respond(r, nil);
		break;
	}
}

void
stlisten1(void *a)
{
	Req *r;
	Conn *c, *cl;
	Ioproc *io;
	char *msg;
	int fnum, xconn, fd, n;
	char buf[10], path[40];

	r = a;
	fnum = (uintptr)r->fid->file->aux;
	xconn = (fnum >> CONNSHIFT) & ConnMask;
	cl = connections[xconn];
	if (cl == nil) {
		respond(r, "Invalid connection");
		threadexits(nil);
	}
	memset(buf, '\0', sizeof(buf));
	io = ioproc();
	seek(cl->ctlfd, 0, 0);
	if ((n = ioread(io, cl->ctlfd, buf, 10)) <= 0)
		fprint(2, "read failed: %r\n");
	buf[n] = '\0';
	cl->state = Listening;
	snprint(path, 40, "%s/tcp/%s/listen", mntpt, buf);
	while (1) {
		fd = ioopen(io, path, ORDWR);
		if (fd < 0) {
			r->aux = 0;
			responderror(r);
			closeioproc(io);
			shutdown(cl);
			threadexits(nil);
		}
		c = alloc_conn();
		if (c)
			break;
		n = ioread(io, fd, buf, 10);
		if (n <= 0) {
			r->aux = 0;
			responderror(r);
			closeioproc(io);
			shutdown(cl);
			threadexits(nil);
		}
		else {
			buf[n] = '\0';
			msg = smprint("reject %s No available connections", buf);
			iowrite(io, fd, msg, strlen(msg));
			free(msg);
		}
		close(fd);
	}
	c->ctlfd = fd;
	filedup(r, c->ctlfile);
	if (debug)
		fprint(2, "**** responding to listen open ***\n");
	r->aux = 0;
	respond(r, nil);
	closeioproc(io);
	threadexits(nil);
}

void
stlisten2(void *a)
{
	Req *r;
	Packet *p2;
	Ioproc *io;
	Conn *c;
	SSHChan *sc;
	int i, n, xconn, fnum;

	r = a;
	fnum = (uintptr)r->fid->file->aux;
	xconn = (fnum >> CONNSHIFT) & ConnMask;
	c = connections[xconn];
	if (c == nil) {
		respond(r, "Invalid connection");
		threadexits(nil);
	}
	if (c->state == Closed || c->state == Closing) {
		r->aux = 0;
		respond(r, "listen on a closed connection");
		threadexits(nil);
	}
	sc = c->chans[fnum & ConnMask];
	qlock(&c->l);
	sc->lreq = r;
	for (i = 0; i < c->nchan; ++i)
		if (c->chans[i] && c->chans[i]->state == Opening && c->chans[i]->ann
				&& strcmp(c->chans[i]->ann, sc->ann) == 0)
			break;
	if (i >= c->nchan) {
		sc->state = Listening;
		rsleep(&sc->r);
		i = sc->waker;
		if (i < 0) {
			qunlock(&c->l);
			r->aux = 0;
			responderror(r);
			threadexits(nil);
		}
	}
	else
		rwakeup(&c->chans[i]->r);
	qunlock(&c->l);
	if (c->state == Closed || c->state == Closing || c->state == Eof) {
		r->aux = 0;
		respond(r, "Listen on a closed connection");
		threadexits(nil);
	}
	c->chans[i]->state = Established;
	p2 = new_packet(c);
	c->chans[i]->rwindow = 32*1024;
	add_byte(p2, SSH_MSG_CHANNEL_OPEN_CONFIRMATION);
	hnputl(p2->payload + 1, c->chans[i]->otherid);
	hnputl(p2->payload + 5, c->chans[i]->id);
	hnputl(p2->payload + 9, 32*1024);
	hnputl(p2->payload + 13, 8192);
	p2->rlength = 18;
	n = finish_packet(p2);
	filedup(r, c->chans[i]->ctl);
	io = ioproc();
	n = iowrite(io, c->datafd, p2->nlength, n);
	free(p2);
	closeioproc(io);
	if (debug)
		fprint(2, "*** Responding to chan listen open ***\n");
	r->aux = 0;
	if (n < 0)
		responderror(r);
	else
		respond(r, nil);
	threadexits(nil);
}

void
getdata(Conn *c, SSHChan *sc, Req *r)
{
	Packet *p;
	Plist *d;
	int n;

	n = r->ifcall.count;
	if (sc->dataq->rem < n)
		n = sc->dataq->rem;
	if (n > 8192)
		n = 8192;
	r->ifcall.offset = 0;
	readbuf(r, sc->dataq->st, n);
	sc->dataq->st += n;
	sc->dataq->rem -= n;
	sc->inrqueue -= n;
	if (sc->dataq->rem <= 0) {
		d = sc->dataq;
		sc->dataq = sc->dataq->next;
		if (d->pack->tlength > sc->rwindow)
			sc->rwindow = 0;
		else
			sc->rwindow -= d->pack->tlength;
		free(d->pack);
		free(d);
	}
	if (sc->rwindow < 16*1024) {
		sc->rwindow += 32*1024;
		if (debug)
			fprint(2, "Increasing receive window to %lud, inq %lud\n", sc->rwindow, sc->inrqueue);
		p = new_packet(c);
		add_byte(p, SSH_MSG_CHANNEL_WINDOW_ADJUST);
		hnputl(p->payload+1, sc->otherid);
		hnputl(p->payload+5, 32*1024);
		p->rlength += 8;
		n = finish_packet(p);
		iowrite(c->dio, c->datafd, p->nlength, n);
		free(p);
	}
	r->aux = 0;
	respond(r, nil);
}

void
stread(Req *r)
{
	Conn *c;
	SSHChan *sc;
	int fd, n, lev,  cnum, xconn, fnum;
	char buf[256], path[40];

	fnum = (uintptr)r->fid->file->aux;
	lev = fnum >> LEVSHIFT;
	xconn = (fnum >> CONNSHIFT) & ConnMask;
	c = connections[xconn];
	if (c == nil) {
		if (lev != 0 || (fnum & FileMask) != ReqRemFile) {
			respond(r, "Invalid connection");
			return;
		}
		cnum = 0;
		sc = nil;
	}
	else {
		cnum = fnum & ConnMask;
		sc = c->chans[cnum];
	}
	switch (fnum & FileMask) {
	case CtlFile:
	case ListenFile:
		if (r->ifcall.offset != 0) {
			respond(r, nil);
			break;
		}
		switch (lev) {
		case 0:
			readstr(r, st_names[c->state]);
			break;
		case 1:
			snprint(buf, 256, "%d", xconn);
			readstr(r, buf);
			break;
		case 2:
			snprint(buf, 256, "%d", cnum);
			readstr(r, buf);
			break;
		default:
			snprint(buf, 256, "Internal error: level %d", lev);
			respond(r, buf);
			return;
			break;
		}
		respond(r, nil);
		break;
	case CloneFile:
		if (r->ifcall.offset != 0) {
			respond(r, nil);
			break;
		}
		readstr(r, "Congratulations, you've achieved the impossible\n");
		respond(r, nil);
		break;
	case DataFile:
		if (lev == 0) {
			respond(r, nil);
			break;
		}
		if (lev == 1) {
			if (c->cap)
				readstr(r, c->cap);
			respond(r, nil);
			break;
		}

		r->aux = (void *)threadcreate(readdata, r, 8192);
		break;
	case LocalFile:
		if (lev == 1) {
			if (c->ctlfd >= 0) {
				n = pread(c->ctlfd, buf, 10, 0);
				buf[n] = '\0';
				snprint(path, 40, "%s/tcp/%s/local", mntpt, buf);
				fd = open(path, OREAD);
				n = pread(fd, buf, 255, 0);
				close(fd);
				buf[n] = '\0';
				readstr(r, buf);
			}
			else
				readstr(r, "::!0\n");
		}
		respond(r, nil);
		break;
	case ReqRemFile:
		r->aux = (void *)threadcreate(readreqrem, r, 8192);
		break;
	case StatusFile:
		switch (lev) {
		case 0:
			readstr(r, "Impossible");
			break;
		case 1:
			if (c->state < 0 || c->state > Closed)
				readstr(r, "Unknown");
			else
				readstr(r, st_names[c->state]);
			break;
		case 2:
			if (sc->state < 0 || sc->state > Closed)
				readstr(r, "Unknown");
			else
				readstr(r, st_names[sc->state]);
			break;
		}
		respond(r, nil);
		break;
	default:
		respond(r, nil);
		break;
	}
}

void
readreqrem(void *a)
{
	Ioproc *io;
	Req *r;
	Conn *c;
	SSHChan *sc;
	int fd, n, lev,  cnum, xconn, fnum;
	char buf[256], path[40];

	r = a;
	fnum = (uintptr)r->fid->file->aux;
	lev = fnum >> LEVSHIFT;
	xconn = (fnum >> CONNSHIFT) & ConnMask;
	c = connections[xconn];
	if (c == nil) {
		if (lev != 0) {
			respond(r, "Invalid connection");
			return;
		}
		sc = nil;
	}
	else {
		cnum = fnum & ConnMask;
		sc = c->chans[cnum];
	}
	switch (lev) {
	case 0:
		if (r->ifcall.offset == 0 && keymbox.state != Empty) {
			r->aux = 0;
			respond(r, "Key file collision");
			break;
		}
		if (r->ifcall.offset != 0) {
			readstr(r, keymbox.msg);
			r->aux = 0;
			respond(r, nil);
			if (r->ifcall.offset + r->ifcall.count >= strlen(keymbox.msg))
				keymbox.state = Empty;
			else
				keymbox.state = Allocated;
			break;
		}
		keymbox.state = Allocated;
		while (1) {
			if (keymbox.msg == nil) {
				if (recv(keymbox.mchan, nil) < 0) {
					r->aux = 0;
					responderror(r);
					keymbox.state = Empty;
					threadexits(nil);
				}
			}
			if (keymbox.state == Empty) {
				break;
			}
			else if (keymbox.state == Allocated) {
				if (keymbox.msg) {
					readstr(r, keymbox.msg);
					if (r->ifcall.offset + r->ifcall.count >= strlen(keymbox.msg)) {
						free(keymbox.msg);
						keymbox.msg = nil;
						keymbox.state = Empty;
					}
				}
				break;
			}
		}
		r->aux = 0;
		respond(r, nil);
		break;
	case 1:
		if (c->ctlfd >= 0) {
			io = ioproc();
			seek(c->ctlfd, 0, 0);
			n = ioread(io, c->ctlfd, buf, 10);
			if (n < 0) {
				r->aux = 0;
				responderror(r);
				closeioproc(io);
				break;
			}
			buf[n] = '\0';
			snprint(path, 40, "%s/tcp/%s/remote", mntpt, buf);
			if ((fd = ioopen(io, path, OREAD)) < 0 || (n = ioread(io, fd, buf, 255)) < 0) {
				r->aux = 0;
				responderror(r);
				if (fd >= 0)
					ioclose(io, fd);
				closeioproc(io);
				break;
			}
			ioclose(io, fd);
			closeioproc(io);
			buf[n] = '\0';
			readstr(r, buf);
		}
		else
			readstr(r, "::!0\n");
		r->aux = 0;
		respond(r, nil);
		break;
	case 2:
		if ((sc->state == Closed || sc->state == Closing || sc->state == Eof) && sc->reqq == nil && sc->dataq == nil) {
			if (debug)
				fprint(2, "Sending EOF1 to channel request listener\n");
			r->aux = 0;
			respond(r, nil);
			break;
		}
		while (sc->reqq == nil) {
			if (recv(sc->reqchan, nil) < 0) {
				r->aux = 0;
				respond(r, "interrupted");
				threadexits(nil);
			}
			if ((sc->state == Closed || sc->state == Closing || sc->state == Eof) && sc->reqq == nil && sc->dataq == nil) {
				if (debug)
					fprint(2, "Sending EOF2 to channel request listener\n");
				r->aux = 0;
				respond(r, nil);
				threadexits(nil);
			}
		}
		n = r->ifcall.count;
		if (sc->reqq->rem < n)
			n = sc->reqq->rem;
		if (n > 8192)
			n = 8192;
		r->ifcall.offset = 0;
		readbuf(r, sc->reqq->st, n);
		sc->reqq->st += n;
		sc->reqq->rem -= n;
		if (sc->reqq->rem <= 0) {
			Plist *d = sc->reqq;
			sc->reqq = sc->reqq->next;
			free(d->pack);
			free(d);
		}
		r->aux = 0;
		respond(r, nil);
		break;
	}
	threadexits(nil);
}

void
readdata(void *a)
{
	Req *r;
	Conn *c;
	SSHChan *sc;
	int cnum, xconn, fnum;

	r = a;
	fnum = (uintptr)r->fid->file->aux;
	xconn = (fnum >> CONNSHIFT) & ConnMask;
	c = connections[xconn];
	if (c == nil) {
		respond(r, "Invalid connection");
		threadexits(nil);
	}
	cnum = fnum & ConnMask;
	sc = c->chans[cnum];
	while (sc->dataq == nil) {
		if (sc->state == Closed || sc->state == Closing || sc->state == Eof) {
			if (debug)
				fprint(2, "Sending EOF2 to channel listener\n");
			r->aux = 0;
			respond(r, nil);
			threadexits(nil);
		}
		if (recv(sc->inchan, nil) < 0) {
			if (debug)
				fprint(2, "Got intterrupt/error in readdata %r\n");
			r->aux = 0;
			respond(r, "interrupted");
			threadexits(nil);
		}
	}
	getdata(c, sc, r);
	threadexits(nil);
}

void
stwrite(Req *r)
{
	Conn *c;
	SSHChan *ch;
	int lev, fnum, xconn;

	fnum = (uintptr)r->fid->file->aux;
	lev = fnum >> LEVSHIFT;
	xconn = (fnum >> CONNSHIFT) & ConnMask;
	c = connections[xconn];
	if (c == nil) {
		respond(r, "Invalid connection");
		return;
	}
	ch = c->chans[fnum & ConnMask];
	switch (fnum & FileMask) {
	case CloneFile:
	case CtlFile:
		r->aux = (void *)threadcreate(writectl, r, 8192);
		break;
	case DataFile:
		r->ofcall.count = r->ifcall.count;
		if (lev < 2) {
			respond(r, nil);
			break;
		}	
		if (c->state == Closed || c->state == Closing || ch->state == Closed || ch->state == Closing) {
			respond(r, nil);
			break;
		}
		r->aux = (void *)threadcreate(writedata, r, 8192);
		break;
	case ReqRemFile:
		r->aux = (void *)threadcreate(writereqrem, r, 8192);
		break;
	default:
		respond(r, nil);
		break;
	}
}

void
writectl(void *a)
{
	Req *r;
	Packet *p;
	Conn *c;
	SSHChan *ch;
	char *q, *buf, *toks[16],*attrs[5];
	int n, ntok, lev, fnum, xconn;
	char path[40], buf2[10];

	r = a;
	fnum = (uintptr)r->fid->file->aux;
	lev = fnum >> LEVSHIFT;
	xconn = (fnum >> CONNSHIFT) & ConnMask;
	c = connections[xconn];
	if (c == nil) {
		respond(r, "Invalid connection");
		threadexits(nil);
	}
	ch = c->chans[fnum & ConnMask];
	if (r->ifcall.count <= 10)
		buf = emalloc9p(11);
	else
		buf = emalloc9p(r->ifcall.count + 1);
	memmove(buf, r->ifcall.data, r->ifcall.count);
	buf[r->ifcall.count] = '\0';
	ntok = tokenize(buf, toks, nelem(toks));
	switch (lev) {
	case 0:
		break;
	case 1:
		if (strcmp(toks[0], "connect") == 0) {
			if (ntok < 2) {
				r->aux = 0;
				free(buf);
				respond(r, "Invalid connect request");
				threadexits(nil);
			}
			memset(buf2, '\0', sizeof(buf2));
			pread(c->ctlfd, buf2, 10, 0);
			fprint(c->ctlfd, "connect %s %s", toks[1], ntok > 3 ? toks[2] : "");
			c->role = Client;
			/* Override the PKA list; we can take any in */
			pkas[0] = &rsa_pka;
			pkas[1] = &dss_pka;
			pkas[2] = nil;
			q = estrdup9p(buf2);
			if (dohandshake(c, q) < 0) {
				r->aux = 0;
				respond(r, "handshake failed");
				free(q);
				free(buf);
				threadexits(nil);
			}
			free(q);
			keymbox.state = Empty;
			nbsendul(keymbox.mchan, 1);
			break;
		}
		if (c->state == Closed || c->state == Closing) {
			r->aux = 0;
			respond(r, "connection closed");
			free(buf);
			threadexits(nil);
		}
		if (strcmp(toks[0], "ssh-userauth") == 0) {
			if (ntok < 3 || ntok > 4) {
				r->aux = 0;
				respond(r, "Invalid connection command");
				free(buf);
				threadexits(nil);
			}
			if (!c->service)
				c->service = estrdup9p(toks[0]);
			if (c->user)
				free(c->user);
			c->user = estrdup9p(toks[2]);
			if (ntok == 4 && strcmp(toks[1], "k") == 0) {
				if (c->authkey) {
					free(c->authkey);
					c->authkey = nil;
				}
				if (c->password)
					free(c->password);
				c->password = estrdup9p(toks[3]);
			}
			else {
				if (c->password) {
					free(c->password);
					c->password = nil;
				}
				attrs[0] = "proto=rsa";
				attrs[1] = "!dk?";
				attrs[2] = smprint("user=%s", c->user);
				attrs[3] = smprint("sys=%s", c->remote);
				if (c->authkey)
					free(c->authkey);
				if (ntok == 3)
					c->authkey = factlookup(4, 2, attrs);
				else {
					attrs[4] = toks[3];
					c->authkey = factlookup(5, 2, attrs);
				}
				free(attrs[2]);
				free(attrs[3]);
			}
			if (!c->password && !c->authkey) {
				r->aux = 0;
				respond(r, "no auth info");
				free(buf);
				threadexits(nil);
			}
			else if (c->state != Authing) {
				p = new_packet(c);
				add_byte(p, SSH_MSG_SERVICE_REQUEST);
				add_string(p, c->service);
				n = finish_packet(p);
				if (c->dio) {
					if (iowrite(c->dio, c->datafd, p->nlength, n) < 0) {
						r->aux = 0;
						responderror(r);
						free(p);
						free(buf);
						threadexits(nil);
					}
				}
				else {
					if (write(c->datafd, p->nlength, n) < 0) {
						r->aux = 0;
						responderror(r);
						free(p);
						free(buf);
						threadexits(nil);
					}
				}
				free(p);
			}
			else {
				if (client_auth(c, c->dio) < 0) {
					r->aux = 0;
					respond(r, "auth failure");
					free(buf);
					threadexits(nil);
				}
			}
			qlock(&c->l);
			if (c->state != Established)
				rsleep(&c->r);
			qunlock(&c->l);
			if (c->state != Established) {
				r->aux = 0;
				respond(r, "Authentication failed");
				free(buf);
				threadexits(nil);
			}
			break;
		}
		else if (strcmp(toks[0], "ssh-connection") == 0) {
		}
		else if (strcmp(toks[0], "hangup") == 0) {
			if (c->rpid >= 0) {
				threadint(c->rpid);
			}
			shutdown(c);
		}
		else if (strcmp(toks[0], "announce") == 0) {
			if (debug)
				print("Got %s argument for announce\n", toks[1]);
			write(c->ctlfd, r->ifcall.data, r->ifcall.count);
		}
		else if (strcmp(toks[0], "accept") == 0) {
			memset(buf2, '\0', sizeof(buf2));
			pread(c->ctlfd, buf2, 10, 0);
			fprint(c->ctlfd, "accept %s", buf2);
			c->role = Server;
			q = estrdup9p(buf2);
			if (dohandshake(c, q) < 0) {
				r->aux = 0;
				respond(r, "handshake failed");
				free(q);
				shutdown(c);
				free(buf);
				threadexits(nil);
			}
			free(q);
		}
		else if (strcmp(toks[0], "reject") == 0) {
			memset(buf2, '\0', sizeof(buf2));
			pread(c->ctlfd, buf2, 10, 0);
			snprint(path, 40, "%s/tcp/%s/data", mntpt, buf2);
			c->datafd = open(path, ORDWR);
			p = new_packet(c);
			add_byte(p, SSH_MSG_DISCONNECT);
			add_byte(p, SSH_DISCONNECT_HOST_NOT_ALLOWED_TO_CONNECT);
			add_string(p, toks[2]);
			add_string(p, "EN");
			n = finish_packet(p);
			if (c->dio && c->datafd >= 0)
				iowrite(c->dio, c->datafd, p->nlength, n);
			free(p);
			if (c->ctlfd >= 0)
				fprint(c->ctlfd, "reject %s %s", buf, toks[2]);
			if (c->rpid >= 0) {
				threadint(c->rpid);
			}
			shutdown(c);
		}
		break;
	case 2:
		if (c->state == Closed || c->state == Closing) {
			r->aux = 0;
			respond(r, "connection closed");
			free(buf);
			threadexits(nil);
		}
		if (strcmp(toks[0], "connect") == 0) {
			p = new_packet(c);
			add_byte(p, SSH_MSG_CHANNEL_OPEN);
			if (ntok > 1)
				add_string(p, toks[1]);
			else
				add_string(p, "session");
			add_uint32(p, ch->id);
			add_uint32(p, 32*1024);
			add_uint32(p, 8192);
			/* more stuff if it's an x11 session */
			n = finish_packet(p);
			iowrite(c->dio, c->datafd, p->nlength, n);
			free(p);
			qlock(&c->l);
			if (ch->otherid == -1)
				rsleep(&ch->r);
			qunlock(&c->l);
			break;
		}
		else if (strcmp(toks[0], "global") == 0) {
		}
		else if (strcmp(toks[0], "hangup") == 0) {
			if (ch->state != Closed && ch->state != Closing) {
				ch->state = Closing;
				if (ch->otherid != -1) {
					p = new_packet(c);
					add_byte(p, SSH_MSG_CHANNEL_CLOSE);
					add_uint32(p, ch->otherid);
					n = finish_packet(p);
					iowrite(c->dio, c->datafd, p->nlength, n);
					free(p);
				}
				qlock(&c->l);
				rwakeup(&ch->r);
				qunlock(&c->l);
				nbsendul(ch->inchan, 1);
				nbsendul(ch->reqchan, 1);
			}
			for (n = 0; n < MAXCONN
				&& (!c->chans[n] || c->chans[n]->state == Empty || c->chans[n]->state == Closing
				|| c->chans[n]->state == Closed); ++n) ;
			if (n >= MAXCONN) {
				if (c->rpid >= 0) {
					threadint(c->rpid);
				}
				shutdown(c);
			}
		}
		else if (strcmp(toks[0], "announce") == 0) {
			if (debug)
				print("Got %s argument for announce\n", toks[1]);
			if (ch->ann)
				free(ch->ann);
			ch->ann = estrdup9p(toks[1]);
		}
		break;
	}
	r->ofcall.count = r->ifcall.count;
	r->aux = 0;
	respond(r, nil);
	free(buf);
	threadexits(nil);
}

void
writereqrem(void *a)
{
	Req *r;
	Packet *p;
	Conn *c;
	SSHChan *ch;
	char *q, *buf, *toks[16];
	int n, ntok, lev, fnum, xconn;
	char *cmd;

	r = a;
	fnum = (uintptr)r->fid->file->aux;
	lev = fnum >> LEVSHIFT;
	xconn = (fnum >> CONNSHIFT) & ConnMask;
	c = connections[xconn];
	if (c == nil) {
		respond(r, "Invalid connection");
		threadexits(nil);
	}
	ch = c->chans[fnum & ConnMask];
	if (r->ifcall.count <= 10)
		buf = emalloc9p(11);
	else
		buf = emalloc9p(r->ifcall.count + 1);
	memmove(buf, r->ifcall.data, r->ifcall.count);
	buf[r->ifcall.count] = '\0';
	ntok = tokenize(buf, toks, nelem(toks));

	if (lev == 0) {
		if (keymbox.msg)
			free(keymbox.msg);
		keymbox.msg = buf;
		nbsendul(keymbox.mchan, 1);
		r->ofcall.count = r->ifcall.count;
		r->aux = 0;
		respond(r,nil);
		threadexits(nil);
	}
		
	r->ofcall.count = r->ifcall.count;
	if (c->state == Closed || c->state == Closing || ch->state == Closed || ch->state == Closing) {
		r->aux = 0;
		respond(r, nil);
		free(buf);
		threadexits(nil);
	}
	p = new_packet(c);
	if (strcmp(toks[0], "success") == 0) {
		add_byte(p, SSH_MSG_CHANNEL_SUCCESS);
		add_uint32(p, ch->otherid);
	}
	else if (strcmp(toks[0], "failure") == 0) {
		add_byte(p, SSH_MSG_CHANNEL_FAILURE);
		add_uint32(p, ch->otherid);
	}
	else if (strcmp(toks[0], "close") == 0) {
		ch->state = Closing;
		add_byte(p, SSH_MSG_CHANNEL_CLOSE);
		add_uint32(p, ch->otherid);
	}
	else if (strcmp(toks[0], "shell") == 0) {
		ch->state = Established;
		/*
		 * Some servers *cough*OpenSSH*cough* don't seem to be able
		 * to intelligently handle a shell with no pty.
		 */
		add_byte(p, SSH_MSG_CHANNEL_REQUEST);
		add_uint32(p, ch->otherid);
		add_string(p, "pty-req");
		add_byte(p, 0);
		if (ntok == 1)
			add_string(p, "dumb");
		else
			add_string(p, toks[1]);
		if(ntok == 7){
			add_uint32(p, atoi(toks[2]));	/* width, chars */
			add_uint32(p, atoi(toks[3]));	/* height, chars */
			add_uint32(p, atoi(toks[4]));	/* width, pixels */
			add_uint32(p, atoi(toks[5]));	/* height, pixels */
			if(atoi(toks[6]))		/* is cooked */
				add_block(p, stty_cooked, sizeof(stty_cooked));
			else
				add_string(p, "");
		}
		else{
			add_uint32(p, 0);			/* width, chars */
			add_uint32(p, 0);			/* height, chars */
			add_uint32(p, 0);			/* width, pixels */
			add_uint32(p, 0);			/* height, pixels */
			add_string(p, "");			/* stty opts */
		}

		n = finish_packet(p);
		iowrite(c->dio, c->datafd, p->nlength, n);
		init_packet(p);
		p->c = c;
		add_byte(p, SSH_MSG_CHANNEL_REQUEST);
		add_uint32(p, ch->otherid);
		add_string(p, "shell");
		add_byte(p, 0);
		if (debug)
			fprint(2, "Sending shell request: rlength=%lud twindow=%lud\n", p->rlength, ch->twindow);
	}
	else if (strcmp(toks[0], "exec") == 0) {
		ch->state = Established;
		add_byte(p, SSH_MSG_CHANNEL_REQUEST);
		add_uint32(p, ch->otherid);
		add_string(p, "exec");
		add_byte(p, 0);
		cmd = malloc(1024);
		q = seprint(cmd, cmd+1024, "%s", toks[1]);
		for (n = 2; n < ntok; ++n) {
			q = seprint(q, cmd+1024, " %s", toks[n]);
			if (q == nil)
				break;
		}
		add_string(p, cmd);
		free(cmd);
	}
	else if (strcmp(toks[0], "subsystem") == 0) {
		ch->state = Established;
		add_byte(p, SSH_MSG_CHANNEL_REQUEST);
		add_uint32(p, ch->otherid);
		add_string(p, "subsystem");
		add_byte(p, 0);				/* no reply */
		cmd = malloc(1024);
		q = seprint(cmd, cmd+1024, "%s", toks[1]);
		for (n = 2; n < ntok; ++n) {
			q = seprint(q, cmd+1024, " %s", toks[n]);
			if (q == nil)
				break;
		}
		add_string(p, cmd);
		free(cmd);
	}
	else {
		r->aux = 0;
		respond(r, "invalid request command");
		free(buf);
		threadexits(nil);
	}
	n = finish_packet(p);
	iowrite(c->dio, c->datafd, p->nlength, n);
	free(p);
	r->aux = 0;
	respond(r, nil);
	free(buf);
	threadexits(nil);
}


void
writedata(void *a)
{
	Req *r;
	Packet *p;
	Conn *c;
	SSHChan *ch;
	int n, fnum, xconn;

	r = a;
	fnum = (uintptr)r->fid->file->aux;
	xconn = (fnum >> CONNSHIFT) & ConnMask;
	c = connections[xconn];
	if (c == nil) {
		respond(r, "Invalid connection");
		threadexits(nil);
	}
	ch = c->chans[fnum & ConnMask];
	p = new_packet(c);
	add_byte(p, SSH_MSG_CHANNEL_DATA);
	hnputl(p->payload+1, ch->otherid);
	p->rlength += 4;
	add_block(p, r->ifcall.data, r->ifcall.count);
	n = finish_packet(p);
	if (ch->sent + p->rlength <= ch->twindow) {
		iowrite(c->dio, c->datafd, p->nlength, n);
		r->aux = 0;
		respond(r, nil);
		free(p);
		threadexits(nil);
	}
	qlock(&ch->xmtlock);
	while (ch->sent + p->rlength > ch->twindow)
		rsleep(&ch->xmtrendez);
	qunlock(&ch->xmtlock);
	iowrite(c->dio, c->datafd, p->nlength, n);
	free(p);
	r->aux = 0;
	respond(r, nil);
	threadexits(nil);
}

/*
 * Although this is named stclunk, it's attached to the destroyfid
 * member of the Srv struct.  It turns out there's no member
 * called clunk.  But if there are no other references, a 9P Tclunk
 * will end up calling destroyfid.
 */
void
stclunk(Fid *f)
{
	Packet *p;
	Conn *c;
	SSHChan *sc;
	int n, fnum, lev, cnum, chnum;

	if (f == nil || f->file == nil)
		return;
	fnum = (uintptr)f->file->aux;
	lev = fnum >> LEVSHIFT;
	cnum = (fnum >> CONNSHIFT) & ConnMask;
	chnum = fnum & ConnMask;
	if (debug)
		fprint(2, "Got destroy fid on file: %x %d %d %d: %s\n", fnum, lev, cnum, chnum, f->file->name);
	if (lev == 0 && fnum == ReqRemFile) {
		if (keymbox.state != Empty) {
			keymbox.state = Empty;
			//nbsendul(keymbox.mchan, 1);
		}
		keymbox.msg = nil;
		return;
	}
	c = connections[cnum];
	if (c == nil)
		return;
	if (lev == 1 && (fnum & FileMask) == CtlFile
			&& (c->state == Opening || c->state == Negotiating
			|| c->state == Authing)) {
		for (n = 0; n < MAXCONN
			&& (!c->chans[n] || c->chans[n]->state == Empty || c->chans[n]->state == Closed || c->chans[n]->state == Closing); ++n) ;
		if (n >= MAXCONN) {
			if (c->rpid >= 0) {
				threadint(c->rpid);
			}
			shutdown(c);
		}
		return;
	}
	sc = c->chans[chnum];
	if (lev == 2) {
		if ((fnum & FileMask) == ListenFile && sc->state == Listening) {
			qlock(&c->l);
			if (sc->state != Closed) {
				sc->state = Closed;
				chanclose(sc->inchan);
				chanclose(sc->reqchan);
			}
			qunlock(&c->l);
		}
		else if ((fnum & FileMask) == DataFile && sc->state != Empty
				&& sc->state != Closed && sc->state != Closing) {
			if (f->file != sc->data && f->file != sc->request) {
				fprint(2, "Great evil is upon us destroying a fid we didn't create\n");
				return;
			}
			p = new_packet(c);
			add_byte(p, SSH_MSG_CHANNEL_CLOSE);
			hnputl(p->payload+1, sc->otherid);
			p->rlength += 4;
			n = finish_packet(p);
			sc->state = Closing;
			iowrite(c->dio, c->datafd, p->nlength, n);
			free(p);
			qlock(&c->l);
			rwakeup(&sc->r);
			qunlock(&c->l);
			nbsendul(sc->inchan, 1);
			nbsendul(sc->reqchan, 1);
		}
		for (n = 0; n < MAXCONN 
			&& (!c->chans[n] || c->chans[n]->state == Empty || c->chans[n]->state == Closed || c->chans[n]->state == Closing); ++n) ;
		if (n >= MAXCONN) {
			if (c->rpid >= 0) {
				threadint(c->rpid);
			}
			shutdown(c);
		}
	}
}

void
stflush(Req *r)
{
	int fnum;

	fnum = (uintptr)r->oldreq->fid->file->aux;
	if (debug)
		fprint(2, "Got flush on file %x %d %d %d: %s %p\n", fnum, fnum >> LEVSHIFT, (fnum >> CONNSHIFT) & ConnMask, fnum & ConnMask, r->oldreq->fid->file->name, r->oldreq->aux);
	if (r->oldreq->aux) {
		if (r->oldreq->ifcall.type == Topen && (fnum & FileMask) == ListenFile && (fnum >> LEVSHIFT) == 1) {
			threadint((uintptr)r->oldreq->aux);
		}
		else if(r->oldreq->ifcall.type == Tread && (fnum & FileMask) == DataFile && (fnum >> LEVSHIFT) == 2) {
			threadint((uintptr)r->oldreq->aux);
		}
		else if(r->oldreq->ifcall.type == Tread && (fnum & FileMask) == ReqRemFile) {
			threadint((uintptr)r->oldreq->aux);
		}
		else {
			threadkill((uintptr)r->oldreq->aux);
			r->oldreq->aux = 0;
			respond(r->oldreq, "interrupted");
		}
	}
	else
		respond(r->oldreq, "interrupted");
	respond(r, nil);
}

void
filedup(Req *r, File *src)
{
	r->ofcall.qid = src->qid;
	closefile(r->fid->file);
	r->fid->file = src;
	incref(src);
}

Conn *
alloc_conn(void)
{
	static QLock aclock;
	Conn *c;
	int sconn, slev, i, s, firstnil;
	char buf[20];

	qlock(&aclock);
	firstnil = -1;
	for (i = 0; i < MAXCONN; ++i) {
		if (connections[i] == nil) {
			if (firstnil == -1)
				firstnil = i;
			continue;
		}
		s = connections[i]->state;
		if (s == Empty || s == Closed)
			break;
	}
	if (i >= MAXCONN) {
		if (firstnil != -1) {
			connections[firstnil] = emalloc9p(sizeof (Conn));
			memset(connections[firstnil], 0, sizeof (Conn));
			i = firstnil;
		}
		else {
			qunlock(&aclock);
			return nil;
		}
	}
	sconn = i << CONNSHIFT;
	c = connections[i];
	memset(&c->r, '\0', sizeof(Rendez));
	c->r.l = &c->l;
	c->dio = ioproc();
	c->rio = nil;
	c->state = Allocated;
	c->role = Server;
	c->id = i;
	c->user = nil;
	c->service = nil;
	c->nchan = 0;
	c->ctlfd = -1;
	c->datafd = -1;
	c->rpid = -1;
	c->inseq = 0;
	c->outseq = 0;
	c->cscrypt = -1;
	c->sccrypt = -1;
	c->csmac = -1;
	c->scmac = -1;
	c->ncscrypt = -1;
	c->nsccrypt = -1;
	c->ncsmac = -1;
	c->nscmac = -1;
	c->encrypt = -1;
	c->decrypt = -1;
	c->outmac = -1;
	c->inmac = -1;
	if (c->e) {
		mpfree(c->e);
		c->e = nil;
	}
	if (c->x) {
		mpfree(c->x);
		c->x = nil;
	}
	slev = 1 << LEVSHIFT;
	snprint(buf, 20, "%d", i);
	if (c->dir == nil) {
		c->dir = createfile(rootfile, buf, uid, 0555|DMDIR, (void *)(slev | sconn));
		c->clonefile = createfile(c->dir, "clone", uid, 0666, (void *)(slev | CloneFile | sconn));
		c->ctlfile = createfile(c->dir, "ctl", uid, 0666, (void *)(slev | CtlFile | sconn));
		c->datafile = createfile(c->dir, "data", uid, 0666, (void *)(slev | DataFile  | sconn));
		c->listenfile = createfile(c->dir, "listen", uid, 0666, (void *)(slev | ListenFile | sconn));
		c->localfile = createfile(c->dir, "local", uid, 0444, (void *)(slev | LocalFile | sconn));
		c->remotefile = createfile(c->dir, "remote", uid, 0444, (void *)(slev | ReqRemFile | sconn));
		c->statusfile = createfile(c->dir, "status", uid, 0444, (void *)(slev | StatusFile | sconn));
	}
//	c->skexinit = nil;
//	c->rkexinit = nil;
	c->got_sessid = 0;
	c->otherid = nil;
	c->inik = nil;
	c->outik = nil;
	c->s2ccs = nil;
	c->c2scs = nil;
	c->enccs = nil;
	c->deccs = nil;
	qunlock(&aclock);
	return c;
}

SSHChan *
alloc_chan(Conn *c)
{
	SSHChan *sc;
	Plist *p;
	int cnum, slev, sconn;
	char buf[10];

	if (c->nchan >= MAXCONN)
		return nil;
	qlock(&c->l);
	cnum = c->nchan;
	if (c->chans[cnum] == nil) {
		c->chans[cnum] = emalloc9p(sizeof (SSHChan));
		memset(c->chans[cnum], 0, sizeof (SSHChan));
	}
	sc = c->chans[cnum];
	snprint(buf, 10, "%d", cnum);
	memset(&sc->r, '\0', sizeof(Rendez));
	sc->r.l = &c->l;
	sc->id = cnum;
	sc->otherid = -1;
	sc->state = Empty;
	sc->waker = -1;
	sc->conn = c->id;
	sc->sent = 0;
	sc->twindow = 0;
	sc->rwindow = 0;
	sc->inrqueue = 0;
	sc->ann = nil;
	sc->lreq = nil;
	slev = 2 << LEVSHIFT;
	sconn = c->id << CONNSHIFT;
	if (sc->dir == nil) {
		sc->dir = createfile(c->dir, buf, uid, 0555|DMDIR, (void *)(slev | sconn | cnum));
		sc->ctl = createfile(sc->dir, "ctl", uid, 0666, (void *)(slev | CtlFile | sconn | cnum));
		sc->data = createfile(sc->dir, "data", uid, 0666, (void *)(slev | DataFile | sconn | cnum));
		sc->listen = createfile(sc->dir, "listen", uid, 0666, (void *)(slev | ListenFile | sconn | cnum));
		sc->request = createfile(sc->dir, "request", uid, 0666,
			(void *)(slev | ReqRemFile | sconn | cnum));
		sc->status = createfile(sc->dir, "status", uid, 0444, (void *)(slev | StatusFile | sconn | cnum));
	}
	c->nchan++;
	sc->dataq = nil;
	sc->datatl = nil;
	while (sc->reqq != nil) {
		p = sc->reqq;
		sc->reqq = p->next;
		free(p->pack);
		free(p);
	}
	sc->reqtl = nil;
	if (sc->inchan)
		chanfree(sc->inchan);
	sc->inchan = chancreate(4, 0);
	if (sc->reqchan)
		chanfree(sc->reqchan);
	sc->reqchan = chancreate(4, 0);
	memset(&sc->xmtrendez, '\0', sizeof(Rendez));
	sc->xmtrendez.l = &sc->xmtlock;
	qunlock(&c->l);
	return sc;
}

int
dohandshake(Conn *c, char *tcpchan)
{
	Ioproc *io;
	char *p;
	int fd, n;
	char path[256], buf[32];

	io = ioproc();
	snprint(path, 256, "%s/tcp/%s/remote", mntpt, tcpchan);
	fd = ioopen(io, path, OREAD);
	n = ioread(io, fd, buf, 31);
	if (n > 0) {
		buf[n] = 0;
		p = strchr(buf, '!');
		if (p)
			*p = 0;
		if (c->remote)
			free(c->remote);
		c->remote = estrdup9p(buf);
	}
	ioclose(io, fd);
	snprint(path, 256, "%s/tcp/%s/data", mntpt, tcpchan);
	fd = ioopen(io, path, ORDWR);
	if (fd < 0) {
		closeioproc(io);
		return -1;
	}
	c->datafd = fd;

	/* exchange versions--we're snobbishly only doing SSH2 */

	snprint(path, 256, "%s\r\n", MYID);
	iowrite(io, fd, path, strlen(path));
	p = path;
	n = 0;
	do {
		if (ioread(io, fd, p, 1) < 0) {
			fprint(2, "Read failure in ID exchange: %r\n");
			break;
		}
		++n;
	} while (*p++ != '\n');
	if (n < 5) {
		close(fd);
		snprint(path, 256, "%s/tcp/%s/ctl", mntpt, tcpchan);
		fd = ioopen(io, path, OWRITE);
		iowrite(io, fd, "hangup", 6);
		ioclose(io, fd);
		closeioproc(io);
		return -1;
	}
	*p = 0;
	if (debug)
		fprint(2, "id string: %d:%s", n, path);
	if (strncmp(path, "SSH-2", 5) != 0 && strncmp(path, "SSH-1.99", 8) != 0) {
		ioclose(io, fd);
		snprint(path, 256, "%s/tcp/%s/ctl", mntpt, tcpchan);
		fd = ioopen(io,path, OWRITE);
		iowrite(io, fd, "hangup", 6);
		ioclose(io, fd);
		closeioproc(io);
		return -1;
	}
	closeioproc(io);
	if (c->otherid)
		free(c->otherid);
	c->otherid = estrdup9p(path);
	for (n = strlen(c->otherid) - 1; c->otherid[n] == '\r' || c->otherid[n] == '\n'; --n)
		c->otherid[n] = '\0';
	c->state = Initting;

	/* start the reader thread */
	if (c->rpid < 0)
		c->rpid = threadcreate(reader, c, 8192);

	/*
	 * negotiate the protocols
	 * We don't do the full negotiation here, because we also have
	 * to handle a re-negotiation request from the other end.  So
	 * we just kick it off and let the receiver process take it from there.
	 */

	send_kexinit(c);

	qlock(&c->l);
	if ((c->role == Client && c->state != Negotiating) || (c->role == Server && c->state != Established))
		rsleep(&c->r);
	qunlock(&c->l);
	if (c->role == Server && c->state != Established)
		return -1;
	if (c->role == Client && c->state != Negotiating)
		return -1;
	return 0;
}

void
send_kexinit(Conn *c)
{
	Packet *ptmp;
	char *p, *e;
	int msglen;
	int i;
	char *buf;

	if (debug)
		fprint(2, "Initializing kexinit packet\n");
	if (c->skexinit != nil)
		free(c->skexinit);
	c->skexinit = new_packet(c);
	buf = emalloc9p(1024);
	buf[0] = (uchar)SSH_MSG_KEXINIT;
	add_packet(c->skexinit, buf, 1);
	for (i = 0; i < 16; ++i)
		buf[i] = fastrand();
	add_packet(c->skexinit, buf, 16);		/* cookie */
	e = buf+1023;
	p = seprint(buf, e, "%s", kexes[0]->name);
	for (i = 1; i < nelem(kexes); ++i)
		p = seprint(p, e, ",%s", kexes[i]->name);
	if (debug)
		fprint(2, "Sent KEX algs: %s\n", buf);
	add_string(c->skexinit, buf);		/* Key exchange */
	if (pkas[0] == nil)
		add_string(c->skexinit, "");
	else{
		p = seprint(buf, e, "%s", pkas[0]->name);
		for (i = 1; i < nelem(pkas) && pkas[i] != nil; ++i)
			p = seprint(p, e, ",%s", pkas[i]->name);
		if (debug)
			fprint(2, "Sent host key algs: %s\n", buf);
		add_string(c->skexinit, buf);		/* server's key algs */
	}
	p = seprint(buf, e, "%s", cryptos[0]->name);
	for (i = 1; i < nelem(cryptos); ++i)
		p = seprint(p, e, ",%s", cryptos[i]->name);
	if (debug)
		fprint(2, "Sent crypto algs: %s\n", buf);
	add_string(c->skexinit, buf);		/* c->s crypto */
	add_string(c->skexinit, buf);		/* s->c crypto */
	p = seprint(buf, e, "%s", macnames[0]);
	for (i = 1; i < nelem(macnames); ++i)
		p = seprint(p, e, ",%s", macnames[i]);
	if (debug)
		fprint(2, "Sent MAC algs: %s\n", buf);
	add_string(c->skexinit, buf);		/* c->s mac */
	add_string(c->skexinit, buf);		/* s->c mac */
	add_string(c->skexinit, "none");		/* c->s compression */
	add_string(c->skexinit, "none");		/* s->c compression */
	add_string(c->skexinit, "");		/* c->s languages */
	add_string(c->skexinit, "");		/* s->c languages */
	memset(buf, 0, 5);
	add_packet(c->skexinit, buf, 5);
	ptmp = new_packet(c);
	memmove(ptmp, c->skexinit, sizeof(Packet));
	msglen = finish_packet(ptmp);
	if (c->dio && c->datafd >= 0)
		iowrite(c->dio, c->datafd, ptmp->nlength, msglen);
	free(ptmp);
	free(buf);
}

void
reader(void *a)
{
	Packet *p, *p2;
	Plist *pl;
	Conn *c;
	SSHChan *ch;
	uchar *q;
	int i, n,nl, np, nm, nb, cnum;
	char buf[256];

	c = a;
	c->rpid = threadid();
	if (debug)
		fprint(2, "Starting reader for connection %d,  pid:%d\n", c->id, c->rpid);
	threadsetname("reader");
	p = new_packet(c);
	p2 = new_packet(c);
	c->rio = ioproc();
	while (1) {
		nm = 0;
		nb = 4;
		if (c->decrypt != -1)
			nb = cryptos[c->decrypt]->blklen;
		if (debug)
			fprint(2, "calling read for connection %d, state %d, nb %d, dc %d\n",
				c->id, c->state, nb, c->decrypt);
		if ((nl = ioreadn(c->rio, c->datafd, p->nlength, nb)) != nb) {
			if (debug)
				fprint(2, "Reader for connection %d exiting, nl=%d: %r\n", c->id, nl);
			goto bail;
		}
		if (c->decrypt != -1)
			cryptos[c->decrypt]->decrypt(c->deccs, p->nlength, nb);
		p->rlength = nhgetl(p->nlength);
		if (debug)
			fprint(2, "Got message length: %ld\n", p->rlength);
		if (p->rlength > 35000) {
			if (debug)
				fprint(2, "Absurd packet length: %ld, unrecoverable decrypt failure\n", p->rlength);
			goto bail;
		}
		np = ioreadn(c->rio, c->datafd, p->nlength+nb, p->rlength + 4 - nb);
		if (c->inmac != -1)
			nm = ioreadn(c->rio, c->datafd, p->nlength + p->rlength + 4, 20);
		n = nl + np + nm;
		if (debug) {
			fprint(2, "got message of %d bytes %d padding", n, p->pad_len);
			if (p->payload[0] > SSH_MSG_CHANNEL_OPEN) {
				i = nhgetl(p->payload+1);
				if (c->chans[i])
					fprint(2, " for channel %d win %lud", i, c->chans[i]->rwindow);
				else
					fprint(2, " for invalid channel %d", i);
			}
			fprint(2, ": first byte: %d\n", p->payload[0]);
		}
		if (np != p->rlength + 4 - nb || c->inmac != -1 && nm != 20) {
			if (debug)
				fprint(2, "Got EOF/error on connection read: %d %d %r\n", np, nm);
			goto bail;
		}
		p->tlength = n;
		p->rlength = n - 4;
		if (undo_packet(p) < 0) {
			if (debug)
				fprint(2, "Bad packet in connection %d: exiting\n", c->id);
			goto bail;
		}
		if (c->state == Initting) {
			if (p->payload[0] != SSH_MSG_KEXINIT) {
				if (debug)
					fprint(2, "Missing KEX init packet: %d\n", p->payload[0]);
				goto bail;
			}
			if (c->rkexinit)
				free(c->rkexinit);
			c->rkexinit = new_packet(c);
			memmove(c->rkexinit, p, sizeof(Packet));
			if (validatekex(c, p) < 0) {
				if (debug)
					fprint(2, "Algorithm mismatch\n");
				goto bail;
			}
			if (debug)
				fprint(2, "Using %s Kex algorithm and %s PKA\n",
					kexes[c->kexalg]->name, pkas[c->pkalg]->name);
			if (c->role == Client)
				kexes[c->kexalg]->clientkex1(c, p);
			c->state = Negotiating;
		}
		else if (c->state == Negotiating) {
			switch (p->payload[0]) {
			case SSH_MSG_IGNORE:
				break;
			case SSH_MSG_DISCONNECT:
				if (debug) {
					get_string(p, p->payload + 5, buf, 256, nil);
					fprint(2, "Got disconnect: %s\n", buf);
				}
				goto bail;
				break;
			case SSH_MSG_NEWKEYS:
				/*
				 * If we're just updating, go straight to established,
				 * otherwise wait for authentication
				 */
				i = c->encrypt;
				memmove(c->c2siv, c->nc2siv, SHA1dlen*2);
				memmove(c->s2civ, c->ns2civ, SHA1dlen*2);
				memmove(c->c2sek, c->nc2sek, SHA1dlen*2);
				memmove(c->s2cek, c->ns2cek, SHA1dlen*2);
				memmove(c->c2sik, c->nc2sik, SHA1dlen*2);
				memmove(c->s2cik, c->ns2cik, SHA1dlen*2);
				c->cscrypt = c->ncscrypt;
				c->sccrypt = c->nsccrypt;
				c->csmac = c->ncsmac;
				c->scmac = c->nscmac;
				c->c2scs = cryptos[c->cscrypt]->init(c, 0);
				c->s2ccs = cryptos[c->sccrypt]->init(c, 1);
				if (c->role == Server) {
					c->encrypt = c->sccrypt;
					c->decrypt = c->cscrypt;
					c->outmac = c->scmac;
					c->inmac = c->csmac;
					c->enccs = c->s2ccs;
					c->deccs = c->c2scs;
					c->outik = c->s2cik;
					c->inik = c->c2sik;
				}
				else{
					c->encrypt = c->cscrypt;
					c->decrypt = c->sccrypt;
					c->outmac = c->csmac;
					c->inmac = c->scmac;
					c->enccs = c->c2scs;
					c->deccs = c->s2ccs;
					c->outik = c->c2sik;
					c->inik = c->s2cik;
				}
				if (debug)
					fprint(2, "Using %s for encryption and %s for decryption\n",
						cryptos[c->encrypt]->name, cryptos[c->decrypt]->name);
				qlock(&c->l);
				if (i != -1)
					c->state = Established;
				if (c->role == Client) {
					rwakeup(&c->r);
				}
				qunlock(&c->l);
				break;
			case SSH_MSG_KEXDH_INIT:
				kexes[c->kexalg]->serverkex(c, p);
				break;
			case SSH_MSG_KEXDH_REPLY:
				init_packet(p2);
				p2->c = c;
				if (kexes[c->kexalg]->clientkex2(c, p) >= 0) {
					add_byte(p2, SSH_MSG_NEWKEYS);
					n = finish_packet(p2);
					iowrite(c->rio, c->datafd, p2->nlength, n);
					qlock(&c->l);
					rwakeup(&c->r);
					qunlock(&c->l);
				}
				else{
					add_byte(p2, SSH_MSG_DISCONNECT);
					add_byte(p2, SSH_DISCONNECT_KEY_EXCHANGE_FAILED);
					add_string(p2, "Key exchange failure");
					add_string(p2, "");
					n = finish_packet(p2);
					iowrite(c->rio, c->datafd, p2->nlength, n);
					shutdown(c);
					free(p);
					free(p2);
					closeioproc(c->rio);
					c->rio = nil;
					c->rpid = -1;
					qlock(&c->l);
					rwakeup(&c->r);
					qunlock(&c->l);
					threadexits(nil);
				}
				break;
			case SSH_MSG_SERVICE_REQUEST:
				get_string(p, p->payload + 1, buf, 256, nil);
				if (debug)
					fprint(2, "Got service request: %s\n", buf);
				if (strcmp(buf, "ssh-userauth") == 0 || strcmp(buf, "ssh-connection") == 0) {
					init_packet(p2);
					p2->c = c;
					if (slfd > 0)
						fprint(slfd, "ssh connection from %s\n", c->remote);
					else
						syslog(1, "ssh", "ssh connection from %s", c->remote);
					add_byte(p2, SSH_MSG_SERVICE_ACCEPT);
					add_string(p2, buf);
					n = finish_packet(p2);
					iowrite(c->rio, c->datafd, p2->nlength, n);
					c->state = Authing;
				}
				else{
					init_packet(p2);
					p2->c = c;
					add_byte(p2, SSH_MSG_DISCONNECT);
					add_byte(p2, SSH_DISCONNECT_SERVICE_NOT_AVAILABLE);
					add_string(p2, "Unknown service type");
					add_string(p2, "");
					n = finish_packet(p2);
					iowrite(c->rio, c->datafd, p2->nlength, n);
					goto bail;
				}
				break;
			case SSH_MSG_SERVICE_ACCEPT:
				get_string(p, p->payload + 1, buf, 256, nil);
				if (c->service && strcmp(c->service, "ssh-userauth") == 0) {
					free(c->service);
					c->service = estrdup9p("ssh-connection");
				}
				if (debug)
					fprint(2, "Got service accept: %s: responding with %s %s\n", buf, c->user, c->service);
				n = client_auth(c, c->rio);
				c->state = Authing;
				if (n < 0) {
					qlock(&c->l);
					rwakeup(&c->r);
					qunlock(&c->l);
				}
				break;
			default:
				break;
			}
		}
		else if (c->state == Authing) {
			switch (p->payload[0]) {
			case SSH_MSG_IGNORE:
				break;
			case SSH_MSG_DISCONNECT:
				if (debug) {
					get_string(p, p->payload + 5, buf, 256, nil);
					fprint(2, "Got disconnect: %s\n", buf);
				}
				goto bail;
				break;
			case SSH_MSG_USERAUTH_REQUEST:
				switch (auth_req(p, c)) {
				case 0:
					qlock(&c->l);
					c->state = Established;
					rwakeup(&c->r);
					qunlock(&c->l);
					break;
				case -1:
					break;
				case -2:
					goto bail;
					break;
				}
				break;
			case SSH_MSG_USERAUTH_FAILURE:
				qlock(&c->l);
				rwakeup(&c->r);
				qunlock(&c->l);
				break;
			case SSH_MSG_USERAUTH_SUCCESS:
				qlock(&c->l);
				c->state = Established;
				rwakeup(&c->r);
				qunlock(&c->l);
				break;
			case SSH_MSG_USERAUTH_BANNER:
				break;
			}
		}
		else if (c->state == Established) {
			if (debug >1) {
				fprint(2, "In Established state, got:\n");
				dump_packet(p);
			}
			switch (p->payload[0]) {
			case SSH_MSG_IGNORE:
				break;
			case SSH_MSG_DISCONNECT:
				if (debug) {
					get_string(p, p->payload + 5, buf, 256, nil);
					fprint(2, "Got disconnect: %s\n", buf);
				}
				goto bail;
				break;
			case SSH_MSG_UNIMPLEMENTED:
				break;
			case SSH_MSG_DEBUG:
				if (debug || p->payload[1]) {
					get_string(p, p->payload + 2, buf, 256, nil);
					fprint(2, "Got debug message: %s\n", buf);
				}
				break;
			case SSH_MSG_KEXINIT:
				send_kexinit(c);
				if (c->rkexinit)
					free(c->rkexinit);
				c->rkexinit = new_packet(c);
				memmove(c->rkexinit, p, sizeof(Packet));
				if (validatekex(c, p) < 0) {
					if (debug)
						fprint(2, "Algorithm mismatch\n");
					goto bail;
				}
				if (debug)
					fprint(2, "Using %s Kex algorithm and %s PKA\n",
						kexes[c->kexalg]->name, pkas[c->pkalg]->name);
				c->state = Negotiating;
				break;
			case SSH_MSG_GLOBAL_REQUEST:
				break;
			case SSH_MSG_REQUEST_SUCCESS:
				break;
			case SSH_MSG_REQUEST_FAILURE:
				break;
			case SSH_MSG_CHANNEL_OPEN:
				q = get_string(p, p->payload + 1, buf, 256, nil);
				if (debug)
					fprint(2, "Searching for a listener for channel type %s\n", buf);
				ch = alloc_chan(c);
				if (ch == nil) {
					init_packet(p2);
					p2->c = c;
					add_byte(p2, SSH_MSG_CHANNEL_OPEN_FAILURE);
					add_block(p2, p->payload + 5, 4);
					hnputl(p2->payload + p2->rlength - 1, 4);
					p2->rlength += 4;
					add_string(p2, "No available channels");
					add_string(p2, "EN");
					n = finish_packet(p2);
					iowrite(c->rio, c->datafd, p2->nlength, n);
					break;
				}
				if (debug)
					fprint(2, "alloced channel %d for listener\n", ch->id);
				qlock(&c->l);
				ch->otherid = nhgetl(q);
				ch->twindow = nhgetl(q+4);
				if (debug)
					fprint(2, "Got lock in channel open\n");
				for (i = 0; i < c->nchan; ++i)
					if (c->chans[i] && c->chans[i]->state == Listening && c->chans[i]->ann
							&& strcmp(c->chans[i]->ann, buf) == 0)
						break;
				if (i >= c->nchan) {
					if (debug)
						fprint(2, "No listener: sleeping\n");
					ch->state = Opening;
					if (ch->ann)
						free(ch->ann);
					ch->ann = estrdup9p(buf);
					if (debug)
						fprint(2, "Waiting for someone to announce %s\n", ch->ann);
					rsleep(&ch->r);
				}
				else{
					if (debug)
						fprint(2, "Found listener on channel %d\n", ch->id);
					c->chans[i]->waker = ch->id;
					rwakeup(&c->chans[i]->r);
				}
				qunlock(&c->l);
				break;
			case SSH_MSG_CHANNEL_OPEN_CONFIRMATION:
				cnum = nhgetl(p->payload + 1);
				ch = c->chans[cnum];
				qlock(&c->l);
				ch->otherid = nhgetl(p->payload+5);
				ch->twindow = nhgetl(p->payload+9);
				rwakeup(&ch->r);
				qunlock(&c->l);
				break;
			case SSH_MSG_CHANNEL_OPEN_FAILURE:
				cnum = nhgetl(p->payload + 1);
				ch = c->chans[cnum];
				qlock(&c->l);
				rwakeup(&ch->r);
				qunlock(&c->l);
				goto bail;
				break;
			case SSH_MSG_CHANNEL_WINDOW_ADJUST:
				cnum = nhgetl(p->payload + 1);
				ch = c->chans[cnum];
				ch->twindow += nhgetl(p->payload + 5);
				if (debug)
					fprint(2, "New twindow for channel: %d: %lud\n", cnum, ch->twindow);
				qlock(&ch->xmtlock);
				rwakeup(&ch->xmtrendez);
				qunlock(&ch->xmtlock);
				break;
			case SSH_MSG_CHANNEL_DATA:
			case SSH_MSG_CHANNEL_EXTENDED_DATA:
				cnum = nhgetl(p->payload + 1);
				ch = c->chans[cnum];
				pl = emalloc9p(sizeof(Plist));
				pl->pack = emalloc9p(sizeof(Packet));
				memmove(pl->pack, p, sizeof(Packet));
				if (p->payload[0] == SSH_MSG_CHANNEL_DATA) {
					pl->rem = nhgetl(p->payload+5);
					pl->st = pl->pack->payload+9;
				}
				else {
					pl->rem = nhgetl(p->payload+9);
					pl->st = pl->pack->payload+13;
				}
				pl->next = nil;
				if (ch->dataq == nil)
					ch->dataq = pl;
				else
					ch->datatl->next = pl;
				ch->datatl = pl;
				ch->inrqueue += pl->rem;
				nbsendul(ch->inchan, 1);
				break;
			case SSH_MSG_CHANNEL_EOF:
				cnum = nhgetl(p->payload + 1);
				ch = c->chans[cnum];
				if (ch->state != Closed && ch->state != Closing) {
					ch->state = Eof;
					nbsendul(ch->inchan, 1);
					nbsendul(ch->reqchan, 1);
				}
				break;
			case SSH_MSG_CHANNEL_CLOSE:
				cnum = nhgetl(p->payload + 1);
				ch = c->chans[cnum];
				if (ch->state != Closed && ch->state != Closing) {
					init_packet(p2);
					p2->c = c;
					add_byte(p2, SSH_MSG_CHANNEL_CLOSE);
					hnputl(p2->payload + 1, ch->otherid);
					p2->rlength += 4;
					n = finish_packet(p2);
					iowrite(c->rio, c->datafd, p2->nlength, n);
				}
				qlock(&c->l);
				if (ch->state != Closed) {
					ch->state = Closed;
					rwakeup(&ch->r);
					nbsendul(ch->inchan, 1);
					nbsendul(ch->reqchan, 1);
					chanclose(ch->inchan);
					chanclose(ch->reqchan);
				}
				qunlock(&c->l);
				for (i = 0; i < MAXCONN && (!c->chans[i] || c->chans[i]->state == Empty || c->chans[i]->state == Closed); ++i) ;
				if (i >= MAXCONN) {
					goto bail;
				}
				break;
			case SSH_MSG_CHANNEL_REQUEST:
				cnum = nhgetl(p->payload + 1);
				ch = c->chans[cnum];
				if (debug)
					fprint(2, "Queueing channel request for channel: %d\n", cnum);
				q = get_string(p, p->payload+5, buf, 256, nil);
				pl = emalloc9p(sizeof(Plist));
				pl->pack = emalloc9p(sizeof(Packet));
				n = snprint((char *)pl->pack->payload, 32768, "%s %c", buf, *q ? 't': 'f');
				if (debug)
					fprint(2, "request message begins: %s\n", (char *)pl->pack->payload);
				memmove(pl->pack->payload + n, q+1, p->rlength - (11 + (n-2)));
				pl->rem = p->rlength - 11 + 2;
				pl->st = pl->pack->payload;
				pl->next = nil;
				if (ch->reqq == nil)
					ch->reqq = pl;
				else
					ch->reqtl->next = pl;
				ch->reqtl = pl;
				nbsendul(ch->reqchan, 1);
				break;
			case SSH_MSG_CHANNEL_SUCCESS:
				break;
			case SSH_MSG_CHANNEL_FAILURE:
				break;
			default:
				break;
			}
		}
		else {
			if (debug)
				fprint(2, "Connection  %d in invalid state, reader exiting\n", c->id);
bail:
			shutdown(c);
			free(p);
			free(p2);
			if (c->rio) {
				closeioproc(c->rio);
				c->rio = nil;
			}
			c->rpid = -1;
			threadexits(nil);
		}
	}
}

int
validatekex(Conn *c, Packet *p)
{
	if (c->role == Server)
		return validatekexs(p);
	else
		return validatekexc(p);
}

int
validatekexs(Packet *p)
{
	uchar *q;
	char *toks[128];
	int i, j, n;
	char *buf;

	buf = emalloc9p(1024);
	q = p->payload + 17;
	q = get_string(p, q, buf, 1024, nil);
	if (debug)
		fprint(2, "Received KEX algs: %s\n", buf);
	n = gettokens(buf, toks, 128, ",");
	for (i = 0; i < n; ++i)
		for (j = 0; j < nelem(kexes); ++j)
			if (strcmp(toks[i], kexes[j]->name) == 0)
				goto foundk;
	free(buf);
	return -1;
foundk:
	p->c->kexalg = j;
	q = get_string(p, q, buf, 1024, nil);
	if (debug)
		fprint(2, "Received host key algs: %s\n", buf);
	n = gettokens(buf, toks, 128, ",");
	for (i = 0; i < n; ++i)
		for (j = 0; j < nelem(pkas) && pkas[j] != nil; ++j)
			if (strcmp(toks[i], pkas[j]->name) == 0)
				goto foundpka;
	free(buf);
	return -1;
foundpka:
	p->c->pkalg = j;
	q = get_string(p, q, buf, 1024, nil);
	if (debug)
		fprint(2, "Received C2S crypto algs: %s\n", buf);
	n = gettokens(buf, toks, 128, ",");
	for (i = 0; i < n; ++i)
		for (j = 0; j < nelem(cryptos); ++j)
			if (strcmp(toks[i], cryptos[j]->name) == 0)
				goto foundc1;
	free(buf);
	return -1;
foundc1:
	p->c->ncscrypt = j;
	q = get_string(p, q, buf, 1024, nil);
	if (debug)
		fprint(2, "Received S2C crypto algs: %s\n", buf);
	n = gettokens(buf, toks, 128, ",");
	for (i = 0; i < n; ++i)
		for (j = 0; j < nelem(cryptos); ++j)
			if (strcmp(toks[i], cryptos[j]->name) == 0)
				goto foundc2;
	free(buf);
	return -1;
foundc2:
	p->c->nsccrypt = j;
	q = get_string(p, q, buf, 1024, nil);
	if (debug)
		fprint(2, "Received C2S MAC algs: %s\n", buf);
	n = gettokens(buf, toks, 128, ",");
	for (i = 0; i < n; ++i)
		for (j = 0; j < nelem(macnames); ++j)
			if (strcmp(toks[i], macnames[j]) == 0)
				goto foundm1;
	free(buf);
	return -1;
foundm1:
	p->c->ncsmac = j;
	q = get_string(p, q, buf, 1024, nil);
	if (debug)
		fprint(2, "Received S2C MAC algs: %s\n", buf);
	n = gettokens(buf, toks, 128, ",");
	for (i = 0; i < n; ++i)
		for (j = 0; j < nelem(macnames); ++j)
			if (strcmp(toks[i], macnames[j]) == 0)
				goto foundm2;
	free(buf);
	return -1;
foundm2:
	p->c->nscmac = j;
	q = get_string(p, q, buf, 1024, nil);
	q = get_string(p, q, buf, 1024, nil);
	q = get_string(p, q, buf, 1024, nil);
	q = get_string(p, q, buf, 1024, nil);
	free(buf);
	if (*q)
		return 1;
	return 0;
}

int
validatekexc(Packet *p)
{
	uchar *q;
	char *toks[128];
	int i, j, n;
	char *buf;

	buf = emalloc9p(1024);
	q = p->payload + 17;
	q = get_string(p, q, buf, 1024, nil);
	n = gettokens(buf, toks, 128, ",");
	for (j = 0; j < nelem(kexes); ++j)
		for (i = 0; i < n; ++i)
			if (strcmp(toks[i], kexes[j]->name) == 0)
				goto foundk;
	free(buf);
	return -1;
foundk:
	p->c->kexalg = j;
	q = get_string(p, q, buf, 1024, nil);
	n = gettokens(buf, toks, 128, ",");
	for (j = 0; j < nelem(pkas) && pkas[j] != nil; ++j)
		for (i = 0; i < n; ++i)
			if (strcmp(toks[i], pkas[j]->name) == 0)
				goto foundpka;
	free(buf);
	return -1;
foundpka:
	p->c->pkalg = j;
	q = get_string(p, q, buf, 1024, nil);
	n = gettokens(buf, toks, 128, ",");
	for (j = 0; j < nelem(cryptos); ++j)
		for (i = 0; i < n; ++i)
			if (strcmp(toks[i], cryptos[j]->name) == 0)
				goto foundc1;
	free(buf);
	return -1;
foundc1:
	p->c->ncscrypt = j;
	q = get_string(p, q, buf, 1024, nil);
	n = gettokens(buf, toks, 128, ",");
	for (j = 0; j < nelem(cryptos); ++j)
		for (i = 0; i < n; ++i)
			if (strcmp(toks[i], cryptos[j]->name) == 0)
				goto foundc2;
	free(buf);
	return -1;
foundc2:
	p->c->nsccrypt = j;
	q = get_string(p, q, buf, 1024, nil);
	n = gettokens(buf, toks, 128, ",");
	for (j = 0; j < nelem(macnames); ++j)
		for (i = 0; i < n; ++i)
			if (strcmp(toks[i], macnames[j]) == 0)
				goto foundm1;
	free(buf);
	return -1;
foundm1:
	p->c->ncsmac = j;
	q = get_string(p, q, buf, 1024, nil);
	n = gettokens(buf, toks, 128, ",");
	for (j = 0; j < nelem(macnames); ++j)
		for (i = 0; i < n; ++i)
			if (strcmp(toks[i], macnames[j]) == 0)
				goto foundm2;
	free(buf);
	return -1;
foundm2:
	p->c->nscmac = j;
	q = get_string(p, q, buf, 1024, nil);
	q = get_string(p, q, buf, 1024, nil);
	q = get_string(p, q, buf, 1024, nil);
	q = get_string(p, q, buf, 1024, nil);
	free(buf);
	if (*q)
		return 1;
	return 0;
}

int
memrandom(void *p, int n)
{
	uchar *cp;

	for (cp = (uchar*)p; n > 0; n--)
		*cp++ = fastrand();
	return 0;
}

/*
 *  create a change uid capability 
 */
char*
mkcap(char *from, char *to)
{
	uchar rand[20];
	char *cap;
	char *key;
	int fd, nfrom, nto;
	uchar hash[SHA1dlen];

	fd = open("#¤/caphash", OWRITE);

	/* create the capability */
	nto = strlen(to);
	nfrom = strlen(from);
	cap = emalloc9p(nfrom+1+nto+1+sizeof(rand)*3+1);
	snprint(cap, nfrom+1+nto+1+sizeof(rand)*3+1, "%s@%s", from, to);
	memrandom(rand, sizeof(rand));
	key = cap+nfrom+1+nto+1;
	enc64(key, sizeof(rand)*3, rand, sizeof(rand));

	/* hash the capability */
	hmac_sha1((uchar*)cap, strlen(cap), (uchar*)key, strlen(key), hash, nil);

	/* give the kernel the hash */
	key[-1] = '@';
	if (write(fd, hash, SHA1dlen) < 0) {
		close(fd);
		free(cap);
		return nil;
	}
	close(fd);
	return cap;
}

int
auth_req(Packet *p, Conn *c)
{
	Packet *p2;
	AuthInfo *ai;
	uchar *q;
	char *service, *me, *user, *pw, *path;
	char *alg, *blob, *sig;
	int n, fd, retval, nblob, nsig;
	char method[32];
	char key1[DESKEYLEN], key2[DESKEYLEN];

	service = emalloc9p(128);
	me = emalloc9p(128);
	user = emalloc9p(128);
	pw = emalloc9p(128);
	alg = emalloc9p(128);
	blob = emalloc9p(512);
	sig = emalloc9p(512);
	path = emalloc9p(128);
	retval = 0;
	q = get_string(p, p->payload + 1, user, 128, nil);
	if (c->user)
		free(c->user);
	c->user = estrdup9p(user);
	q = get_string(p, q, service, 128, nil);
	q = get_string(p, q, method, 32, nil);
	if (debug) {
		fprint(2, "Got userauth request: ");
		fprint(2, " %s ", user);
		fprint(2, " %s ", service);
		fprint(2, " %s\n", method);
	}
	fd = open("/dev/user", OREAD);
	n = read(fd, me, 127);
	me[n] = '\0';
	close(fd);
	p2 = new_packet(c);
	if (strcmp(method, "publickey") == 0) {
		if (*q == 0) {
			/* Should really check to see if this user can be authed this way */
			q = get_string(p, q+1, alg, 128, nil);
			get_string(p, q, blob, 512, &nblob);
			for (n = 0; n < nelem(pkas) && pkas[n] != nil && strcmp(pkas[n]->name, alg) != 0; ++n) ;
			if (n >= nelem(pkas) || pkas[n] == nil) {
				add_byte(p2, SSH_MSG_USERAUTH_FAILURE);
				add_string(p2, "password,publickey");
				add_byte(p2, 0);
				retval = -1;
			}
			else {
				add_byte(p2, SSH_MSG_USERAUTH_PK_OK);
				add_string(p2, alg);
				add_block(p2, blob, nblob);
				retval = -1;
			}
		}
		else {
			q = get_string(p, q+1, alg, 128, nil);
			q = get_string(p, q, blob, 512, &nblob);
			get_string(p, q, sig, 512, &nsig);

			for (n = 0; n < nelem(pkas) && pkas[n] != nil && strcmp(pkas[n]->name, alg) != 0; ++n) ;
			if (n >= nelem(pkas) || pkas[n] == nil) {
				add_byte(p2, SSH_MSG_USERAUTH_FAILURE);
				add_string(p2, "password,publickey");
				add_byte(p2, 0);
				retval = -1;
			}
			else {
				add_block(p2, c->sessid, SHA1dlen);
				add_byte(p2, SSH_MSG_USERAUTH_REQUEST);
				add_string(p2, user);
				add_string(p2, service);
				add_string(p2, method);
				add_byte(p2, 1);
				add_string(p2, alg);
				add_block(p2, blob, nblob);
				if (pkas[n]->verify(c, p2->payload, p2->rlength - 1, user, sig, nsig)) {
					if (c->cap != nil)
						free(c->cap);
					c->cap = mkcap(me, user);
					init_packet(p2);
					p2->c = c;
					if (slfd > 0)
						fprint(slfd, "ssh logged in as %s\n", user);
					else
						syslog(1, "ssh", "ssh logged in as %s", user);
					add_byte(p2, SSH_MSG_USERAUTH_SUCCESS);
				}
				else {
					init_packet(p2);
					p2->c = c;
					if (slfd > 0)
						fprint(slfd, "ssh public key login failure for %s\n", user);
					else
						syslog(1, "ssh", "ssh public key login failure for %s", user);
					add_byte(p2, SSH_MSG_USERAUTH_FAILURE);
					add_string(p2, "password,publickey");
					add_byte(p2, 0);
					retval = -1;
				}
			}
		}
	}
	else if (strcmp(method, "password") == 0) {
		++q;
		get_string(p, q, pw, 128, nil);
		if (debug > 1)
			fprint(2, "%s\n", pw);
		ai = nil;
		if (kflag) {
			if (passtokey(key1, pw) == 0)
				goto answer;
			snprint(path, 128, "/mnt/keys/%s/key", user);
			if ((fd = open(path, OREAD)) < 0) {
				werrstr("Invalid user");
				goto answer;
			}
			if (read(fd, key2, DESKEYLEN) != DESKEYLEN) {
				werrstr("Password mismatch");
				goto answer;
			}
			close(fd);
			if (memcmp(key1, key2, DESKEYLEN) != 0) {
				werrstr("Password mismatch");
				goto answer;
			}
			ai = emalloc9p(sizeof(AuthInfo));
			ai->cuid = estrdup9p(user);
			ai->suid = estrdup9p(me);
			ai->cap = mkcap(me, user);
			ai->nsecret = 0;
			ai->secret = (uchar *)estrdup9p("");
		}
		else
			ai = auth_userpasswd(user, pw);
answer:
		if (ai == nil) {
			if (debug)
				fprint(2, "Auth error: %r\n");
			if (slfd > 0)
				fprint(slfd, "ssh login failure for %s: %r\n", user);
			else
				syslog(1, "ssh", "ssh login failure for %s: %r", user);
			add_byte(p2, SSH_MSG_USERAUTH_FAILURE);
			add_string(p2, "password,publickey");
			add_byte(p2, 0);
			retval = -1;
		}
		else{
			if (debug)
				fprint(2, "Auth successful: cuid is %s suid is %s cap is %s\n", ai->cuid, ai->suid, ai->cap);
			if (c->cap != nil)
				free(c->cap);
			if (strcmp(user, me) == 0)
				c->cap = estrdup9p("n/a");
			else
				c->cap = estrdup9p(ai->cap);
			if (slfd > 0)
				fprint(slfd, "ssh logged in as %s\n", user);
			else
				syslog(1, "ssh", "ssh logged in as %s", user);
			add_byte(p2, SSH_MSG_USERAUTH_SUCCESS);
			auth_freeAI(ai);
		}
	}
	else {
		add_byte(p2, SSH_MSG_USERAUTH_FAILURE);
		add_string(p2, "password,publickey");
		add_byte(p2, 0);
		retval = -1;
	}
	n = finish_packet(p2);
	iowrite(c->dio, c->datafd, p2->nlength, n);

	free(service);
	free(me);
	free(user);
	free(pw);
	free(alg);
	free(blob);
	free(sig);
	free(path);
	free(p2);
	return retval;
}

int
client_auth(Conn *c, Ioproc *io)
{
	Packet *p2, *p3, *p4;
	char *r, *s;
	mpint *ek, *nk;
	int i, n;

	if (!c->password && !c->authkey)
		return -1;
	p2 = new_packet(c);
	add_byte(p2, SSH_MSG_USERAUTH_REQUEST);
	add_string(p2, c->user);
	add_string(p2, c->service);
	if (c->password) {
		add_string(p2, "password");
		add_byte(p2, 0);
		add_string(p2, c->password);
	}
	else {
		add_string(p2, "publickey");
		add_byte(p2, 1);
		add_string(p2, "ssh-rsa");

		r = strstr(c->authkey, " ek=");
		s = strstr(c->authkey, " n=");
		if (!r || !s) {
			shutdown(c);
			free(p2);
			return -1;
		}
		ek = strtomp(r+4, nil, 16, nil);
		nk = strtomp(s+3, nil, 16, nil);

		p3 = new_packet(c);
		add_string(p3, "ssh-rsa");
		add_mp(p3, ek);
		add_mp(p3, nk);
		add_block(p2, p3->payload, p3->rlength-1);

		p4 = new_packet(c);
		add_block(p4, c->sessid, SHA1dlen);
		add_byte(p4, SSH_MSG_USERAUTH_REQUEST);
		add_string(p4, c->user);
		add_string(p4, c->service);
		add_string(p4, "publickey");
		add_byte(p4, 1);
		add_string(p4, "ssh-rsa");
		add_block(p4, p3->payload, p3->rlength-1);
		mpfree(ek);
		mpfree(nk);
		free(p3);

		for (i = 0; pkas[i] && strcmp("ssh-rsa", pkas[i]->name); ++i) ;
		if ((p3 = pkas[i]->sign(c, p4->payload, p4->rlength-1)) == nil) {
			free(p4);
			free(p2);
			return -1;
		}
		add_block(p2, p3->payload, p3->rlength-1);
		free(p3);
		free(p4);
	}
	n = finish_packet(p2);
	if (io)
		iowrite(io, c->datafd, p2->nlength, n);
	else
		write(c->datafd, p2->nlength, n);
	free(p2);
	return 0;
}

char *
factlookup(int nattr, int nreq, char *attrs[])
{
	Biobuf *bp;
	char *buf, *toks[32], *res, *q;
	int ntok, nmatch, maxmatch;
	int i, j;

	res = nil;
	bp = Bopen("/mnt/factotum/ctl", OREAD);
	if (bp == nil)
		return nil;
	maxmatch = 0;
	while (buf = Brdstr(bp, '\n', 1)) {
		q = estrdup9p(buf);
		ntok = gettokens(buf, toks, 32, " ");
		nmatch = 0;
		for (i = 0; i < nattr; ++i) {
			for (j = 0; j < ntok; ++j) {
				if (strcmp(attrs[i], toks[j]) == 0) {
					++nmatch;
					break;
				}
			}
			if (i < nreq && j >= ntok)
				break;
		}
		if (i >= nattr && nmatch > maxmatch) {
			free(res);
			res = q;
			maxmatch = nmatch;
		}
		else
			free(q);
		free(buf);
	}
	Bterm(bp);
	return res;
}

void
shutdown(Conn *c)
{
	Plist *p;
	SSHChan *sc;
	int i, ostate;

	if (debug)
		fprint(2, "Shutting down connection %d\n", c->id);
	ostate = c->state;
	if (c->clonefile->ref <= 2 && c->ctlfile->ref <= 2 && c->datafile->ref <= 2
			&& c->listenfile->ref <= 2 && c->localfile->ref <= 2
			&& c->remotefile->ref <= 2 && c->statusfile->ref <= 2)
		c->state = Closed;
	else {
		if (c->state != Closed)
			c->state = Closing;
		if (debug)
			fprint(2, "clone %ld ctl %ld data %ld listen %ld local %ld remote %ld status %ld\n",
				c->clonefile->ref, c->ctlfile->ref, c->datafile->ref, c->listenfile->ref, c->localfile->ref,
				c->remotefile->ref, c->statusfile->ref);
	}
	if (ostate == Closed || ostate == Closing) {
		c->state = Closed;
		return;
	}
	if (c->role == Server && c->remote) {
		if (slfd > 0)
			fprint(slfd, "closing ssh connection from %s\n", c->remote);
		else
			syslog(1, "ssh", "closing ssh connection from %s", c->remote);
	}
	fprint(c->ctlfd, "hangup");
	close(c->ctlfd);
	close(c->datafd);
	if (c->dio) {
		closeioproc(c->dio);
		c->dio = nil;
	}
	c->decrypt = -1;
	c->inmac = -1;
	c->ctlfd = -1;
	c->datafd = -1;
	c->nchan = 0;
	free(c->otherid);
	if (c->s2ccs) {
		free(c->s2ccs);
		c->s2ccs = nil;
	}
	if (c->c2scs) {
		free(c->c2scs);
		c->c2scs = nil;
	}
	if (c->remote) {
		free(c->remote);
		c->remote = nil;
	}
	if (c->x) {
		mpfree(c->x);
		c->x = nil;
	}
	if (c->e) {
		mpfree(c->e);
		c->e = nil;
	}
	if (c->user) {
		free(c->user);
		c->user = nil;
	}
	if (c->service) {
		free(c->service);
		c->service = nil;
	}
	c->otherid = nil;
	qlock(&c->l);
	rwakeupall(&c->r);
	qunlock(&c->l);
	for (i = 0; i < MAXCONN; ++i) {
		sc = c->chans[i];
		if (sc == nil)
			continue;
		if (sc->ann) {
			free(sc->ann);
			sc->ann = nil;
		}
		if (sc->state != Empty && sc->state != Closed) {
			sc->state = Closed;
			sc->lreq = nil;
			while (sc->dataq != nil) {
				p = sc->dataq;
				sc->dataq = p->next;
				free(p->pack);
				free(p);
			}
			while (sc->reqq != nil) {
				p = sc->reqq;
				sc->reqq = p->next;
				free(p->pack);
				free(p);
			}
			qlock(&c->l);
			rwakeupall(&sc->r);
			nbsendul(sc->inchan, 1);
			nbsendul(sc->reqchan, 1);
			chanclose(sc->inchan);
			chanclose(sc->reqchan);
			qunlock(&c->l);
		}
	}
	qlock(&availlck);
	rwakeup(&availrend);
	qunlock(&availlck);
	if (debug)
		fprint(2, "Done processing shutdown of connection %d\n", c->id);
}
