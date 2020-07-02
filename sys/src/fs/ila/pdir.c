#include<u.h>
#include<libc.h>
#include<bio.h>

void
main(int argc, char **argv)
{
	Dir *d;
	Biobuf b;
	long i, n;
	int fd;

	ARGBEGIN{
	}ARGEND

	if(Binit(&b, 1, OWRITE) == -1)
		sysfatal("Binit: %r");

	if(argc == 0){
		n = dirreadall(0, &d);
		for(i = 0; i < n; i++)
			Bprint(&b, "%s\n", d[i].name);
		free(d);
	}
	for(; *argv; argv++){
		fd = open(*argv, OREAD);
		if(fd == -1)
			sysfatal("open: %r");
		n = dirreadall(fd, &d);
		for(i = 0; i < n; i++)
			Bprint(&b, "%s\n", d[i].name);
		free(d);
		close(fd);
	}
	exits("");
}
