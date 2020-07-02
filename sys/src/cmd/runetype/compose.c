#include <u.h>
#include <libc.h>
//#include "runecompose.h"
#include <bio.h>

void
compose(Biobuf *o, Biobuf *b)
{
	int r0, r1, c;

	r0 = Bgetrune(o);
	r1 = Bgetrune(o);
	for(;;){
		if((c = runecompose(r0, r1)) != -1)
			r0 = c;		/* allow recomposition */
		else{
			Bputrune(b, r0);
			r0 = r1;
		}
		r1 = Bgetrune(o);
		if(r1 == -1)
			break;
	}
	if(r0 != -1)
		Bputrune(b, r0);
}

void
usage(void)
{
	fprint(2, "usage: compose ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, fd;
	Biobuf b, o;

	ARGBEGIN{
	default:
		usage();
	}ARGEND
	Binit(&b, 1, OWRITE);
	for(i = 0; i < argc; i++){
		fd = open(argv[i], OREAD);
		if(fd == -1)
			sysfatal("open: %r");
		Binit(&o, fd, OREAD);
		compose(&o, &b);
		close(fd);
	}
	if(argc == 0){
		Binit(&o, 0, OREAD);
		compose(&o, &b);
	}
	Bterm(&o);
	Bterm(&b);
	exits("");
}
