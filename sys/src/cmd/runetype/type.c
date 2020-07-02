#include <u.h>
#include <libc.h>
//#include <runetype.c>
#include <bio.h>

Rune
strtorune(char *s)
{
	char *r;
	ulong u;

	u = strtoul(s, &r, 0);
	if(*r != 0 || u > Runemax)
		return Runeerror;
	return (Rune)u;
}

void
classify(Biobuf *b, Rune r)
{
	print("%.4ux ", r);
	if(isalpharune(r))
		Bprint(b, "alpha ");
	if(istitlerune(r))
		Bprint(b, "title ");
	if(isspacerune(r))
		Bprint(b, "space ");
	if(islowerrune(r))
		Bprint(b, "lower:%C(%.4ux) ", toupperrune(r), toupperrune(r));
	if(isupperrune(r))
		Bprint(b, "upper:%C(%.4ux) ", tolowerrune(r), tolowerrune(r));
	if(isdigitrune(r))
		Bprint(b, "digit:%d", digitrunevalue(r));
	Bprint(b, "\n");
}

void
usage(void)
{
	fprint(2, "usage: runetype [-x] ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *s;
	int i, flagx;
	Rune r;
	Biobuf b;

	flagx = 0;
	ARGBEGIN{
	case 'x':
		flagx = 1;
		break;
	default:
		usage();
	}ARGEND
	Binit(&b, 1, OWRITE);
	for(i = 0; i < argc; i++){
		for(s = argv[i]; *s != 0;){
			if(flagx){
				r = strtorune(s);
				s += strlen(s);
			}else
				s += chartorune(&r, s);
			if(r == Runeerror){
				Bflush(&b);
				fprint(2, "bad rune %s\n", s);
				continue;
			}
			classify(&b, r);
		}
	}
	Bterm(&b);
	exits("");
}
