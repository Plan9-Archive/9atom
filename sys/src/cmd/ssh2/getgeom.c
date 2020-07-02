#include <u.h>
#include <libc.h>
#include <draw.h>

static Point
fontsize(void)
{
	char *fontname;
	Font *f;
	Point sz;

	if((fontname = getenv("font")) == nil)
		return Pt(8, 12);

	if((f = openfont(nil, fontname)) == nil){
		fprint(2, "%s: %s cannot open - %r\n", argv0, fontname);
		free(fontname);
		return Pt(8, 12);
	}
	sz = stringsize(f, "0");
	freefont(f);
	free(fontname);
	return sz;
}

int
getgeom(int *cols, int *lines, int *width, int *height)
{
	char *a[6], buf[64];
	int fd, n;
	Point sz;

	if((fd = open("/dev/wctl", OREAD)) < 0)
		return -1;

	/* wait for event, but don't care what it says */
	if((n = read(fd, buf, sizeof buf)) < 0){
		fprint(2, "%s: /dev/wctl read failed - %r\n", argv0);
		close(fd);
		return -1;
	}

	buf[n-1] = 0;
	if((n = tokenize(buf, a, nelem(a))) < 4){
		fprint(2, "%s: /dev/wctl too few tokens (%d<4)\n", argv0, n);
		close(2);
		return -1;
	}
	close(fd);

	sz = fontsize();

	/*
	 * This code lifted from mc.c, and is correct for rio(1) windows.
	 * 4 pixels left edge
	 * 1 pixels gap
	 * 12 pixels scrollbar
	 * 4 pixels gap
	 * text
	 * 4 pixels right edge
	 *
	 * 4 pixels top and bottom edges
	 */
	*width = atoi(a[2]) - atoi(a[0]) - (4+1+12+4+4);
	*height = atoi(a[3]) - atoi(a[1]) - (4+4);
	*lines = *height / sz.y;
	*cols = *width / sz.x;

	return 0;
}
