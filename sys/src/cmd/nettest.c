#include <u.h>
#include <libc.h>

char	*service	= "17766";
char	*laddr	= "*";
char	*raddr;

char	*hash	= "clear";
char	*enc	= "clear";
char	*secret	= 
		"01234567890abcdef01234567890abcdef01234567890abcdef01234567890abcdef"
		"01234567890abcdef01234567890abcdef01234567890abcdef01234567890abcdef"
		"01234567890abcdef01234567890abcdef01234567890abcdef01234567890abcdef";


uchar	flag[127];
ulong	n;
long	sleepms;
vlong	sleepns;

#define dprint(...) {if(flag['d']) print(__VA_ARGS__);}

enum {
	Blen = 8*1024,
};

void
usage(void)
{
	fprint(2, "usage: %s [-l] [-t [-h halg -e encalg]] [-s sleepms] [-n nblocks]\n"
		"	%s  [-t [-h halg -e encalg]] [-a addr] [-s sleepms]\n"
		"	%s  [-t [-h halg -e encalg]] -r addr -n nblocks\n", argv0, argv0, argv0);
	exits("usage");
}

void
die(void)
{
	sysfatal("%s: %r", argv0);
}

int
notef(void*, char *note)
{
	if(strstr(note, "alarm") != nil)
		return 1;
	return 0;
}

char*
·netmkaddr(char *linear, char *net, char *service)
{
	char *r;
	static QLock stupid;

	qlock(&stupid);		/* protect stupid static memory */
	r = netmkaddr(linear, net, service);
	if(r != nil)
		r = strdup(r);
	qunlock(&stupid);

	return r;
}

void
client(void)
{
	char *dst, b[Blen];
	int fd, i;

	atnotify(notef, 1);
	sleep(1);
	dprint("client\n");
	dst = ·netmkaddr(raddr, "tcp", service);
	alarm(3*1000);
	if((fd = dial(dst, 0, 0, 0)) < 0)
		die();
	alarm(0);
	free(dst);
	dprint("dialed\n");
	if(flag['t']){
		fd = pushtls(fd, hash, enc, 1, secret, nil);
		if(fd == -1)
			sysfatal("client: pushtls %r");
	}
	for(i = 0; i < n; ++i){
		if(write(fd, b, sizeof b) < 0)
			die();
	}
	close(fd);
	dprint("wrote\n");
}
				
void
server(void)
{
	int acfd, lcfd, dfd;
	char adir[40], ldir[40], b[Blen], *a;
	long c;
	uvlong i, t;
	double ratemb, maxr, delta;
	NetConnInfo *nc;

	dprint("server\n");
	a = ·netmkaddr(laddr, "tcp", service);
	acfd = announce(a, adir);
	if(acfd == -1)
		die();
	free(a);

	dprint("listen\n");
	if((lcfd = listen(adir, ldir)) < 0)
		die();
	dprint("listened\n");
	if((dfd = accept(lcfd, ldir)) < 0)
		die();
	nc = getnetconninfo(ldir, dfd);
	dprint("accepted\n");
	if(flag['t']){
		dfd = pushtls(dfd, hash, enc, 0, secret, nil);
		if(dfd == -1)
			sysfatal("server: pushtls: %r");
	}
	t = -nsec();
	for(i = 0;; i++){
		c = readn(dfd, b, sizeof b);
		if(c != sizeof b)
			break;
		if(sleepms)
			sleep(sleepms);
	}
	t += nsec();
	close(dfd);
	close(lcfd);

	print("%s count %llud; ", nc? nc->raddr: "(unknown)", i);
	free(nc);
	i *= Blen;
	delta = t/1e9;
	ratemb = i/delta/1024/1024;
	if(sleepms > 0){
		maxr = Blen*1000. / sleepms / 1e6;
		print("%llud bytes in %g s @ %.2g MB/s (%ldms; limit %.2g MB/s)\n", i, delta, ratemb, sleepms, maxr);
	}
	else
		print("%llud bytes in %g s @ %.2g MB/s (0ms)\n", i, delta, ratemb);
}

void
main(int argc, char **argv)
{
	ARGBEGIN{
	case 'r':
		raddr = EARGF(usage());
		break;
	case 'a':
		laddr = EARGF(usage());
		break;
	case 'n':
		n = strtoul(EARGF(usage()), nil, 0);
		break;
	case 's':
		sleepms = strtoul(EARGF(usage()), nil, 0);
		break;
	case 'd':
	case 'l':
	case 't':
		flag[ARGC()] = 1;
		break;
	case 'h':
		hash = EARGF(usage());
		break;
	case 'e':
		enc = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();

	if(flag['l']){
		/* loopback */
		raddr = getenv("sysname");
		if(raddr == nil)
			sysfatal("%s: can't find my address", argv0);
		switch(fork()){
		case -1:
			die();
			break;
		case 0:
			server();
			break;
		default:
			client();
			wait();
			break;
		}
	}else if(raddr != nil)
		client();
	else
		server();
	exits(nil);
}
