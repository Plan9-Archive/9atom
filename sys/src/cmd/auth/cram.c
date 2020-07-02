#include <u.h>
#include <libc.h>
#include <authsrv.h>
#include <auth.h>
#include <bio.h>
#include "authcmdlib.h"

char	flagd;
char	flag6;		/* force base64 output */
char	flagn;

int
trim(char *s, int n)
{
	if(s[0] == '+' && s[1] == ' ')
		memmove(s, s + 2, n -= 2);
	else if(!strncmp(s, "334 ", 4))
		memmove(s, s + 4, n -= 4);
	if(n > 0 && s[n - 1] == '\r')
		s[--n] = 0;
	if(n > 0 && s[n - 1] == '\n')
		s[--n] = 0;
	return n;
}

int
decode(char *s, int n, int *base64)
{
	char ch[128];

	n = trim(s, n);
	if(strchr(s, '<') != 0)
		return n;
	*base64 = 1;
	n = dec64((uchar*)ch, sizeof ch, s, n);
	if(n == -1)
		sysfatal("dec64: buffer too small");
	memcpy(s, ch, n);
	s[n] = 0;
	return n;
}

void
usage(void)
{
	fprint(2, "usage: cram [-d6] [-u user] [-s server]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char buf[128], usr[64], rbuf[128], ubuf[128], ebuf[192], arbuf[128];
	char *user, *server, *p, *e;
	int n, l, i, base64;

	quotefmtinstall();
	fmtinstall('[', encodefmt);
	user = getuser();
	server = "cramtemp";

	ARGBEGIN{
	case 'd':
		flagd++;
		break;
	case '6':
		flag6++;
		break;
	case 's':
		server = EARGF(usage());
		break;
	case 'u':
		user = EARGF(usage());
		break;
	case 'n':
		flagn++;
		break;
	default:
		usage();
	}ARGEND
	if(argc)
		usage();

	for(;;){
		fprint(2, "challenge: ");
		n = read(0, buf, sizeof buf - 1);
		if(n <= 0)
			exits(0);
		buf[n] = '\0';
		base64 = flag6;
		n = decode(buf, n, &base64);
		if(flagd && base64)
			fprint(2, "raw ch [%s]\n", buf);
		e = arbuf + sizeof arbuf;
		p = seprint(arbuf, e, "proto=cram role=client server=%q", server);
		if(!flagn)
			seprint(p, e, " user=%q", user);
		n = auth_respond(buf, n, usr, sizeof usr, rbuf, sizeof rbuf, auth_getkey, "%s", arbuf);
		if(n == -1)
			sysfatal("auth_respond: %r");
		for(i = 0; i < n; i++)
			if(rbuf[i] >= 'A' && rbuf[i] <= 'Z')
				rbuf[i] += 'a' - 'A';
		l = snprint(ubuf, sizeof ubuf, "%s %.*s", usr, n, rbuf);
		snprint(ebuf, sizeof ebuf, "%.*[", l, ubuf);
		if(flagd && base64)
			fprint(2, "raw cram [%s]\n", ubuf);
		if(base64)
			print("%s\n", ebuf);
		else
			print("%s\n", ubuf);
	}
}
