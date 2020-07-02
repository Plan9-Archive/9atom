#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include "dns.h"
#include "ip.h"

/*
 *  convert address into a reverse lookup address
 */
static char*
mkptrname(char *p, char *e)
{
	uchar a[IPaddrlen];
	int i;

	if(strstr(p, "in-addr.arpa") || strstr(p, "IN-ADDR.ARPA")
	|| strstr(p, "ip6.arpa") || strstr(p, "IP6.ARPA")
	|| parseip(a, p) == -1)
		return p;
	if(isv4(a))
		p = seprint(p, e, "%ud.%ud.%ud.%ud.in-addr.arpa ptr",
			a[15], a[14], a[13], a[12]);
	else{
		for(i = 15; i >= 0; i--){
			p = seprint(p, e, "%ux.", a[i]&0xf);
			p = seprint(p, e, "%ux.", a[i]>>4);
		}
		p = seprint(p, e, "ip6.arpa ptr");
	}
	*p = 0;
	return p;
}

int
doquery(int fd, char *line, char *p, char *e)
{
	char buf[1024], *type;
	int n;

	/* default to an "ip" request if alpha, "ptr" if numeric */
	
	if((type = strchr(line, ' ')) == nil){
		type = p+1;
		if(strcmp(ipattr(line), "ip") == 0)
			p = seprint(p, e, " ptr");
		else
			p = seprint(p, e, " ip");
	}else
		type++;

	/* inverse queries may need to be permuted */
	if(strcmp(type, "ptr") == 0)
		p = mkptrname(line, e);

	seek(fd, 0, 0);
	if(write(fd, line, p-line) < 0) {
		print("!%r\n");
		return -1;
	}
	seek(fd, 0, 0);
	while((n = read(fd, buf, sizeof(buf))) > 0){
		buf[n] = 0;
		print("%s\n", buf);
	}
	return 0;
}

void
main(int argc, char *argv[])
{
	int fd, rv, n, domount;
	Biobuf in;
	char line[1024], *lp, *p, *e, *net, mtpt[40], srv[40], dns[40];

	net = nil;
	domount = 1;
	ARGBEGIN {
	case 'x':
		net = ARGF();
		if(net == nil){
			/* temporary compatability */
			fprint(2, "-x needs argument; assuming net.alt\n");
			net = "/net.alt";
		}
		break;
	default:
		fprint(2, "usage: %s [-x netmtpt]\n", argv0);
		exits("usage");
	} ARGEND;
	setnetmtpt(mtpt, sizeof mtpt, net);
	snprint(dns, sizeof dns, "%s/dns", mtpt);
	snprint(srv, sizeof srv, "/srv/dns_%s", mtpt+1);
	for(n = strlen(srv); n>0 && srv[-1] == '/'; )
		srv[--n] = 0;
	if(strcmp(srv, "/srv/dns_net") == 0)
		srv[8] = 0;

	fd = open(dns, ORDWR);
	if(fd < 0){
		if(domount == 0){
			fprint(2, "can't open %s: %r\n", mtpt);
			exits(0);
		}
		fd = open(srv, ORDWR);
		if(fd < 0){
			print("can't open %s: %r\n", srv);
			exits(0);
		}
		if(mount(fd, -1, mtpt, MBEFORE, "") < 0){
			print("can't mount(%s, %s): %r\n", srv, mtpt);
			exits(0);
		}
		fd = open(dns, ORDWR);
		if(fd < 0){
			print("can't open %s: %r\n", mtpt);
			exits(0);
		}
	}

	rv = 0;
	if(argc > 0){
		p = line;
		e = line+sizeof line;
		while(--argc)
			p = seprint(p, e, "%s ", *argv++);
		p = seprint(p, e, "%s", *argv);
		*p = 0;
		rv |= doquery(fd, line, p, line+sizeof line);
	}else{	
		Binit(&in, 0, OREAD);
		for(fprint(2, "> "); lp = Brdline(&in, '\n'); fprint(2, "> ")){
			n = Blinelen(&in)-1;
			strncpy(line, lp, n);
			line[n] = 0;
			if (n<=1)
				continue;
			rv |= doquery(fd, line, line+n, line+sizeof line);
		}
	}
	close(fd);
	exits(rv ? "fail" : 0);
}
