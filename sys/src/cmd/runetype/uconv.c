#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>

enum{
	Maxdig	= 16,
};

int	fflag;
int	tab['z'] = {
['u']	4,
['U']	6,
};

void
getu(Biobuf *bin, Biobuf *bout, int n)
{
	char d[Maxdig+1];
	int i, u;

	for(i=0; i<n; i++){
		d[i] = Bgetc(bin);
		if(!isxdigit(d[i])){
			if(fflag){
				Bungetc(bin);
				break;
			}
			goto lose;
		}
	}
	d[i--] = 0;
	u = strtoul(d, 0, 16);
	if (u<=Runemax){
		Bprint(bout, "%C", u);
		return;
	}

lose:
	if(d[i] == -1)
		i--;
	if(i>0)
		Bprint(bout, "%.*s", i, d);
}

void
convert(Biobuf *bin, Biobuf *bout)
{
	int c;

	for(;;){
		c = Bgetrune(bin);
top:		switch(c){
		default:
			Bputrune(bout, c);
			break;
		case '\\':
			c = Bgetrune(bin);
			switch(c){
			case 'u':
			case 'U':
				getu(bin, bout, tab[c]);
				break;
			default:
				Bputrune(bout, '\\');
				goto top;
			}
			break;
		case -1:
			return;
		}
	}
}

void
usage(void)
{
	fprint(2, "uconv [-f] [-n defsize] [file ...]\n");
	exits("usage");
}

void main(int argc, char **argv)
{
	int i, n;
	Biobuf *b, bin, bout;

	ARGBEGIN{
	case 'f':
		fflag = 1;
		n = 16;
		goto set;
	case 'n':
		n = strtoul(EARGF(usage()), 0, 0);
	set:
		tab['u'] = n;
		tab['U'] = tab['u'];
		break;
	default:
		usage();
	}ARGEND
	if(tab['u'] > Maxdig || tab['U'] > Maxdig)
		sysfatal("runes too large");

	Binit(&bout, 1, OWRITE);
	if(argc == 0){
		Binit(&bin, 0, OREAD);
		convert(&bin, &bout);
	}else
		for(i = 0; i < argc; i++){
			if ((b = Bopen(argv[i], OREAD)) == nil)
				sysfatal("uconv: open: %r");
			convert(b, &bout);
			Bterm(b);
		}
	Bterm(&bout);
	exits(nil);
}
