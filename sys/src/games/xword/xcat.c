#include <u.h>
#include <libc.h>
#include <bio.h>
#include "xword.h"

void
usage(void)
{
	fprint(2, "usage: xcat\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	Biobuf bin, bout;
	Xword *x;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();

	Binit(&bin, 0, OREAD);
	if((x=Brdxword(&bin)) == nil)
		sysfatal("cannot read puzzle: %r");

	Binit(&bout, 1, OWRITE);
	if(Bwrxword(&bout, x) < 0)
		sysfatal("write error: %r");

	Bterm(&bout);
	exits(nil);
}
