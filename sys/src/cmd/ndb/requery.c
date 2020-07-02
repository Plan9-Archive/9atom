/*
 *  pattern search the network database for matches
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <regexp.h>

static	int	flag[127];
static	Biobuf	bout;

void
usage(void)
{
	fprint(2, "usage: ndb/requery [-i] [-f ndbfile] attr value [rattr]\n");
	exits("usage");
}

/* there is no way to get " or \n in an ndb rr */
static int
fmtndbquote(Fmt *f)
{
	char *s;

	s = va_arg(f->args, char*);
	if(strpbrk(s, " \t") == nil)
		return fmtstrcpy(f, s);
	else
		return fmtprint(f, "\"%s\"", s);
}

static void
prmatch(Ndbtuple *nt, char *rattr)
{
	for(; nt; nt = nt->entry)
		if(rattr && strcmp(nt->attr, rattr) == 0)
			Bprint(&bout, "%q\n", nt->val);
		else if(!rattr)
			Bprint(&bout, "%s=%q ", nt->attr, nt->val);
	if(!rattr)
		Bprint(&bout, "\n");
}

char*
lower(char *s)
{
	char *p, c;

	for(p = s; c = *p; p++)
		if(c >= 'A' && c <= 'Z')
			*p = c - ('A' - 'a');
	return s;
}

int
iregex(Reprog *re, char *val)
{
	char *s;
	int r;

	if(!flag['i'])
		return regexec(re, val, 0, 0);
	s = lower(strdup(val));
	r = regexec(re, s, 0, 0);
	free(s);
	return r;
}

Ndbtuple*
research(Ndb *db, char *attr, char *valre, char *rattr)
{
	int m;
	Reprog *re;
	Ndbs s;
	Ndbtuple *nt, *t;

	re = regcomp(valre);
	if(re == nil)
		sysfatal("regcomp: %r");
	memset(&s, 0, sizeof s);
	for(m = 0; t = ndbparsedbs(&s, db); m = 0){
		for(nt = t; nt; nt = nt->entry){
			if(flag['a'] || strcmp(nt->attr, attr) == 0)
				if(iregex(re, nt->val))
					m = 1;
		}
		if(m)
			prmatch(t, rattr);
		ndbfree(t);
	}
	free(re);
	return 0;
}

void
main(int argc, char **argv)
{
	char *dbfile, *attr, *val, *rattr;
	Ndb *db;

	dbfile = "/lib/ndb/local";
	ARGBEGIN{
	case 'f':
		dbfile = EARGF(usage());
		break;
	case 'a':
	case 'i':
		flag[ARGC()] = 1;
		break;
	default:
		usage();
	}ARGEND;

	fmtinstall('q', fmtndbquote);
	attr = nil;
	if(flag['a'] == 0){
		attr = *argv++;
		if(attr == nil)
			usage();
	}
	val = *argv++;
	if(val == nil)
		usage();
	if(flag['i'])
		val = lower(val);
	rattr = *argv;

	if(Binit(&bout, 1, OWRITE) == -1)
		sysfatal("Binit: %r");
	db = ndbopen(dbfile);
	if(db == nil){
		fprint(2, "%s: no db files\n", argv0);
		exits("no db");
	}
	research(db, attr, val, rattr);
	ndbclose(db);
	Bterm(&bout);
	exits(0);
}
