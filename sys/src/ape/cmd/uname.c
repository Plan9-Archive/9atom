#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>

#define	ARGBEGIN	for((argv0||(argv0=*argv)),argv++,argc--;\
			    argv[0] && argv[0][0]=='-' && argv[0][1];\
			    argc--, argv++) {\
				char *_args, *_argt;\
				int _argc;\
				_args = &argv[0][1];\
				if(_args[0]=='-' && _args[1]==0){\
					argc--; argv++; break;\
				}\
				while(_argc = *_args++)\
				switch(_argc)
#define	ARGEND		SET(_argt);USED(_argt);USED(_argc);USED(_args);}USED(argv);USED(argc);
#define	ARGF()		(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): 0))
#define	EARGF(x)	(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): ((x), abort(), (char*)0)))

#define	ARGC()		_argc

char *argv0;

static int started;

static void
prword(char *w)
{
	if (started)
		fputs(" ", stdout);
	else
		started = 1;
	fputs(w, stdout);
}

void
main(int argc, char **argv)
{
	struct utsname u;

	uname(&u);
	if(argc == 1){
		printf("%s\n", u.sysname);
		exit(0);
	}
	ARGBEGIN {
	case 'a':
		prword(u.sysname);
		prword(u.nodename);
		prword(u.release);
		prword(u.version);
		prword(u.machine);
		break;
	case 'm':
		prword(u.machine);
		break;
	case 'n':
		prword(u.nodename);
		break;
	case 'r':
		prword(u.release);
		break;
	case 's':
		prword(u.sysname);
		break;
	case 'v':
		prword(u.version);
		break;
	} ARGEND
	printf("\n");
	exit(0);
}
