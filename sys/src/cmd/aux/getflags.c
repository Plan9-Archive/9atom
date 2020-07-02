#include <u.h>
#include <libc.h>

void
usage(void)
{
	print("status=usage\n");
	exits(0);
}

char*
findarg(char *flags, Rune r)
{
	char *p;
	Rune rr;
	
	for(p=flags; p!=(char*)1; p=strchr(p, ',')+1){
		chartorune(&rr, p);
		if(rr == r)
			return p;
	}
	return nil;	
}

int
countargs(char *p)
{
	int n;

	n = 1;
	while(*p == ' ')
		p++;
	for(; *p && *p != ','; p++)
		if(*p == ' ' && *(p-1) != ' ')
			n++;
	return n;
}

void
main(int argc, char *argv[])
{
	char *ptflags, *flags, *p, buf[512];
	int i, n, pt;
	Fmt fmt, ptfmt;
	
	doquote = needsrcquote;
	quotefmtinstall();
	argv0 = argv[0];	/* for sysfatal */
	
	flags = getenv("flagfmt");
	ptflags = getenv("ptflagfmt");
	if(flags == nil && ptflags == nil){
		fprint(2, "$flagfmt and $ptflagfmt not set\n");
		print("exit 'missing flagfmt'");
		exits(0);
	}
	fmtfdinit(&fmt, 1, buf, sizeof buf);
	if(flags != nil){
		for(p=flags; p!=(char*)1; p=strchr(p, ',')+1)
			fmtprint(&fmt, "flag%.1s=()\n", p);
	}
	fmtstrinit(&ptfmt);
	if(ptflags != nil){
	//	for(p=ptflags; p!=(char*)1; p=strchr(p, ',')+1)
	//		fmtprint(&fmt, "flag%.1s=()\n", p);
	}
	ARGBEGIN{
	default:
		pt = 0;
		p = nil;		/* compiler doesn't know usage() doesn't return */
		if(flags == nil || (p = findarg(flags, ARGC())) == nil){
			if(ptflags == nil || (p = findarg(ptflags, ARGC())) == nil)
				usage();
			pt = 1;
		}

		p += runelen(ARGC());
		if(*p == ',' || *p == 0){
			if(pt)
				fmtprint(&ptfmt, "-%C ", ARGC());
			else
				fmtprint(&fmt, "flag%C=1\n", ARGC());
			break;
		}
		n = countargs(p);
		if(pt){
			fmtprint(&ptfmt, "-%C", ARGC());
			for(i=0; i<n; i++)
				fmtprint(&ptfmt, "%q ", EARGF(usage()));
		}else{
			fmtprint(&fmt, "flag%C=(", ARGC());
			for(i=0; i<n; i++)
				fmtprint(&fmt, "%s%q", i ? " " : "", EARGF(usage()));
			fmtprint(&fmt, ")\n");
		}
	}ARGEND

	fmtprint(&fmt, "ptflag=(%s)\n", p = fmtstrflush(&ptfmt));
	fmtprint(&fmt, "*=(");
	for(i=0; i<argc; i++)
		fmtprint(&fmt, "%s%q", i ? " " : "", argv[i]);
	fmtprint(&fmt, ")\n");
	fmtprint(&fmt, "status=''\n");
	fmtfdflush(&fmt);
	free(p);
	exits(0);
}
