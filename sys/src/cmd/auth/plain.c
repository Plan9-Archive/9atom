#include <u.h>
#include <libc.h>
#include <authsrv.h>
#include <auth.h>
#include <bio.h>
#include "authcmdlib.h"

char	flagd;
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
	char arbuf[128], buf[1024], ebuf[2048];
	char *user, *server, *p, *e;
	int n;
	UserPasswd *up;

	quotefmtinstall();
	fmtinstall('[', encodefmt);
	user = getuser();
	server = "cramtemp";

	ARGBEGIN{
	case 'd':
		flagd++;
		break;
	case '6':
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

	e = arbuf + sizeof arbuf;
	p = seprint(arbuf, e, "proto=pass role=client server=%q", server);
	if(!flagn)
		seprint(p, e, " user=%q", user);
	up = auth_getuserpasswd(auth_getkey, "%s", arbuf);
	if(!up)
		sysfatal("auth_respond: %r");
	n = snprint(buf, sizeof buf, "%c%s%c%s", 0, up->user, 0, up->passwd);
	enc64(ebuf, sizeof ebuf, (uchar *)buf, n);
	print("%s\n", ebuf);

	exits("");
}
