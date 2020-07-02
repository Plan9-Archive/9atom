#include <u.h>
#include <libc.h>
#include <fis.h>
#include "dat.h"

typedef	struct	Dl	Dl;
struct Dl {
	char	*firmware;
	uint	length;
	uint	okfw;
};

static	Sdisk	*disks;
static	Dl	dl;
static	Dtype	dtab[] = {
	Tata,	"ata",	ataprobe,	atadl,
	Tscsi,	"scsi",	scsiprobe,	nil,
};

void
eprint(Sdisk *d, char *s, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, s);
	vseprint(buf, buf + sizeof buf, s, arg);
	va_end(arg);
	fprint(2, "%s: %s", d->name, buf);
}

void
firmware(char *f)
{
	int fd;
	Dir *d;

	fd = open(f, OREAD);
	if(fd == -1)
		sysfatal("open: %r");
	d = dirfstat(fd);
	if(d == nil)
		sysfatal("dirfstat: %r");
	if(d->length/512 > 1ul<<16 || d->length&0x1ff)
		sysfatal("firmware length: %llux", d->length);
	dl.length = d->length;
	free(d);

	dl.firmware = realloc(dl.firmware, dl.length);
	if(readn(fd, dl.firmware, dl.length) != dl.length)
		sysfatal("readn: %r");
	dl.okfw = 1;
}

static int
diskopen(Sdisk *d)
{
	char buf[128];

	snprint(buf, sizeof buf, "%s/raw", d->path);
	werrstr("");
	return d->fd = open(buf, ORDWR);
}

static void
diskclose(Sdisk *d)
{
	close(d->fd);
	d->fd = -1;
}

static int
newdisk(char *s)
{
	char buf[128], *p;
	int i;
	Sdisk d;

	memset(&d, 0, sizeof d);
	snprint(d.path, sizeof d.path, "%s", s);
	if(p = strrchr(s, '/'))
		p++;
	else
		p = s;
	snprint(d.name, sizeof d.name, "%s", p);
	snprint(buf, sizeof buf, "%s/raw", s);
	if(diskopen(&d) == -1)
		return -1;
	for(i = 0; i < nelem(dtab); i++)
		if(dtab[i].probe(&d) == 0){
			d.t = dtab + i;
			break;
		}
	diskclose(&d);
	if(d.t != 0){
		d.next = disks;
		disks = malloc(sizeof d);
		memmove(disks, &d, sizeof d);
		return 0;
	}
	werrstr("unknown disk protocol");
	return -1;
}

void
usage(void)
{
	fprint(2, "usage: dlmicrocode -f fw disk ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i;
	Sdisk *d;

	ARGBEGIN{
	case 'f':
		firmware(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND

	if(dl.okfw != 1)
		usage();

	for(i = 0; i < argc; i++)
		if(newdisk(argv[i]) != 0)
			sysfatal("bad disk");
		else if(disks->t->dlmc == nil)
			sysfatal("no dlmc function");

	for(d = disks; d != nil; d = d->next){
		print("%s\n", d->path);
		if(diskopen(d) == -1)
			sysfatal("diskopen: %r");
		if(d->t->dlmc(d, dl.firmware, dl.length) == -1)
			sysfatal("disk bricked");
		diskclose(d);
	}
}
