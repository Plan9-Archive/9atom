#include <u.h>
#include <libc.h>
#include <authsrv.h>

Nvrsafe nvr;

void
usage(void)
{
	fprint(2, "usage: mknvrsafe [-r file] [-c config]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *config, *file;
	int fd;
	uint c;

	config = nil;
	file = nil;
	fd = 1;
	ARGBEGIN{
	case 'r':
		file = EARGF(usage());
		break;
	case 'c':
		config = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	if(argc || file != nil && config == nil)
		usage();
	if(file != nil){
		fd = open(file, ORDWR);
		if(fd == -1 || pread(fd, &nvr, sizeof nvr, 0) != sizeof nvr)
			sysfatal("mknvrsafe: read: %r");
	}
	c = 0;
	if(config != nil)
		c = strlen(config);
	if(c >= sizeof nvr.config)
		sysfatal("mknvrsafe: config string too long: %d > %d", c, sizeof nvr.config);
	memcpy(nvr.config, config, c);
	nvr.configsum = nvcsum(nvr.config, sizeof nvr.config);

	pwrite(fd, &nvr, sizeof nvr, 0);
	exits("");
}
