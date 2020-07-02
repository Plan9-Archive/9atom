#include "common.h"
#include "dat.h"

int cflag;
int aflag;
int rflag;

int
createpipeto(char *alfile, char *user, char *listname, int owner)
{
	char buf[Pathlen];
	int fd;
	Dir *d;

	mboxpathbuf(buf, sizeof buf, user, "pipeto");

	fprint(2, "creating new pipeto: %s\n", buf);
	fd = create(buf, OWRITE, 0775);
	if(fd < 0)
		return -1;
	d = dirfstat(fd);
	if(d == nil){
		fprint(fd, "Couldn't stat %s: %r\n", buf);
		return -1;
	}
	d->mode |= 0775;
	if(dirfwstat(fd, d) < 0)
		fprint(fd, "Couldn't wstat %s: %r\n", buf);
	free(d);

	fprint(fd, "#!/bin/rc\n");
	if(owner)
		fprint(fd, "/bin/upas/mlowner %s %s\n", alfile, listname);
	else
		fprint(fd, "/bin/upas/ml %s %s\n", alfile, user);
	close(fd);

	return 0;
}

void
usage(void)
{
	fprint(2, "usage:\t%s -c listname\n", argv0);
	fprint(2, "\t%s -[ar] listname addr\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *listname, *addr, alfile[Pathlen], owner[64];

	rfork(RFENVG|RFREND);

	ARGBEGIN{
	case 'c':
		cflag = 1;
		break;
	case 'r':
		rflag = 1;
		break;
	case 'a':
		aflag = 1;
		break;
	default:
		usage();
	}ARGEND;

	if(aflag + rflag + cflag > 1){
		fprint(2, "%s: -a, -r, and -c are mutually exclusive\n", argv0);
		exits("usage");
	}

	if(argc < 1)
		usage();

	listname = argv[0];
	mboxpathbuf(alfile, sizeof alfile, listname, "address-list");

	if(cflag){
		snprint(owner, sizeof owner, "%s-owner", listname);
		if(creatembox(listname, nil) < 0)
			sysfatal("creating %s's mbox: %r", listname);
		if(creatembox(owner, nil) < 0)
			sysfatal("creating %s's mbox: %r", owner);
		if(createpipeto(alfile, listname, listname, 0) < 0)
			sysfatal("creating %s's pipeto: %r", owner);
		if(createpipeto(alfile, owner, listname, 1) < 0)
			sysfatal("creating %s's pipeto: %r", owner);
		writeaddr(alfile, "# mlmgr c flag", 0, listname);
	} else if(rflag){
		if(argc != 2)
			usage();
		addr = argv[1];
		writeaddr(alfile, "# mlmgr r flag", 0, listname);
		writeaddr(alfile, addr, 1, listname);
	} else if(aflag){
		if(argc != 2)
			usage();
		addr = argv[1];
		writeaddr(alfile, "# mlmgr a flag", 0, listname);
		writeaddr(alfile, addr, 0, listname);
	}
	exits(0);
}
