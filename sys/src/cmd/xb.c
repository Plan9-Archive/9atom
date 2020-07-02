/*
 * Almost an xml beautifier.
 *
 * It can be useful just for re-indenting machine generated xml
 * to make it human readable. It also performs some basic checks
 * on the file's validity which can be handy if hand editing the
 * xml source.
 *
 * It does take some liberties with the data:
 *
 *	- It strips the all comments, though the xml version
 *	   and DOCTYPE "structured comments" are preserved.
 *
 *	- attributes are always quoted with more plan9 style single
 *	  quotes whether the source has single or double quotes.
 *
 *	- empty elements are replaced with the short form, e.g.
 *	   <fred a='1'> </fred> is mapped to <fred a='1' />
 */

#include <u.h>
#include <libc.h>
#include <pool.h>
#include <xml.h>

static void
usage(void)
{
	fprint(2, "usage: %s [-dM] [file.xml]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Xml *xp;
	int fd, mem;

	mem = 0;
	ARGBEGIN{
	case 'd':
		xmldebug++;
		break;
	case 'M':
		mem++;
		break;
	default:
		usage();
	}ARGEND;


	if(mem > 1)
		mainmem->flags |= POOL_NOREUSE|POOL_PARANOIA|POOL_ANTAGONISM;

	if(argc == 0){
		if((xp = xmlparse(0, 8192, Fcrushwhite)) == nil)
			sysfatal("stdin:%r\n");
	}
	else{
		if((fd = open(argv[0], OREAD)) == -1)
			sysfatal("%s cannot open\n", argv[0]);
		if((xp = xmlparse(fd, 8192, Fcrushwhite)) == nil)
			sysfatal("%s:%r\n", argv[0]);
		close(fd);
	}
	if(mem)
		fprint(2, "%s: parsed - mem size=%lud free=%lud alloc=%lud nfree=%d\n",
			argv0, mainmem->cursize, mainmem->curfree, mainmem->curalloc, mainmem->nfree);
	xmlprint(xp, 1);
	xmlfree(xp);
	if(mem)
		fprint(2, "%s: free'd - mem size=%lud free=%lud alloc=%lud nfree=%d\n",
			argv0, mainmem->cursize, mainmem->curfree, mainmem->curalloc, mainmem->nfree);
	exits("");
}
