#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>
#include <bio.h>

void
usage(void)
{
	fprint(2, "usage: dsasign [-i id] [-v] key <data\n");
	exits("usage");
}

static void doverify(void);
static char *getline(int*);

char *id;
Biobuf b;
int nid;
char *key;

void
main(int argc, char **argv)
{
	int n, verify, afd;
	char *text, *p;
	uchar digest[SHA1dlen];
	AuthRpc *rpc;
	Fmt fmt;
	
	fmtinstall('[', encodefmt);
	fmtinstall('H', encodefmt);
	
	verify = 0;
	id = "";
	ARGBEGIN{
	case 'i':
		id = EARGF(usage());
		break;
	case 'v':
		verify = 1;
		break;
	default:
		usage();
	}ARGEND
	
	if(argc != 1)
		usage();
	key = argv[0];
	nid = strlen(id);

	Binit(&b, 0, OREAD);
	if(verify) {
		doverify();
		exits(nil);
	}

	if((afd = open("/mnt/factotum/rpc", ORDWR)) < 0)
		sysfatal("open /mnt/factotum/rpc: %r");

	if((rpc = auth_allocrpc(afd)) == nil){
		fprint(2, "dsasign: auth_allocrpc: %r\n");
		exits("rpc");
	}
	key = smprint("proto=dsa role=sign %s", key);
	if(auth_rpc(rpc, "start", key, strlen(key)) != ARok){
		fprint(2, "dsasign: auth 'start' failed: %r\n");
		auth_freerpc(rpc);
		exits("rpc");
	}
	
	print("+%s\n", id);

	Binit(&b, 0, OREAD);
	fmtstrinit(&fmt);
	while((p = getline(&n)) != nil) {
		if(p[0] == '-' || p[0] == '+')
			print("+");
		print("%s\n", p);
		fmtprint(&fmt, "%s\n", p);
	}
	text = fmtstrflush(&fmt);
	sha1((uchar*)text, strlen(text), digest, nil);

	if(auth_rpc(rpc, "write", digest, SHA1dlen) != ARok)
		sysfatal("auth write in sign failed: %r");
	if(auth_rpc(rpc, "read", nil, 0) != ARok)
		sysfatal("auth read in sign failed: %r");

	print("-%s %.*H\n", id, rpc->narg, rpc->arg);
	exits(nil);
}

static mpint*
keytomp(Attr *a, char *name)
{
	char *p;
	mpint *m;
	
	p = _strfindattr(a, name);
	if(p == nil)
		sysfatal("missing key attribute %s", name);
	m = strtomp(p, nil, 16, nil);
	if(m == nil)
		sysfatal("malformed key attribute %s=%s", name, p);
	return m;
}

static void
doverify(void)
{
	char *p, *text;
	uchar digest[SHA1dlen], sig[1024];
	int n;
	Fmt fmt;
	Attr *a;
	DSAsig dsig;
	DSApub dkey;
	
	a = _parseattr(key);
	if(a == nil)
		sysfatal("invalid key");
	dkey.alpha = keytomp(a, "alpha");
	dkey.key = keytomp(a, "key");
	dkey.p = keytomp(a, "p");
	dkey.q = keytomp(a, "q");
	if(!probably_prime(dkey.p, 20) && !probably_prime(dkey.q, 20))
		sysfatal("p or q not prime");

	while((p = getline(&n)) != nil)
		if(p[0] == '+' && strcmp(p+1, id) == 0)
			goto start;
	sysfatal("no message found");

start:
	fmtstrinit(&fmt);
	while((p = getline(&n)) != nil) {
		if(n >= 1+nid+1+16 && p[0] == '-' && strncmp(p+1, id, nid) == 0 && p[1+nid] == ' ')
			goto end;
		if(p[0] == '+')
			p++;
		fmtprint(&fmt, "%s\n", p);
	}
	sysfatal("did not find end of message");

end:
	if(dec16(sig, sizeof sig, p+1+nid+1, n-(1+nid+1)) != 20+20)
		sysfatal("malformed signture");
	text = fmtstrflush(&fmt);
	sha1((uchar*)text, strlen(text), digest, nil);

	dsig.r = betomp(sig, 20, nil);
	dsig.s = betomp(sig+20, 20, nil);

	if(dsaverify(&dkey, &dsig, betomp(digest, sizeof digest, nil)) < 0)
		sysfatal("signature failed to verify: %r");
	
	write(1, text, strlen(text));
	exits("");
}

static char*
getline(int *np)
{
	char *p;
	int n;
	
	if((p = Brdline(&b, '\n')) == nil)
		return nil;
	n = Blinelen(&b);
	while(n > 0 && (p[n-1] == '\n' || p[n-1] == ' ' || p[n-1] == '\t'))
		n--;
	p[n] = '\0';
	*np = n;
	return p;
}
