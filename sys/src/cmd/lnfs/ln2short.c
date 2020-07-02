#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>

enum
{
	Enclen = 26,
};

void
long2short(char shortname[Enclen+1], char *longname)
{
	uchar digest[MD5dlen];

	md5((uchar*)longname, strlen(longname), digest, nil);
	enc32(shortname, Enclen+1, digest, MD5dlen);
}

void
ln2short(int fd, Biobuf *o)
{
	char *s, enc[Enclen+1];
	Biobuf i;

	if(Binit(&i, fd, OREAD) == -1)
		sysfatal("ln2short: Binit: %r");
	for(; s = Brdstr(&i, '\n', 1); free(s)){
		long2short(enc, s);
		Bprint(o, "%s\n", enc);
	}
}

void
usage(void)
{
	fprint(2, "ln2short < long\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	Biobuf o;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(Binit(&o, 1, OWRITE) == -1)
		sysfatal("ln2short: Binit: %r");

	ln2short(0, &o);
	exits("");
}
