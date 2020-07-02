#include <u.h>
#include <libc.h>
#include <bio.h>

Biobuf	o;
Rune	(*f)(Rune) = tolowerrune;

/*
 * assume UTFmax+1 0 bytes at end to avoid
 * using fullrune.
 */
void
chcase(uchar *us, int len)
{
	char *s;
	int i;
	Rune r;

	s = (char*)us;
	for(i = 0; i < len; ){
		i += chartorune(&r, s+i);
		Bputrune(&o, f(r));
	}
}

void
cases(char *s, int len)
{
	uchar *s2;

	s2 = malloc(len + UTFmax + 1);
	if(s2 == nil)
		sysfatal("malloc: %r");
	memcpy(s2, s, len);
	memset(s2 + len, 0, UTFmax + 1);
	chcase(s2, len);
	free(s2);
	Bputc(&o, '\n');
}

enum {
	Bufsz	= 16*1024,
};

void
casefd(int fd)
{
	uchar buf[Bufsz + 2*UTFmax+1];
	int i, n, r;

	memset(buf + Bufsz, 0, 2*UTFmax+1);
	for(r = 0;;){
		n = read(fd, buf+r, Bufsz);
		if(n <= 0){
			chcase(buf, r);
			break;
		}
		for(i = 0; (buf[n+r-i-1]&0xc0) == 0x80; i++)
			;
		if(buf[n+r-i-1]&0x80)
			i++;
		chcase(buf, n+r-i);
		memcpy(buf, buf+n+r-i, i);
		r = i;
	}
}

void
usage(void)
{
	fprint(2, "usage: rune/case [-ltu] [-f file] [string ...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int work, fd, i;

	if(Binit(&o, 1, OWRITE) == -1)
		sysfatal("Binit: %r");
	work = 0;
	ARGBEGIN{
	case 'l':
		f = tolowerrune;
		break;
	case 't':
		f = totitlerune;
		break;
	case 'u':
		f = toupperrune;
		break;
	case 'f':
		if(fd = open(EARGF(usage()), OREAD) == -1)
			sysfatal("open: %r");
		casefd(fd);
		close(fd);
		work++;
		break;
	default:
		usage();
	}ARGEND
	for(i = 0; i < argc; i++)
		cases(argv[i], strlen(argv[i]));
	if(work + i == 0)
		casefd(0);
	Bterm(&o);
	exits("");
}
