#include <u.h>
#include <goo.h>
#include "snap.h"

static char	*user = "gre";
static char	*sys = "gnot";
static char	*arch = "unknown";
static char	*term = "unknown";

#ifdef LINUX
#include <sys/utsname.h>
extern char* getenv(char*);

void
vars(void)
{
	struct utsname u;
	char *s, buf[64];

	if(s = getenv("USER"))
		user = s;
	uname(&u);
	snprint(buf, sizeof buf, "%s:%s", u.sysname, u.release);
	sys = estrdup(buf);
	if(u.machine)
		arch =estrdup(u.machine);
	if(s = getenv("TERM"))
		term = s;
}
#else
void
vars(void)
{
	extern char *getuser(void);
	extern char *sysname(void);
	extern char *getenv(char*);

	if((user = getuser()) == nil)
		user = "gre";
	if((sys = sysname()) == nil)
		sys = "gnot";
	if((arch = getenv("cputype")) == nil)
		arch = "unknown";
	if((term = getenv("terminal")) == nil)
		term = "unknown terminal type";
}
#endif

void
usage(void)
{
	fprint(2, "usage: aoesnap [-o snapfile] shelf\n");
	exits("usage");
}

char *ofile;

void
main(int argc, char **argv)
{
	Biobuf *b, b0;

	ofile = 0;
	ARGBEGIN{
	case 'o':
		ofile = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	if(argc != 1)
		usage();

	if(ofile == 0){
		b = &b0;
		if(Binit(&b0, 1, OWRITE) == Beof)
			b = 0;
	}else
		b = Bopen(ofile, OWRITE);
	if(b == 0)
		sysfatal("Bopen: %r");

	vars();

	Bprint(&b->h, "process snapshot %ld %s@%s %s %ld \"%s\"\n",
		time(0), user, sys, arch, 0L, term);
	snapw(b, atoi(*argv));
	exits("");
}
