#include <u.h>
#include <libc.h>
#include <bio.h>

#include "dat.h"

uchar	flag[0x7f];
Biobuf	out;

void
usage(void)
{
	fprint(2, "usage: pcirw -r tbdf [offset size ...]\n");
	fprint(2, "usage: pcirw -w tbdf [val offset size ...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int c, i, o, w, n, tbdf;
	Pcidev *p;

	ARGBEGIN{
	case 'r':
	case 'w':
		i = ARGC();
		flag['r'] = flag['w'] = 0;
		flag[i] = 1;
		break;
	default:
		usage();
	}ARGEND;

	if(flag['r'] + flag['w'] == 0)
		flag['r'] = 1;
	if((argc-1) % 2 && flag['r'] || (argc-1) % 3 && flag['w'])
		usage();

	if(Binit(&out, 1, OWRITE) == -1)
		sysfatal("Binit: %r");

	pciinit();
	tbdf = strtotbdf(argv[0]);
	if(tbdf == BUSUNKNOWN)
		usage();
	p = pcimatchtbdf(tbdf);
	if(p == nil)
		sysfatal("device not found");
	w = 0;
	for(c = 1; c < argc;){
		if(flag['w'])
			w = strtoul(argv[c++], 0, 0);
		o = strtoul(argv[c++], 0, 0);
		if(o > 4095)
			sysfatal("offset range");
		n = strtoul(argv[c++], 0, 0);
		if(n != 1 && n != 2 && n != 4)
			sysfatal("bad size");
		if(flag['w'])
			putn(p, o, w, n);
		else
			Bprint(&out, "%.*ux\n", n*2, getn(p, o, n));
	}
	Bterm(&out);

	exits("");
}
