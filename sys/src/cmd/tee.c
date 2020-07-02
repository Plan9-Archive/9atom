/*
 * tee-- pipe fitting
 */

#include <u.h>
#include <libc.h>

int	uflag;
int	aflag;
int	*openf;
char	in[8192];

int	intignore(void*, char*);

void
main(int argc, char **argv)
{
	int i;
	int r, n;

	ARGBEGIN {
	case 'a':
		aflag++;
		break;

	case 'i':
		atnotify(intignore, 1);
		break;

	default:
		fprint(2, "usage: tee [-ai] [file ...]\n");
		exits("usage");
	} ARGEND

	openf = malloc((1+argc)*sizeof *openf);
	if(openf == nil)
		sysfatal("tee: malloc: %r");
	n = 0;
	while(*argv) {
		if(aflag) {
			openf[n] = open(argv[0], OWRITE);
			if(openf[n] < 0)
				openf[n] = create(argv[0], OWRITE, 0666);
			seek(openf[n], 0L, 2);
		} else
			openf[n] = create(argv[0], OWRITE, 0666);
		if(openf[n] < 0) {
			fprint(2, "tee: cannot open %s: %r\n", argv[0]);
		} else
			n++;
		argv++;
	}
	openf[n++] = 1;

	for(;;) {
		r = read(0, in, sizeof in);
		if(r <= 0)
			exits(nil);
		for(i=0; i<n; i++)
			write(openf[i], in, r);
	}
}

int
intignore(void*, char *msg)
{
	if(strcmp(msg, "interrupt") == 0)
		return 1;
	return 0;
}
