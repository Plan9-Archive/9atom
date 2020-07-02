#include <u.h>
#include <libc.h>
#include <ip.h>
#include <bio.h>

char	flag[127];
char	*fil;
uint	lin;

int
parsecidr(uchar *addr, uchar *mask, char *from)
{
	char *p, buf[50];
	int i, bits, z;
	vlong v;
	uchar *a;

	strecpy(buf, buf+sizeof buf, from);
	if(p = strchr(buf, '/'))
		*p = 0;
	v = parseip(addr, buf);
	if(v == -1)
		return -1;
	switch((ulong)v){
	default:
		bits = 32;
		z = 96;
		break;
	case 6:
		bits = 128;
		z = 0;
		break;
	}

	if(p){
		i = strtoul(p+1, &p, 0);
		if(i > bits)
			i = bits;
		i += z;
		memset(mask, 0, 128/8);
		for(a = mask; i >= 8; i -= 8)
			*a++ = 0xff;
		if(i > 0)
			*a = ~((1<<(8-i))-1);
	}else
		memset(mask, 0xff, IPaddrlen);
	return 0;
}

/*
 * match x.y.z.w to ~?x1.y1.z1.w1/m
 */
int
cidrmatch(char *x, char *y)
{
	uchar r, a[IPaddrlen], b[IPaddrlen], m[IPaddrlen];

	r = *y == '~';
	if(parseip(a, x) == -1)
		return 0;
	parsecidr(b, m, y+r);
	maskip(a, m, a);
	maskip(b, m, b);
	if(!memcmp(a, b, IPaddrlen) ^ r)
		return 1;
	return 0;
}

Biobuf*
patopen(char *s)
{
	Biobuf *b;

	lin = 0;
	b = Bopen(fil = s, OREAD);
	if(b == nil)
		sysfatal("open: %r");
	return b;
}

char*
nextpat(Biobuf *b)
{
	char *q;
	static char *s, *p;

b1:
	if(p == nil || *p == 0){
		free(s);
		p = s = Brdstr(b, '\n', 1);
		lin++;
	}
	if(p == nil)
		return nil;
	p += strspn(p, " \t\r");
	if(*p == '#' || *p == 0){
		*p = 0;
		goto b1;
	}
	q = p;
	p += strcspn(p, " \t\r");
	if(*p != 0)
		*p++ = 0;
	return q;
}

void
prmatch(char *s)
{
	if(flag['s'] == 0){
		if(flag['n'])
			print("%s:%ud: %s\n", fil, lin, s);
		else
			print("%s\n", s);
	}
}

void
usage(void)
{
	fprint(2, "cidr [-clLnrsv] ip [-f ipfile] file ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *pat, *file, **tab;
	int i, j, c, c0, n, ntab;
	Biobuf *b;

	file = nil;
	ARGBEGIN{
	case 'f':
		file = EARGF(usage());
	case 'c':
	case 'l':
	case 'L':
	case 'n':
	case 'r':
	case 's':
	case 'v':
		flag[ARGC()] = 1;
		break;
	default:
		usage();
	}ARGEND

	if(flag['l'] || flag['L'] || flag['c'])
		flag['s'] = 1;

	ntab = 1;
	tab = malloc(1 * sizeof *tab);
	if(tab == nil)
		sysfatal("malloc: %r");
	if(!flag['f']){
		if(argc == 0)
			usage();
		tab[0] = strdup(*argv++);
		argc--;
	}else{
		b = patopen(file);
		for(i = 0; tab[i] = nextpat(b); i++){
			if((tab[i] = strdup(tab[i])) == nil)
				sysfatal("strdup: %r");
			if(i + 1 == ntab){
				ntab <<= 2;
				tab = realloc(tab, ntab*sizeof *tab);
				if(tab == nil)
					sysfatal("realloc: %r");
			}
		}
		Bterm(b);
		ntab = i;
	}

	if(argc == 0)
		argv[argc++] = "/fd/0";

	c = 0;
	for(i = 0; i < argc; i++){
		b = patopen(argv[i]);
		c0 = c;
		while((pat = nextpat(b)) != nil){
			for(j = 0; j < ntab; j++){
				if(flag['r'])
					n = cidrmatch(tab[j], pat);
				else
					n = cidrmatch(pat, tab[j]);
				if(n ^ flag['v']){
					prmatch(pat);
					c++;
					break;
				}
			}
		}
		Bterm(b);
		if(c0 == c && flag['L'] || c0 != c&& flag['l'])
			print("%s\n", argv[i]);
	}
	for(i = 0; i < ntab; i++)
		free(tab[i]);
	free(tab);
	if(flag['c'])
		print("%d\n", c);
	exits(c>0? nil: "no matches");
}
