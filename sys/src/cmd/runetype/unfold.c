#include <u.h>
#include <libc.h>
//#include "runeclass.c"
//#include "tobaserune.c"
#include <bio.h>

typedef struct Class Class;
struct Class {
	int	n;
	Rune	tab[1000*2];
};

char	*fmt = "%S";
int	flagi;

int
grune(Rune *r, char *re)
{
	int n;

	n = chartorune(r, re);
	r[0] = tobaserune(r[0]);
	r[1] = r[0];
	if(flagi){
		if(islowerrune(r[0]))
			r[1] = toupperrune(r[0]);
		else if(isupperrune(r[0]))
			r[1] = tolowerrune(r[0]);
	}
	return n;
}

int
pcmp(void *va, void *vb)
{
	int n;
	Rune *a, *b;

	a = va;
	b = vb;

	n = a[0] - b[0];
	if(n)
		return n;
	return a[1] - b[1];
}

int
cmprclass(Class *c)
{
	Rune *t, *p, *q;

	t = c->tab;
	qsort(t, c->n, 2*sizeof c->tab[0], pcmp);
	t[c->n*2] = 0;
	t[c->n*2+1] = 0;

	q = t;
	for(p=t+2; *p; p+=2) {
		if(p[0] > p[1])
			continue;
		if(p[0] > q[1]+1 || p[1] < q[0]) {
			q[2] = p[0];
			q[3] = p[1];
			q += 2;
			continue;
		}
		if(p[0] < q[0])
			q[0] = p[0];
		if(p[1] > q[1])
			q[1] = p[1];
	}
	q[2] = 0;
	return c->n = (q-t)/2+1;
}

void
newclass(Class *c)
{
	c->n = 0;
}

void
pclass0(Class *c, Rune r0, Rune r1)
{
	Rune *t;
	int n;

	if(c->n*2 == nelem(c->tab) - 2){
		cmprclass(c);
		if(c->n*2 == nelem(c->tab) - 2)
			sysfatal("class too big");
	}
	t = c->tab + 2*c->n++;
	n = r1!=0 && r0>r1;
	t[n] = r0;
	t[!n] = r1;
}

void
pclass(Class *c, Rune r0, Rune r1)
{
	int i;
	Rune t, *s;

	r0 = tobaserune(r0);
	r1 = tobaserune(r1);
	if(r0 > r1){
		t = r1;
		r1 = r0;
		r0 = t;
	}
	for(i = r0; i <= r1; i++)
		if(s = runeclass(i))
			for(; *s; s++)
				pclass0(c, *s, *s);
		else
			pclass0(c, i, i);
}

void
finclass(Biobuf *b, Class *c)
{
	Rune *t, *p;

	cmprclass(c);
	t = c->tab;

	if(c->n == 1 && t[0] == t[1]){
		Bprint(b, "%C", t[0]);
		return;
	}

	Bputc(b, '[');
	for(p = t; p[0]; p += 2){
		if(p[0] != p[1])
			Bprint(b, "%C-%C", p[0], p[1]);
		else
			Bprint(b, "%C", p[0]);
	}
	Bputc(b, ']');
}

void
unfold(Biobuf *b, char *re)
{
	int square, n;
	Rune r[2], o[2], r2[2];
	Class c;

	square = 0;
	n = grune(r, re);
	for(;;){
		switch(r[0]){
		case '\\':
			re += n;
			n = grune(r, re+=n);
			break;
		case '-':
			if(square)
				pclass(&c, r[0], r[0]);
			else
				Bputc(b, '-');
			n = grune(r, re+=n);
			continue;
		case '[':
			newclass(&c);
			square++;
			n = grune(r, re+=n);
			continue;
		case ']':
			finclass(b, &c);
			square--;
			n = grune(r, re+=n);
			continue;
		case 0:
			Bputc(b, '\n');
			return;
		}
		n = grune(o, re+=n);
		if(o[0] != '-' || !square){
			if(!square)
				newclass(&c);
			pclass(&c, r[0], r[0]);
			pclass(&c, r[1], r[1]);
			if(!square)
				finclass(b, &c);
			memcpy(r, o, sizeof o);
			continue;
		}
		n = grune(r2, re+=n);
		if(r2[0] == 0){
			memcpy(r, o, sizeof o);
			continue;
		}
		pclass(&c, r2[0], r[0]);
		pclass(&c, r2[1], r[1]);
	}
}

void
usage(void)
{
	fprint(2, "usage: rune/unfold string ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i;
	Biobuf b;

	ARGBEGIN{
	case 'i':
		flagi = 1;
		break;
	case 'b':
		fmt = "[%S]";
		break;
	default:
		usage();
	}ARGEND
	Binit(&b, 1, OWRITE);
	for(i = 0; i < argc; i++)
		unfold(&b, argv[i]);
	Bterm(&b);
	exits("");
}
