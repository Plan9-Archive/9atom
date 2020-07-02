#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <regexp.h>
#include <fcall.h>
#include "httpd.h"
#include "httpsrv.h"

static	Hio		*hout;
static	Hio		houtb;
static	HConnect	*connect;
static	int		vermaj, gidwidth, uidwidth, lenwidth, devwidth;
static	Biobuf		*aio, *dio;

typedef struct Search Search;
struct Search{
	char	*f[10];
	int	nf;
	char	*g[10];
};

static int
parsesearch(Search *s, char *p)
{
	char *e;
	int i;

	memset(s, 0, sizeof *s);
	s->nf = getfields(p, s->f, nelem(s->f), 0, "&");
	for(i = 0; i < s->nf; i++){
		if(e = strchr(s->f[i], '=')){
			*e = 0;
			s->g[i] = hurlunesc(connect, e + 1);
		}
	}
	return 0;
}

char*
findsearch(Search *s, char *p)
{
	int i;

	for(i = 0; i < s->nf; i++)
		if(!strcmp(s->f[i], p))
			return s->g[i]? s->g[i]: "";
	return "";
}

static void
doctype(void)
{
	hprint(hout, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n");
	hprint(hout, "    \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n");
}

void
error(char *title, char *fmt, ...)
{
	va_list arg;
	char buf[1024], *out;

	va_start(arg, fmt);
	out = vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	*out = 0;

	hprint(hout, "%s 404 %s\r\n", hversion, title);
	hprint(hout, "Date: %D\r\n", time(nil));
	hprint(hout, "Server: Plan9\r\n");
	hprint(hout, "Content-type: text/html\r\n");
	hprint(hout, "\r\n");
	doctype();
	hprint(hout, "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n");
	hprint(hout, "<head><title>%s</title></head>\n", title);
	hprint(hout, "<body>\n");
	hprint(hout, "<h1>%s</h1>\n", title);
	hprint(hout, "%s\n", buf);
	hprint(hout, "</body>\n");
	hprint(hout, "</html>\n");
	hflush(hout);
	writelog(connect, "Reply: 404\nReason: %s\n", title);
	exits(nil);
}

/*
 * Are we actually allowed to look in here?
 *
 * Rules:
 *	1) If neither allowed nor denied files exist, access is granted.
 *	2) If allowed exists and denied does not, dir *must* be in allowed
 *	   for access to be granted, otherwise, access is denied.
 *	3) If denied exists and allowed does not, dir *must not* be in
 *	   denied for access to be granted, otherwise, access is enied.
 *	4) If both exist, okay if either (a) file is not in denied, or
 *	   (b) in denied and in allowed.  Otherwise, access is denied.
 */
static Reprog *
getre(Biobuf *buf)
{
	Reprog	*re;
	char	*p, *t;
	char	*bbuf;
	int	n;

	if (buf == nil)
		return(nil);
	for ( ; ; free(p)) {
		p = Brdstr(buf, '\n', 0);
		if (p == nil)
			return(nil);
		t = strchr(p, '#');
		if (t != nil)
			*t = '\0';
		t = p + strlen(p);
		while (--t > p && isspace(*t))
			*t = '\0';
		n = strlen(p);
		if (n == 0)
			continue;

		/* root the regular expresssion */
		bbuf = malloc(n+2);
		if(bbuf == nil)
			sysfatal("out of memory");
		bbuf[0] = '^';
		strcpy(bbuf+1, p);
		re = regcomp(bbuf);
		free(bbuf);

		if (re == nil)
			continue;
		free(p);
		return(re);
	}
}

static int
allowed(char *dir)
{
	Reprog	*re;
	int	okay;
	Resub	match;

	if (strcmp(dir, "..") == 0 || strncmp(dir, "../", 3) == 0)
		return(0);
	if (aio == nil)
		return(0);
	if (aio != nil)
		Bseek(aio, 0, 0);
	if (dio != nil)
		Bseek(dio, 0, 0);

	/* if no deny list, assume everything is denied */
	okay = (dio != nil);

	/* go through denials till we find a match */
	while (okay && (re = getre(dio)) != nil) {
		memset(&match, 0, sizeof(match));
		okay = (regexec(re, dir, &match, 1) != 1);
		free(re);
	}

	/* go through accepts till we have a match */
	if (aio == nil)
		return(okay);
	while (!okay && (re = getre(aio)) != nil) {
		memset(&match, 0, sizeof(match));
		okay = (regexec(re, dir, &match, 1) == 1);
		free(re);
	}
	return(okay);
}

/*
 * Comparison routine for sorting the directory.
 */
static int
compar(Dir *a, Dir *b)
{
	return(strcmp(a->name, b->name));
}

/*
 * These is for formating; how wide are variable-length
 * fields?
 */
static void
maxwidths(Dir *dp, long n)
{
	long	i;
	char	scratch[64];

	for (i = 0; i < n; i++) {
		if (snprint(scratch, sizeof scratch, "%ud", dp[i].dev) > devwidth)
			devwidth = strlen(scratch);
		if (strlen(dp[i].uid) > uidwidth)
			uidwidth = strlen(dp[i].uid);
		if (strlen(dp[i].gid) > gidwidth)
			gidwidth = strlen(dp[i].gid);
		if (snprint(scratch, sizeof scratch, "%lld", dp[i].length) > lenwidth)
			lenwidth = strlen(scratch);
	}
}

/*
 * Do an actual directory listing.
 * asciitime is lifted directly out of ls.
 */
char *
asciitime(long l)
{
	ulong clk;
	static char buf[32];
	char *t;

	clk = time(nil);
	t = ctime(l);
	/* 6 months in the past or a day in the future */
	if(l<clk-180L*24*60*60 || clk+24L*60*60<l){
		memmove(buf, t+4, 7);		/* month and day */
		memmove(buf+7, t+23, 5);		/* year */
	}else
		memmove(buf, t+4, 12);		/* skip day of week */
	buf[12] = 0;
	return buf;
}

static char*
nam(Search *s, char *f, char *e)
{
	char *p;
	static char buf[256];

	p = findsearch(s, e);
	snprint(buf, sizeof buf, "%s/%s", p, f);
	cleanname(buf);
	return buf;
}

static char*
rnam(Search *s, char *f)
{
	return nam(s, f, "d");
}

static char*
deprefix(Search *, char *f)
{
	return f;
}

static char*
defix(Search *s, char *f)
{
	char *p;
	int l;

	p = findsearch(s, "p");
	l = strlen(p);
	if(cistrncmp(p, f, l) == 0)
		f += l;
	return f;
}

static char*
fnam(Search *s, char *f)
{
	char *p;
	static char buf[256];

	p = findsearch(s, "f");
	snprint(buf, sizeof buf, "/%s/%s", p, defix(s, f));
	cleanname(buf);
	return buf;
}

static int
sallowed(Search *s, char *p)
{
	char buf[256];

	snprint(buf, sizeof buf, "%s/%s", findsearch(s, "p"), p);
	cleanname(buf);
	return allowed(buf);
}

static void
rollup(Search *s, char *dir)
{
	char *f[30], *r;
	int nf, i;

	r = findsearch(s, "r");
	if(cistrncmp(dir, r, strlen(r)) == 0)
		dir += strlen(r);

	nf = getfields(dir, f, nelem(f), 0, "/");
	hprint(hout, "<h1>");
	if(nf <= 1){
		hprint(hout, " . </h1>");
		return;
	}
	hprint(hout, "<a href=\"%s%s\">%s</a>", r, rnam(s, f[0]), ".");

	for(i = 1; i < nf - 1; i++){
		f[i][-1] = '/';
		hprint(hout, " / ");
		if(sallowed(s, dir))
			hprint(hout, "<a href=\"%s%s\">%s</a>", r, rnam(s, dir), f[i]);
		else
			hprint(hout, "%s", f[i]);
	}
	f[i][-1] = '/';
	hprint(hout, "/ %s</h1>", f[i]);
}

static void
dols(char *dir, Search *s)
{
	char *f, *p, buf[256];
	int i, n, fd;
	Dir *d;

	/* expand "" to "."; ``dir+1'' access below depends on this */
	cleanname(dir);
	snprint(buf, sizeof buf, "%s/%s", findsearch(s, "p"), dir);
	cleanname(buf);
	if(!allowed(buf)) {
		error("Permission denied", "<p>Cannot list directory %s: Access prohibited</p>", dir);
		return;
	}
	fd = open(buf, OREAD);
	if(fd < 0) {
		error("Cannot read directory", "<p>Cannot read directory %s: %r</p>", dir);
		return;
	}
	if (vermaj) {
		hokheaders(connect);
		hprint(hout, "Content-type: text/html\r\n");
		hprint(hout, "\r\n");
	}
	doctype();
	hprint(hout, "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n");
	hprint(hout, "<head><title>Index of %s</title></head>\n", dir);
	hprint(hout, "<body>\n");

	rollup(s, dir);
	n = dirreadall(fd, &d);
	close(fd);
	maxwidths(d, n);
	qsort(d, n, sizeof(Dir), (int (*)(void *, void *))compar);
	hprint(hout, "<pre>\n");
	for (i = 0; i < n; i++) {
		f = smprint("%s/%s", dir, d[i].name);
		cleanname(f);
		if(d[i].mode & DMDIR) {
			p = smprint("%s", rnam(s, f));
			free(f);
			f = p;
		}else
			p = fnam(s, f);
		hprint(hout, "%M %C %*ud %-*s %-*s %*lld %s <a href=\"%s\">%s</a>\n",
			d[i].mode, d[i].type,
			devwidth, d[i].dev,
			uidwidth, "none" /*d[i].uid*/,
			gidwidth, "none" /*d[i].gid*/,
			lenwidth, d[i].length,
			asciitime(d[i].mtime), p, d[i].name);
		free(f);
	}
	cleanname(f = smprint("%s/..", deprefix(s, dir)));
	if(strcmp(f, "/") != 0 && sallowed(s, f))
		hprint(hout, "\nGo to <a href=\"%s\">parent</a> directory\n", rnam(s, f));
	else
		hprint(hout, "\n");
	free(f);
	hprint(hout, "</pre>\n</body>\n</html>\n");
	hflush(hout);
	free(d);
}

/*
 * Handle unpacking the request in the URI and
 * invoking the actual handler.
 */
static void
dosearch(char *search)
{
	char *dir;
	Search s;

	if(parsesearch(&s, search) == -1)
		goto lose;
	if(dir = findsearch(&s, "dir")){
		dols(dir, &s);
		return;
	}

	/*
	 * Otherwise, we've gotten an illegal request.
	 * spit out a non-apologetic error.
	 */
lose:
	search = hurlunesc(connect, search);
	error("Bad directory listing request",
	    "<p>Illegal formatted directory listing request:</p>\n"
	    "<p>%H</p>", search);
}

void
main(int argc, char **argv)
{
	fmtinstall('H', httpfmt);
	fmtinstall('U', hurlfmt);
	fmtinstall('M', dirmodefmt);

	aio = Bopen("/sys/lib/webls.allowed", OREAD);
	dio = Bopen("/sys/lib/webls.denied", OREAD);

	if(argc == 2){
		hinit(&houtb, 1, Hwrite);
		hout = &houtb;
		dols(argv[1], nil);
		exits(nil);
	}
	close(2);

	connect = init(argc, argv);
	hout = &connect->hout;
	vermaj = connect->req.vermaj;
	if(hparseheaders(connect, HSTIMEOUT) < 0)
		exits("failed");

	if(strcmp(connect->req.meth, "GET") != 0 && strcmp(connect->req.meth, "HEAD") != 0){
		hunallowed(connect, "GET, HEAD");
		exits("not allowed");
	}
	if(connect->head.expectother || connect->head.expectcont){
		hfail(connect, HExpectFail, nil);
		exits("failed");
	}

	bind("/usr/web", "/", MREPL);

	if(connect->req.search != nil)
		dosearch(connect->req.search);
	else
		error("Bad argument", "<p>Need a search argument</p>");
	hflush(hout);
	writelog(connect, "200 sources %ld %ld\n", hout->seek, hout->seek);
	exits(nil);
}
