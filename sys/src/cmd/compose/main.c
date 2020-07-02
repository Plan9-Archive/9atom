#include "compose.h"

void
usage(void)
{
	fprint(2, "usage: compose [-t] [-c chan] [-o drawop] img ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i;
	ulong chan, op;
	C *c;

	chan = 0;
	op = SoverD;
	ARGBEGIN{
	case 'c':
		chan = strtochan(EARGF(usage()));
		if(chan == 0)
			usage();
		break;
	case 'o':
		op = strtoop(EARGF(usage()));
		if(op == ~0)
			usage();
		break;
	case 't':
		flag['t'] = 1;
		break;
	default:
		usage();
	}ARGEND
	if(argc == 0)
		usage();
	c = malloc(argc*sizeof *c);
	if(c == nil)
		sysfatal("malloc: %r");
	for(i = 0; i < argc; i++)
		intimg(c + i, argv[i], -1);
	composer(c, argc, chan, op);
	exits("");
}
