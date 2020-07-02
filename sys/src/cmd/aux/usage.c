#include <u.h>
#include <libc.h>

void
addflags(Fmt *fmt, char *flags)
{
	char *p, *p0;
	int single;
	Rune r;

	single = 0;
	for(p=flags; *p; ){
		p += chartorune(&r, p);
		if(*p == ',' || *p == 0){
			if(!single){
				fmtprint(fmt, " [-");
				single = 1;
			}
			fmtprint(fmt, "%C", r);
			if(*p == ',')
				p++;
			continue;
		}
		while(*p == ' ')
			p++;
		if(single){
			fmtprint(fmt, "]");
			single = 0;
		}
		p0 = p;
		p = strchr(p0, ',');
		if(p == nil)
			p = "";
		else
			*p++ = 0;
		fmtprint(fmt, " [-%C %s]", r, p0);
	}
	if(single)
		fmtprint(fmt, "]");
	
}

void
main(void)
{
	Fmt fmt;
	char buf[512];
	char *argv0, *args, *ptargs, *flags, *ptflags, *p;
	
	argv0 = getenv("0");
	if((p = strrchr(argv0, '/')) != nil)
		argv0 = p+1;
	flags = getenv("flagfmt");
	ptflags = getenv("ptflagfmt");
	args = getenv("args");
	ptargs = getenv("ptargs");

	if(argv0 == nil){
		fprint(2, "aux/usage: $0 not set\n");
		exits("$0");
	}
	if(flags == nil)
		flags = "";
	if(ptflags == nil)
		ptflags = "";
	if(args == nil)
		args = "";

	fmtfdinit(&fmt, 2, buf, sizeof buf);
	fmtprint(&fmt, "usage: %s", argv0);
	if(ptargs != nil)
		fmtprint(&fmt, " [%s]", ptargs);
	else if(ptflags[0])
		addflags(&fmt, ptflags);
	if(flags[0])
		addflags(&fmt, flags);
	if(args)
		fmtprint(&fmt, " %s", args);
	fmtprint(&fmt, "\n");
	fmtfdflush(&fmt);
	exits("usage");
}
