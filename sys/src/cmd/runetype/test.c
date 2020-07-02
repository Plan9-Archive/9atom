#include <u.h>
#include <libc.h>

char* tf(int i)
{
	if(i)
		return "t";
	return "";
}

int
utftod(const char* s)
{
	int l, b, t;
	Rune r;

	t=0;
	for(l=strlen(s); *s; s+=b, l-=b){
		b = chartorune(&r, (char*)s);
		if(b == 0)
			break;
		t = t*10 + digitrunevalue(r);
	}
	return t;
}

int
convert(char* s, Rune (*R)(Rune))
{
	int l, b, t;
	Rune r;

	t=0;
	for(l=strlen(s); *s; s+=b, l-=b){
		b = chartorune(&r, s);
		if(b == 0)
			break;
		print("%C", R(r));
	}
	return t;
}

void
main(int, char**v)
{
	int l, b, number;
	Rune r;
	char* s;
	char ult[UTFmax*3 + sizeof("//\0")];

	if (*v)
		print ("%C\t%6s\t%6s\t%6s\t%6s\t%6s\t%6s\t%6s\n", 0x24d0,
			"digit", "alpha", "upper", "lower", "title", "space", "u/l/t");
	for(++v; *v; v++){
		s = *v;
		l=strlen(s);
		number=1;
		for(; *s; s += b, l-=b){
			b=chartorune(&r, s);
			if (0 == b)
				break;
			if (number)
				number &= isdigitrune(r);

			snprint(ult, sizeof(ult), "%C/%C/%C", tolowerrune(r), toupperrune(r),
				totitlerune(r));

			print ("%C\t" "%6s\t%6s\t%6s\t%6s\t%6s\t%6s\t%6s\n", r,
				tf(isdigitrune(r)),
				tf(isalpharune(r)),
				tf(isupperrune(r)),
				tf(islowerrune(r)),
				tf(istitlerune(r)),
				tf(isspacerune(r)),
				ult
			);
		}

		if (number)
			print("%d\t", utftod(*v));
		convert(*v, toupperrune);
		print("\t");
		convert(*v, tolowerrune);
		print("\t");
		convert(*v, totitlerune);
		print("\n");

	}
	exits("");
}
