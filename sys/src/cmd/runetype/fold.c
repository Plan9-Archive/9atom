#include <u.h>
#include <libc.h>
//#include "tobaserune.c"
#include <bio.h>

int flagi;

void
fold(int fd)
{
	Biobuf b, o;
	int r;

	if(Binit(&b, fd, OREAD) == -1)
		sysfatal("Binit: %r");
	if(Binit(&o, 1, OWRITE) == -1)
		sysfatal("Binit: %r");
	for(;;){
		r = Bgetrune(&b);
		if(r == Beof)
			break;
		r = tobaserune(r);
		if(flagi)
			r = tolowerrune(r);
		Bputrune(&o, r);
	}
	Bterm(&b);
	Bterm(&o);
}

void
usage(void)
{
	fprint(2, "usage: rune/fold [-i] ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, fd;

	ARGBEGIN{
	case 'i':
		flagi = 1;
		break;
	default:
		usage();
	}ARGEND

	for(i = 0; i < argc; i++){
		fd = open(argv[i], OREAD);
		if(fd == -1)
			sysfatal("open: %r");
		fold(fd);
		close(fd);
	}
	if(argc == 0)
		fold(0);
	exits("");
}
