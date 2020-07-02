/*
 * Syslogd
 * Nigel Roles (nigel@9fs.org) 24/7/2000
 * Steve Simon 26/11/2004
 */
#include <u.h>
#include <libc.h>
#include <ip.h>

#pragma varargck type "U" Udphdr*

char *facname[] = {
[0]	 "kernel",
[1]	 "user",
[2]	 "mail",
[3]	 "daemon",
[4]	 "auth",
[5]	 "syslog",
[6]	 "lpr",
[7]	 "news",
[8]	 "uucp",
[9]	 "cron",
[10]	 "authpriv",

[16]	 "sr",
[17]	 "local1",
[18]	 "local2",
[19]	 "local3",
[20]	 "local4",
[21]	 "local5",
[22]	 "local6",
[23]	 "local7",
};

char *priname[] = {
[0]	"emergency",
[1]	"alert",
[2]	"critical",
[3]	"error",
[4]	"warning",
[5]	"notice",
[6]	"info",
[7]	"debug",
};

int
udphdrfmt(Fmt *f)
{
	int n;
	Udphdr *h;

	h = va_arg(f->args, Udphdr*);
	if(h == nil)
		n = fmtprint(f, "(nil udphdr)");
	else
		n = fmtprint(f, "udp!%I!%ud", h->raddr, h->rport[0]<<8 | h->rport[1]);
	return n;
}

void 
usage(void)
{
	fprint(2, "usage: syslog [-x net]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *p, *q, mntpt[32], lognam[32], data[64], devdir[40 + 1], buf[8192];
	int ctl, netfd, nb;
	uint pri, fac;

	setnetmtpt(mntpt, sizeof mntpt, nil);
	ARGBEGIN{
	case 'x':
		setnetmtpt(mntpt, sizeof mntpt, EARGF(usage()));
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(argc != 0)
		usage();
	fmtinstall('I', eipfmt);
	fmtinstall('U', udphdrfmt);

	ctl = announce(netmkaddr("*", "udp", "syslog"), devdir);
	if(ctl == -1)
		sysfatal("announce: %r");

	if(fprint(ctl, "headers") == -1)
		sysfatal("udp headers: %r");

	snprint(data, sizeof data, "%s/data", devdir);
	netfd = open(data, ORDWR);
	if(netfd == -1)
		sysfatal("open: %r");
	close(ctl);

	switch(fork()){
	case 0:
		exits(0);
	case -1:
		sysfatal("fork: %r\n");
	}

	for(;;){
		nb = read(netfd, buf, sizeof buf);
		if(nb < 0)
			break;
		while(nb > 0 && buf[nb - 1] == '\n')
			nb--;
		if(nb == 0)
			continue;
		buf[nb] = 0;
		p = buf + Udphdrsize;
		pri = 0;
		if (*p == '<') {
			p++;
			pri = strtol(p, &p, 10);
			if(*p == '>')
				p++;
		}
		fac = pri >> 3;
		pri = pri & 7;

		q = "other";
		if (fac < nelem(facname) && facname[fac] != nil)
			q = facname[fac];
		snprint(lognam, sizeof lognam, "syslog.%s", q);
		syslog(0, lognam, "%U %s %s", (Udphdr*)buf, priname[pri], p);
	}
	syslog(1, facname[0], "read: %r");
	exits("read");
}
