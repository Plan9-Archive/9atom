#include <u.h>
#include <libc.h>
#include <ip.h>

#include "boot.h"

static	uchar	fsip[IPaddrlen];
	uchar	auip[IPaddrlen];
static	char	mpoint[32];

static int isvalidip(uchar*);
static void netndb(char*, uchar*);
static void netenv(char*, uchar*);

void
configip(int bargc, char **bargv, int needfs)
{
	Waitmsg *w;
	int argc, pid;
	char **arg, **argv, buf[32], *p;
	char buf1[32];

	fmtinstall('I', eipfmt);
	fmtinstall('M', eipfmt);
	fmtinstall('E', eipfmt);

	arg = malloc((bargc+1) * sizeof(char*));
	if(arg == nil)
		fatal("%r");
	memmove(arg, bargv, bargc * sizeof(char*));
	arg[bargc] = 0;

	argc = bargc;
	argv = arg;
	strcpy(mpoint, "/net");
	ARGBEGIN {
	case 'x':
		p = ARGF();
		if(p != nil)
			snprint(mpoint, sizeof(mpoint), "/net%s", p);
		break;
	case 'g':
	case 'b':
	case 'h':
	case 'm':
		p = ARGF();
		USED(p);
		break;
	} ARGEND;

	/* bind in an ip interface */
	bind("#I", mpoint, MAFTER);
	bind("#l0", mpoint, MAFTER);
	bind("#l1", mpoint, MAFTER);
	bind("#l2", mpoint, MAFTER);
	bind("#l3", mpoint, MAFTER);
	bind("#©", "/dev", MBEFORE);
	writefile("/dev/cecctl", "cecon #l0/ether0", 16);
	bind("#æ", "/dev", MAFTER);
	werrstr("");

	/* let ipconfig configure the ip interface */
	switch(pid = fork()){
	case -1:
		fatal("configuring ip: %r");
	case 0:
		exec("/boot/ipconfig", arg);
		fatal("execing /ipconfig");
	default:
		break;
	}

	/* wait for ipconfig to finish */
	for(;;){
		w = wait();
		if(w != nil && w->pid == pid){
			if(w->msg[0] != 0)
				fatal(w->msg);
			free(w);
			break;
		} else if(w == nil)
			fatal("configuring ip");
		free(w);
	}

	readfile("#c/sysname", buf, sizeof buf);
	if(buf[0] != 0){
		snprint(buf1, sizeof buf1, "name %s\n", buf);
		writefile("#©/cecctl", buf1, sizeof buf1);
	}

	if(!needfs)
		return;

	/* if we didn't get a file and auth server, query user */
	netndb("fs", fsip);
	if(!isvalidip(fsip))
		netenv("fs", fsip);
	while(!isvalidip(fsip)){
		buf[0] = 0;
		outin("filesystem IP address", buf, sizeof(buf));
		if (parseip(fsip, buf) == -1)
			fprint(2, "configip: can't parse fs ip %s\n", buf);
	}

	netndb("auth", auip);
	if(!isvalidip(auip))
		netenv("auth", auip);
	while(!isvalidip(auip)){
		buf[0] = 0;
		outin("authentication server IP address", buf, sizeof(buf));
		if (parseip(auip, buf) == -1)
			fprint(2, "configip: can't parse auth ip %s\n", buf);
	}
}

static void
setauthaddr(char *proto, int port)
{
	char buf[128];

	snprint(buf, sizeof buf, "%s!%I!%d", proto, auip, port);
	authaddr = strdup(buf);
}

void
configtcp(Method*)
{
	configip(bargc, bargv, 1);
	setauthaddr("tcp", 567);
}

int
connecttcp(void)
{
	char buf[64];
	int fd;

	snprint(buf, sizeof buf, "tcp!%I!564", fsip);
	fd = dial(buf, 0, 0, 0);
	if (fd < 0)
		werrstr("dial %s: %r", buf);
	return fd;
}

void
configil(Method*)
{
	configip(bargc, bargv, 1);
	setauthaddr("tcp", 567);
}

int
connectil(void)
{
	char buf[64];

	snprint(buf, sizeof buf, "il!%I!17008", fsip);
	return dial(buf, 0, 0, 0);
}

static int
isvalidip(uchar *ip)
{
	if(ipcmp(ip, IPnoaddr) == 0)
		return 0;
	if(ipcmp(ip, v4prefix) == 0)
		return 0;
	return 1;
}

static void
netenv(char *attr, uchar *ip)
{
	int fd, n;
	char buf[128];

	ipmove(ip, IPnoaddr);
	snprint(buf, sizeof(buf), "#e/%s", attr);
	fd = open(buf, OREAD);
	if(fd < 0)
		return;

	n = read(fd, buf, sizeof(buf)-1);
	if(n <= 0)
		return;
	buf[n] = 0;
	if (parseip(ip, buf) == -1)
		fprint(2, "netenv: can't parse ip %s\n", buf);
}

static void
netndb(char *attr, uchar *ip)
{
	int fd, n, c;
	char buf[1024];
	char *p;

	ipmove(ip, IPnoaddr);
	snprint(buf, sizeof(buf), "%s/ndb", mpoint);
	fd = open(buf, OREAD);
	if(fd < 0)
		return;
	n = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if(n <= 0)
		return;
	buf[n] = 0;
	n = strlen(attr);
	for(p = buf; ; p++){
		p = strstr(p, attr);
		if(p == nil)
			break;
		c = *(p-1);
		if(*(p + n) == '=' && (p == buf || c == '\n' || c == ' ' || c == '\t')){
			p += n+1;
			if (parseip(ip, p) == -1)
				fprint(2, "netndb: can't parse ip %s\n", p);
			return;
		}
	}
	return;
}
