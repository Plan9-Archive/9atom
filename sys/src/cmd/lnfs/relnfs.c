#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>

enum
{
	Enclen = 26,
};

typedef struct Name Name;
struct Name {
	char	shortname[Enclen + 1];
	char*	longname;
	Name*	next;
};

static	Name	*names;
static	Name	*newnames;
static	int	newmax		= 53;
static	char	flag[128];

void
long2short(char shortname[Enclen+1], char *longname)
{
	uchar digest[MD5dlen];

	md5((uchar*)longname, strlen(longname), digest, nil);
	enc32(shortname, Enclen+1, digest, MD5dlen);
}

Name*
addname(Name **list, char *longname)
{
	Name *n;

	n = malloc(sizeof *n);
	if(n == nil)
		sysfatal("relnfs: malloc: %r");
	memset(n, 0, sizeof *n);
	n->longname = longname;
	long2short(n->shortname, longname);
	n->next = *list;
	return *list = n;
}

void
readnames(Name **list, char *lnfile)
{
	char *s;
	Biobuf *b;

	b = Bopen(lnfile, OREAD);
	if(b == nil)
		sysfatal("relnfs: cannot open %s: %r", lnfile);
	while((s = Brdstr(b, '\n', 1)) != nil)
		addname(list, s);
	Bterm(b);
}

void
writenames(Name **list, char *lnfile)
{
	int fd;
	Name *n;
	Biobuf b;

	fd = create(lnfile, OWRITE, 0664);
	if(fd == -1)
		sysfatal("relnfs: can't rewrite: %s: %r", lnfile);
	if(Binit(&b, fd, OWRITE) == -1)
		sysfatal("relnfs: Binit: %s: %r", lnfile);
	for(n = *list; n != nil; n = n->next)
		Bprint(&b, "%s\n", n->longname);
	Bterm(&b);
	close(fd);
}

void
rename(char *d, char *old, char *new)
{
	char *p;
	Dir dir;

	p = smprint("%s/%s", d, old);
	nulldir(&dir);
	dir.name = new;
	if(dirwstat(p, &dir) == -1)
		fprint(2, "relnfs: cannot rename %s to %s: %r\n", p, new);
	if(flag['v'])
		fprint(2, "%s/%s → %s\n", d, old, new);
	free(p);
}

Name*
lookupshort(Name *list, char *s)
{
	Name *n;

	if(strlen(s) != Enclen)
		return nil;
	for(n = list; n != nil; n = n->next)
		if(strcmp(n->shortname, s) == 0)
			return n;
	return nil;
}

static int
froggy(char *s)
{
	return !flag['f'] && strchr(s, ' ') != 0;
}

void
renamedir(char *d)
{
	char *sub, *name;
	int fd, i, n;
	Dir *dir;
	Name *na;

	fd = open(d, OREAD);
	if(fd == -1)
		return;
	while((n = dirread(fd, &dir)) > 0){
		for(i = 0; i < n; i++){
			if(dir[i].mode & DMDIR){
				sub = smprint("%s/%s", d, dir[i].name);
				renamedir(sub);
				free(sub);
			}
			name = dir[i].name;
			if((na = lookupshort(names, name)) != nil)
				name = na->longname;
			if(strlen(name) > newmax || froggy(name))
				name = addname(&newnames, strdup(name))->shortname;
			if(strcmp(dir[i].name, name) != 0)
				rename(d, dir[i].name, name);
		}
		free(dir);
	}
	close(fd);
}

void
usage(void)
{
	fprint(2, "usage: relnfs [-f] [-l maxlen] dir\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *lnfile, *d;
	int i, j;
	Name *n;

	ARGBEGIN{
	default:
		usage();
	case 'l':
		newmax = atoi(EARGF(usage()));
		break;
	case 'f':
	case 'v':
		flag[ARGC()] = 1;
		break;
	}ARGEND

	switch(argc){
	default:
		usage();
	case 0:
		d = ".";
		break;
	case 1:
		d = argv[0];
		break;
	}

	lnfile = smprint("%s/.longnames", d);
	readnames(&names, lnfile);

	renamedir(d);
	writenames(&newnames, lnfile);

	i = 0;
	for(n = names; n != nil; n = n->next)
		i++;
	j = 0;
	for(n = newnames; n != nil; n = n->next)
		j++;

	fprint(2, "%d old long names; %d new long names\n", i, j);

	free(lnfile);
	exits("");
}
