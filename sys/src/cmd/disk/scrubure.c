#include <u.h>
#include <libc.h>

char	*block;
char	*zb;
int	bs;
int	fd;

int
isure(void)
{
	char ebuf[ERRMAX];

	rerrstr(ebuf, sizeof ebuf);
	return strstr(ebuf, "i/o error") != nil;
}

void
chkblk(uvlong lba)
{
	int r;

	r = pread(fd, block, bs, lba*bs);
	if(r == -1){
		if(!isure())
			sysfatal("pread: %ulld %r", lba);
		r = pwrite(fd, zb, bs, lba*bs);
		if(r == -1)
			sysfatal("uncorrectable: %ulld %r", lba);
		fprint(2, "%ulld\n", lba);
		/* fruitless to reread here.  can't fua */
	}
}

void
usage(void)
{
	fprint(2, "usage: scrubure file\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	uvlong max, i;
	Dir *d;

	bs = 512;

	ARGBEGIN{
	default:
		usage();
	}ARGEND
	if(argc != 1)
		usage();

	fd = open(*argv, ORDWR);
	if(fd == -1)
		sysfatal("open: %r");
	d = dirfstat(fd);
	if(d == nil)
		sysfatal("dirfstat: %r");
	max = d->length;
	free(d);

	if(max % bs)
		fprint(2, "warning: size not multiple of bs %ulld", max % bs);
	max /= bs;
	block = malloc(bs);
	zb = malloc(bs);

	if(block == nil || zb == nil)
		sysfatal("malloc: %r");
	memset(zb, 0, bs);

	for(i = 0; i < max; i++)
		chkblk(i);
	close(fd);
	exits("");
}
