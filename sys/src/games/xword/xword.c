#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <event.h>
#include <cursor.h>
#include "xword.h"

typedef struct Square Square;
typedef struct Clue Clue;

struct Square {
	Rectangle r;
	Image	*bg;
	int	num;
	char	c;
	Rune	*cp;
	int	black;
	Clue	*across;
	Clue	*down;
	Square	*sqdown;
	Square	*squp;
	int	writing;
	int	notsure;
};

enum {
	Mclueline = 5
};

struct Clue {
	int	num;
	int	isdown;
	Rune	*text;
	Square	*sq;
	Image	*bg;
	Image	*fg;
	Rectangle r;
	Rune	*line[Mclueline];		/* only free line[0] */
	int	nline;
};

static char *numfontname = "/lib/font/bit/lucida/unicode.5.font";
static char *letterfontname = "/lib/font/bit/lucm/unicode.9.font";
static char *titlefontname = "/lib/font/bit/times/latin1.bold.10.font";
static char *authorfontname = "/lib/font/bit/times/latin1.7.font";
static char *copyfontname = "/lib/font/bit/lucidasans/unicode.7.font";	/* need © */
static char *adfontname = "/lib/font/bit/times/latin1.bold.6.font";
static char *cluefontname = "/lib/font/bit/lucidasans/unicode.7.font";

static Font *numfont, *letterfont, *titlefont, *authorfont, *copyfont, *adfont, *cluefont;
static Rectangle titlerect, authorrect, copyrect, acrossrect, downrect;

static Image *highlight, *grey, *writing;
static Xword *xword;
static Rectangle xrect;
static int nclue, clueindent;

static char *puzname;

enum {
	Squaresize = 25,	/* pixels in a square */
};
static Square **sq;
static Clue *clue;

static Cursor query = {
	{-7,-7},
	{0x0f, 0xf0, 0x1f, 0xf8, 0x3f, 0xfc, 0x7f, 0xfe, 
	 0x7c, 0x7e, 0x78, 0x7e, 0x00, 0xfc, 0x01, 0xf8, 
	 0x03, 0xf0, 0x07, 0xe0, 0x07, 0xc0, 0x07, 0xc0, 
	 0x07, 0xc0, 0x07, 0xc0, 0x07, 0xc0, 0x07, 0xc0, },
	{0x00, 0x00, 0x0f, 0xf0, 0x1f, 0xf8, 0x3c, 0x3c, 
	 0x38, 0x1c, 0x00, 0x3c, 0x00, 0x78, 0x00, 0xf0, 
	 0x01, 0xe0, 0x03, 0xc0, 0x03, 0x80, 0x03, 0x80, 
	 0x00, 0x00, 0x03, 0x80, 0x03, 0x80, 0x00, 0x00, }
};

static void
squaregeom(Rectangle xr)
{
	int r, c;
	int dy, dx;

	xrect = xr;
	dx = (Dx(xr)-2)/xword->wid;
	dy = (Dy(xr)-2)/xword->ht;
	
	for(r=0; r<xword->ht; r++)
	for(c=0; c<xword->wid; c++) {
		sq[r][c].r = rectaddpt(Rect(2,2,dx,dy), Pt(xr.min.x+c*dx, xr.min.y+r*dy));
	}
}

enum {
	CLUE,
	SQUARE,
};
static int
findclick(Point p, Clue **cp, Square **sp)
{
	int i, r, c;

	if(ptinrect(p, xrect)) {
		for(c=0; c<xword->wid; c++) 
			if(sq[0][c].r.min.x <= p.x && p.x < sq[0][c].r.max.x)
				break;
		if(c == xword->wid)
			return -1;
		for(r=0; r<xword->ht; r++)
			if(sq[r][0].r.min.y <= p.y && p.y < sq[r][0].r.max.y)
				break;
		if(r == xword->ht)
			return -1;
		*sp = &sq[r][c];
		assert(ptinrect(p, sq[r][c].r));
		return SQUARE;
	} else {
		for(i=0; i<nclue; i++) {
			if(ptinrect(p, clue[i].r)) {
				*cp = &clue[i];
				return CLUE;
			}
		}
		return -1;
	}
}

static void
initxword(void)
{
	int i, r, c, n;
	Xpt p;
	Clue *cl;

	sq = malloc(sizeof(*sq)*(xword->ht+2));
	if(sq == nil)
		sysfatal("out of memory");

	n = sizeof(**sq)*(xword->wid+2);
	for(r=0; r<xword->ht+2; r++) {
		sq[r] = malloc(n);
		if(sq[r] == nil)
			sysfatal("out of memory");
		memset(sq[r], 0, n);
		sq[r]++;
	}
	sq++;

	for(r=0; r<xword->ht; r++)
	for(c=0; c<xword->wid; c++) {
		sq[r][c].sqdown = &sq[r+1][c];
		sq[r][c].squp = &sq[r-1][c];

		if(xword->board[r*xword->wid+c] == '.') {
			sq[r][c].black = 1;
			sq[r][c].bg = display->black;
		} else {
			sq[r][c].bg = display->white;
			sq[r][c].c = xword->board[r*xword->wid+c];
			sq[r][c].cp = &xword->board[r*xword->wid+c];
			if(sq[r][c].c == '-')
				sq[r][c].c = ' ';
			if('a' <= sq[r][c].c && sq[r][c].c <= 'z') {
				sq[r][c].c += 'A'-'a';
				sq[r][c].notsure = 1;
			}
		}
	}

	for(r=0; r<xword->ht; r++) {
		sq[r][-1].black = 1;
		sq[r][xword->wid].black = 1;
	}
	for(c=0; c<xword->wid; c++) {
		sq[-1][c].black = 1;
		sq[xword->ht][c].black = 1;
	}

	for(i=0; i<xword->nloc; i++)
		sq[xword->loc[i].r][xword->loc[i].c].num = i+1;

	nclue = xword->nclue;
	clue = malloc(sizeof(*clue)*nclue);
	if(clue == nil)
		sysfatal("cannot allocate clues");
	memset(clue, 0, sizeof(*clue)*nclue);

	for(i=0; i<nclue; i++) {
		clue[i].num = xword->clue[i].num+1;
		clue[i].isdown = xword->clue[i].isdown;
		clue[i].text = xword->clue[i].text;
		clue[i].bg = display->white;
		clue[i].fg = display->black;
		p = xword->loc[xword->clue[i].num];
		clue[i].sq = &sq[p.r][p.c];
		if(clue[i].isdown)
			clue[i].sq->down = &clue[i];
		else
			clue[i].sq->across = &clue[i];
	}

	/* fill down */
	cl = nil;
	for(c=0; c<xword->wid; c++)
	for(r=0; r<xword->ht; r++) {
		if(sq[r][c].down)
			cl = sq[r][c].down;
		else if(sq[r][c].black == 0)
			sq[r][c].down = cl;
	}
	for(c=0; c<xword->wid; c++)
		sq[xword->ht][c].squp = &sq[xword->ht-1][c];

	/* fill across */
	cl = nil;
	for(r=0; r<xword->ht; r++) 
	for(c=0; c<xword->wid; c++) {
		if(sq[r][c].across)
			cl = sq[r][c].across;
		else if(sq[r][c].black == 0)
			sq[r][c].across = cl;
	}
}

static void
placetext(Rectangle *dstr, Rune *s, Rectangle **r, Rectangle *er, Rune **line, int *nlinep, Font *f)
{
	int i, dx, nline, mline;
	Rune *m, *p, *q, *nextq;

	/* out of space? */
	if(*r >= er) {
		*dstr = Rpt(ZP, ZP);
		line[0] = nil;
		*nlinep = 0;
		return;
	}

	/* break text into lines; assumes that all rectangles are same width */
	dx = Dx(**r);
	mline = *nlinep;

	/* p points at beginning of next line */
	m = p = runestrdup(s);
	if(p == nil) {
	Error:
		*dstr = Rpt(ZP, ZP);
		*r = er;	/* mark out of space */
		line[0] = nil;
		*nlinep = 0;
		free(m);
		return;
	}
	for(i=0; i<mline && *p; i++) {
		if((q = runestrchr(p, L' ')) == nil)
			q = p+runestrlen(p);
		else {
			while(*q) {
				nextq = runestrchr(q+1, ' ');
				if(nextq == nil)
					nextq = q+runestrlen(q);
				if(runestringnwidth(f, p, nextq-p) > dx)
					break;
				q = nextq;
			}
		}

		if(*q)
			*q++ = L'\0';
		line[i] = p;
		p = q;
	}
	nline = i;
	*nlinep = nline;

	/* can't fit vertically in rectangle? */
	if(nline*f->height > Dy(**r)) {
		(*r)++;
		if(*r >= er || nline*f->height > Dy(**r))
			goto Error;
	}

	/* save rectangle */
	*dstr = **r;
	dstr->max.y = dstr->min.y + nline*f->height;

	/* chop off space */
	(*r)->min.y = dstr->max.y;
}

static void
addspace(Rectangle **r, Rectangle *er, int dy)
{
	if(*r >= er)
		return;

	if(Dy(**r) <= dy) 
		(*r)++;
	else
		(*r)->min.y += dy;
}

static void
needspace(Rectangle **r, Rectangle *er, int dy)
{
	while(*r < er && Dy(**r) <= dy)
		(*r)++;
}

static void
geometry(void)
{
	char buf[10];
	int i, n, nr, y0, y1, y2, col, cwid, miny, maxy;
	Rune *s;
	Rectangle r[20], xr, *rp, *erp;
	Point corner;

	/*
	 * the puzzle itself goes in the upper left corner,
	 * under the title, author, and copyright notice.
	 */
	corner = screen->r.min;
	xr = canonrect(Rpt(corner, addpt(corner, Pt(Squaresize*xword->wid+2, Squaresize*xword->ht+2))));

	y0 = titlefont->height;
	y1 = authorfont->height;
	y2 = copyfont->height;

	xr = rectaddpt(xr, Pt(0, y0+y1+y2));
	squaregeom(xr);
	titlerect = rectaddpt(Rect(0, 0, Dx(xr), y0), corner);
	authorrect = rectaddpt(Rect(0, y0, Dx(xr), y0+y1), corner);
	copyrect = rectaddpt(Rect(0, y0+y1, Dx(xr), y0+y1+y2), corner);

	/*
	 * We want an integral number of columns
	 * under the puzzle.  150 pixels seems like a good minimum
	 * column width, including a 15 pixel gutter.
	 */
	col = (25*xword->wid+15)/150;
	if(col < 1)
		col = 1;
	cwid = (25*xword->wid+15)/col;
	if(cwid < 150)
		cwid = 150;

	miny = xr.max.y+4;
	maxy = screen->r.max.y-4;
	for(i=0; i<col; i++)
		r[i] = Rect(2+xr.min.x+i*cwid, miny, 2+xr.min.x+(i+1)*cwid-15, maxy);

	miny = screen->r.min.y+4;
	for(; xr.min.x+(i+1)*cwid-15 < screen->r.max.x; i++)
		r[i] = Rect(2+xr.min.x+i*cwid, miny, xr.min.x+(i+1)*cwid-15, maxy);
	nr = i;
	assert(nr <= 20);

	/* fill text */
	rp = r;
	erp = r+nr;

	/* indent space for numbers */
	sprint(buf, "%d", xword->nloc);
	clueindent = stringwidth(numfont, buf)+2;
	for(i=0; i<nr; i++)
		r[i].min.x += clueindent;

//	/* debugging */
//	for(i=0; i<nr; i++)
//		draw(screen, r[i], grey, nil, ZP);

	for(i=0; i<nclue; i++) {
		free(clue[i].line[0]);
		clue[i].line[0] = nil;
		clue[i].nline = 0;
	}

	n = 1;
	needspace(&rp, erp, 40);
	placetext(&acrossrect, L"ACROSS", &rp, erp, &s, &n, adfont);
	acrossrect.min.x -= clueindent;
	free(s);
	addspace(&rp, erp, 5);

	for(i=0; i<nclue; i++) {
		if(clue[i].isdown)
			continue;
		clue[i].nline = nelem(clue[i].line);
		placetext(&clue[i].r, clue[i].text, &rp, erp, clue[i].line, &clue[i].nline, cluefont);
		clue[i].r.min.x -= clueindent;	/* so clicking on numbers works; see drawclue */
	}

	addspace(&rp, erp, 10);
	needspace(&rp, erp, 40);
	n = 1;
	placetext(&downrect, L"DOWN", &rp, erp, &s, &n, adfont);
	downrect.min.x -= clueindent;
	free(s);
	addspace(&rp, erp, 5);

	for(i=0; i<nclue; i++) {
		if(clue[i].isdown==0)
			continue;
		clue[i].nline = nelem(clue[i].line);
		placetext(&clue[i].r, clue[i].text, &rp, erp, clue[i].line, &clue[i].nline, cluefont);
		clue[i].r.min.x -= clueindent;	/* so clicking on numbers works; see drawclue */
	}

}

static void
runecenterstring(Image *im, Rectangle r, Image *src, Font *f, Rune *s)
{
	Point d;

	d = runestringsize(f, s);
	if(d.x < Dx(r))
		r.min.x = (r.min.x+r.max.x-d.x)/2;
	runestring(im, r.min, src, ZP, f, s);
}

static void
drawsquare(Square *s)
{
	char buf[10];
	Rune rbuf[2];
	Rectangle r;
	Image *fg;

	if(s->black)
		return;

	if(s->writing)
		draw(screen, s->r, writing, nil, ZP);
	else
		draw(screen, s->r, s->bg, nil, ZP);

	if(s->num) {
		sprint(buf, "%d", s->num);
		string(screen, addpt(s->r.min, Pt(1,0)), display->black, ZP, numfont, buf);
	}
	if(s->c) {
		rbuf[0] = s->c;
		rbuf[1] = L'\0';
		r = s->r;
		r.min.y = r.max.y - letterfont->ascent - 2;
		if(s->notsure) {
			if(s->bg == highlight)
				fg = display->white;
			else
				fg = grey;
		} else
			fg = display->black;
		runecenterstring(screen, r, fg, letterfont, rbuf);
	}
}

static void
drawboard(void)
{
	int r, c;

	draw(screen, xrect, display->black, nil, ZP);
	for(r=0; r<xword->ht; r++)
	for(c=0; c<xword->wid; c++)
		drawsquare(&sq[r][c]);
}

static void
drawtitle(void)
{
	runecenterstring(screen, titlerect, display->black, titlefont, xword->title);
	runecenterstring(screen, authorrect, display->black, authorfont, xword->author);
	runecenterstring(screen, copyrect, display->black, copyfont, xword->copyr);

	runecenterstring(screen, acrossrect, display->black, adfont, L"ACROSS");
	runecenterstring(screen, downrect, display->black, adfont, L"DOWN");
}

static void
drawclue(Clue *c)
{
	int i;
	char buf[10];
	Rectangle r;

	r = c->r;
	draw(screen, r, c->bg, nil, ZP);
	sprint(buf, "%d", c->num);
	string(screen, r.min, display->black, ZP, numfont, buf);

	r.min.x += clueindent;
	for(i=0; i<c->nline; i++) {
		runestring(screen, r.min, display->black, ZP, cluefont, c->line[i]);
		r.min.y += cluefont->height;
	}
	r.min.x -= clueindent;
}

static void
drawclues(void)
{
	int i;

	for(i=0; i<nclue; i++)
		drawclue(&clue[i]);
}

static void
usage(void)
{
	fprint(2, "usage: games/xword file\n");
	exits("usage");
}

static Font*
eopenfont(char *name)
{
	Font *f;

	f = openfont(display, name);
	if(f == nil) {
		fprint(2, "warning: no font %s: %r\n", name);
		f = display->defaultfont;
	}
	return f;
}

void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		fprint(2, "can't reattach to window");

	draw(screen, screen->r, display->white, nil, ZP);
	geometry();
	drawtitle();
	drawboard();
	drawclues();
	flushimage(display, 1);
}

static void
sethighlight(Clue *c, Image *bg)
{
	Square *sq;

	if(c == nil)
		return;

	c->bg = bg;
	drawclue(c);

	if(c->isdown) {
		for(sq=c->sq; sq->black==0; sq=sq->sqdown) {
			sq->bg = bg;
			drawsquare(sq);
		}
	} else {
		for(sq=c->sq; sq->black==0; sq++) {
			sq->bg = bg;
			drawsquare(sq);
		}
	}
}

enum {
	CLEAR,
	NOTSURE,
};

enum {
	SAVE,
	QUIT,
};

static char *menuitem2[] = { "clear", "XXX", nil };
static Menu menu2= {
	menuitem2,
	nil,
	0
};

static char *menuitem3[] = { "Save", "Quit", nil };
static Menu menu3 = {
	menuitem3,
	nil,
	0
};

static void
confused(void)
{
	esetcursor(&query);
	sleep(1000);
	esetcursor(nil);
}

static void
play(void)
{
	Biobuf *b;
	Square *wsq, *nwsq, *sq;	
	Clue *c, *nc;
	Event e;
	int i, nsure, down, clicking, ch, checksave;
	
	down = 0;
	einit(Emouse|Ekeyboard);
	// add networked xword event

	c = nil;
	wsq = nil;
	clicking = 0;
	checksave = 0;
	for(;;) {
		switch(eread(Emouse|Ekeyboard, &e)) {
		case Ekeyboard:
			if(clicking)
				break;
			if(wsq == nil)
				break;
			ch = e.kbdc;
			if(ch == '\b') {
				if(down)
					nwsq = wsq->squp;
				else
					nwsq = wsq-1;
				if(nwsq && nwsq->black == 0) {
					wsq->writing = 0;
					drawsquare(wsq);
					wsq = nwsq;
					wsq->writing = 1;
					drawsquare(wsq);
				}	
				break;
			}
			if(wsq->black)
				break;
			if('a' <= ch && ch <= 'z')
				ch += 'A'-'a';
			if(('A' <= ch && ch <= 'Z') || ch == ' ') {
				wsq->c = ch;
				if(ch == ' ')
					*wsq->cp = L'-';
				else
					*wsq->cp = (Rune)ch;
				checksave = 1;
				wsq->notsure = 0;
				wsq->writing = 0;
				drawsquare(wsq);
				if(down)
					wsq = wsq->sqdown;
				else
					wsq++;
				if(wsq->black == 0) {
					wsq->writing = 1;
					drawsquare(wsq);
				}
			}
			break;
		case Emouse:
			if(clicking == 0 && wsq && e.mouse.buttons == (1<<1)) {
				if(wsq->black)
					nsure = (down ? wsq->squp : wsq-1)->notsure;
				else
					nsure = wsq->notsure;

				menuitem2[NOTSURE] = nsure ? "sure" : "notsure";
				nsure = !nsure;

				i = emenuhit(2, &e.mouse, &menu2);
				if(i < 0 || e.mouse.buttons != 0)
					break;
				switch(i){
				case CLEAR:
					for(sq=c->sq; sq->black==0; down ? sq=sq->sqdown : sq++) {
						sq->c = ' ';
						*sq->cp = L' ';
						checksave = 1;
						drawsquare(sq);
					}
				case NOTSURE:
					for(sq=c->sq; sq->black==0; down ? sq=sq->sqdown : sq++) {
						sq->notsure = nsure;
						*sq->cp = nsure ?  tolower(*sq->cp) : toupper(*sq->cp);
						drawsquare(sq);
					}
					break;
				}
				break;
			}
			if(clicking == 0 && e.mouse.buttons == (1<<2)) {
				i = emenuhit(3, &e.mouse, &menu3);
				if(i < 0 || e.mouse.buttons != 0)
					break;

				switch(i){
				case SAVE:
					if((b = Bopen(puzname, OWRITE)) == nil) {
						confused();
						break;
					}
					if(Bwrxword(b, xword) < 0) {
						Bterm(b);
						confused();
						break;
					}
					Bterm(b);
					checksave = 0;
					break;
				case QUIT:
					if(checksave) {
						confused();
						checksave = 0;
					} else
						exits(nil);
					break;
				}
				break;
			}

			if(e.mouse.buttons == 1)
				clicking = 1;

			if(clicking == 0 || e.mouse.buttons != 0)
				break;
			clicking = 0;

			switch(findclick(e.mouse.xy, &nc, &sq)) {
			case -1:
				if(c) {
					sethighlight(c, display->white);
					c = nil;
				}
				if(wsq && wsq->black==0) {
					wsq->writing = 0;
					drawsquare(wsq);
				}
				wsq = nil;
				break;
			case CLUE:
				if(wsq && wsq->black==0) {
					wsq->writing = 0;
					drawsquare(wsq);
				}
				if(c != nc) {
					sethighlight(c, display->white);
					sethighlight(nc, highlight);
					c = nc;
				}
				down = c->isdown;
				wsq = c->sq;
				wsq->writing = 1;
				drawsquare(wsq);
				break;
			case SQUARE:
				if(sq == wsq)
					down = !down;
				else if(sq != nil) {
					if(sq->down && sq->down->sq == sq
					&& (sq->across == nil || sq->across->sq != sq))
						down = 1;
					else if(sq->across && sq->across->sq == sq
					&& (sq->down == nil || sq->down->sq != sq))
						down = 0;
				}
				if(down)
					nc = sq->down;
				else
					nc = sq->across;
				if(c != nc) {
					sethighlight(c, display->white);
					sethighlight(nc, highlight);
					c = nc;
				}
				if(wsq && wsq->black==0) {
					wsq->writing = 0;
					drawsquare(wsq);
				}
				if(sq->black == 0) {
					wsq = sq;
					wsq->writing = 1;
					drawsquare(wsq);
				} else
					wsq = nil;
				break;
			}
			break;
		}
	}
}

void
main(int argc, char **argv)
{
	Xword *x;	
	Biobuf *b;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;

	if(argc != 1)
		usage();

	puzname = argv[0];

	if((b = Bopen(puzname, OREAD)) == nil)
		sysfatal("cannot open %s", argv[0]);

	if((x = Brdxword(b)) == nil)
		sysfatal("cannot read xword: %r");

	Bterm(b);
	initdraw(0, 0, "xword");

	highlight = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DPalegreygreen);
	writing = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DPurpleblue);

	grey = allocimage(display, Rect(0,0,1,1), screen->chan, 1, 0xAAAAAAFF);
	if(highlight == nil || grey == nil)
		sysfatal("allocimage: %r");

	numfont = eopenfont(numfontname);
	letterfont = eopenfont(letterfontname);
	titlefont = eopenfont(titlefontname);
	authorfont = eopenfont(authorfontname);
	copyfont = eopenfont(copyfontname);
	adfont = eopenfont(adfontname);
	cluefont = eopenfont(cluefontname);

	xword = x;
	initxword();
	eresized(0);
	play();
}
