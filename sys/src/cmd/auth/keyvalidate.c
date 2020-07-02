#include <u.h>
#include <libc.h>
#include <authsrv.h>
#include <fcall.h>
#include <bio.h>
#include <mp.h>
#include <libsec.h>
#include "authcmdlib.h"

typedef struct User User;
struct User{
	char	*name;
	char	key[DESKEYLEN];
	char	secret[SECRETLEN];
	ulong	expire;			/* 0 == never */
	uchar	status;
	ulong	bad;			/* number of consecutive bad authentication attempts */
	int	ref;
	char	removed;
	uchar	warnings;
	long	purgatory;		/* time purgatory ends */
	ulong	uniq;
	User	*link;
};

enum{
	Namelen	= ANAMELEN,
};

enum{
	Sok,
	Sdisabled,
	Smax,
};

char authkey[8];
char *userkeys;

#pragma	varargck	type	"W"	char*

int
wierdfmt(Fmt *f)
{
	char *s, buf[ANAMELEN*4+1];
	int i, j, n;
	Rune r;

	s = va_arg(f->args, char*);

	j = 0;
	for(i = 0; i < ANAMELEN; i += n){
		n = chartorune(&r, s+i);
		if(r == Runeerror)
			j += sprint(buf+j, "[%.2x]", buf[i]);
		else if(r < ' ')
			j += sprint(buf+j, "[%.2x]", r);
		else if(r == ' ' || r == '/')
			j += sprint(buf+j, "[%c]", r);
		else
			j += sprint(buf+j, "%C", r);
	}
	return fmtstrcpy(f, buf);
}

int
checkuser(char *user, int nu)
{
	char buf[ANAMELEN+1];
	int i, n, rv;
	Rune r;

	memset(buf, 0, sizeof buf);
	memcpy(buf, user, ANAMELEN);

	if(buf[ANAMELEN-1] != 0){
		fprint(2, "name %d no termination: %W\n", nu, buf);
		return -1;
	}

	rv = 0;
	for(i = 0; buf[i]; i += n){
		n = chartorune(&r, buf+i);
		if(r == Runeerror){
//			fprint(2, "name %W bad rune byte %d\n", buf, i);
			rv = -1;
		}
		if(r <= ' ' || r == '/'){
//			fprint(2, "name %W bad char %C\n", buf, r);
			rv = -1;
		}
	}

	if(i == 0){
		fprint(2, "nil name\n");
		return -1;
	}
	if(rv == -1)
		fprint(2, "name %d bad: %W\n", nu, buf);
	return rv;
}

void
printuser(User *u, int nu)
{
	print("%d: %.28s %ux %ux %.11lud\n", nu, u->name, u->status, u->warnings, u->expire); 
}

void
oldCBCdecrypt(char *key7, uchar *p, int len)
{
	uchar ivec[8];
	uchar key[8];
	DESstate s;

	memset(ivec, 0, 8);
	des56to64((uchar*)key7, key);
	setupDESstate(&s, key, ivec);
	desCBCdecrypt((uchar*)p, len, &s);

}

User U;

int
readusers(void)
{
	int fd, i, n, nu;
	uchar *p, *buf, *ep;
	User *u;
	Dir *d;

	/* read file into an array */
	fd = open(userkeys, OREAD);
	if(fd < 0)
		return 0;
	d = dirfstat(fd);
	if(d == nil){
		fprint(2, "dirfstat: %r\n");
		close(fd);
		return 0;
	}
	buf = malloc(d->length);
	if(buf == 0){
		fprint(2, "malloc: %r\n");
		close(fd);
		free(d);
		return 0;
	}
	n = readn(fd, buf, d->length);
	close(fd);
	free(d);
	if(n != d->length){
		fprint(2, "readn: %r\n");
		free(buf);
		return 0;
	}

	/* decrypt */
	n -= n % KEYDBLEN;
	oldCBCdecrypt(authkey, buf, n);

	/* unpack */
	nu = 0;
	for(i = KEYDBOFF; i < n; i += KEYDBLEN){
		ep = buf + i;
		if(checkuser((char*)ep, i/KEYDBLEN))
			continue;
u = &U;
memset(u, 0, sizeof *u);
u->name = (char*)ep;
//		u = finduser((char*)ep);
//		if(u == 0)
//			u = installuser((char*)ep);
		memmove(u->key, ep + Namelen, DESKEYLEN);
		p = ep + Namelen + DESKEYLEN;
		u->status = *p++;
		u->warnings = *p++;
		if(u->status >= Smax)
			fprint(2, "keyfs: warning: bad status in key file\n");
		u->expire = p[0] + (p[1]<<8) + (p[2]<<16) + (p[3]<<24);
		p += 4;
		memmove(u->secret, p, SECRETLEN);
		u->secret[SECRETLEN-1] = 0;
		nu++;
printuser(u, i/KEYDBLEN);
	}
	free(buf);

	print("%d keys read\n", nu);
	return 1;
}

void
usage(void)
{
	fprint(2, "usage: keyvalidate [-k keyfile] [-p]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	int p[2], usepass;

	userkeys = "/adm/keys";
	usepass = 0;
	ARGBEGIN{
	case 'k':
		userkeys = EARGF(usage());
		break;
	case 'p':
		usepass = 1;
		break;
	}ARGEND

	userkeys = "/adm/keys";
	if(argc > 0)
		userkeys = argv[0];

	if(usepass)
		getpass(authkey, nil, 0, 0);
	else{
		if(!getauthkey(authkey))
			print("keyfs: warning: can't read /dev/key\n");
	}

	fmtinstall('W', wierdfmt);
	readusers();
	exits("");
}
