#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

/* colorflood: make all squares of the same 
 * color within the number of clicks allowed 
 */

enum{
	/* difficulty levels (how many circles are initially occupied) */
	DEasy = 14,
	DMed = 21,
	DHard = 28,

	/* board sizes for difficulty setting */
	SzE = 14,
	SzM = 21,
	SzH = 28,

	Ncolors = 6,
	Gap = 2, 	/* empty space between squares */

	Won = 1,	/* game-ending states */
	Lost = 2,
};

Font *font;
Image *brdr;

int lvl = DEasy;	/* difficulty level; array size for board */
int finished;

int moves;
int selected = -1;
int sx, sy, barsize;

int *level;
int *olevel;	/* so we can restart levels */

Image 	*colors[Ncolors];
int cc[Ncolors] = {
	DPalegreyblue,
	DYellow,
	DPalegreen,
	DGreen,
	DBluegreen,
	DRed,
};

int maxmoves[]= {
	[DEasy] = 25,
	[DMed] = 35,
	[DHard] = 50,
};


char *mbuttons[] = 
{
	"easy",
	"medium",
	"hard",
	0
};

char *rbuttons[] = 
{
	"new",
	"restart",
	"exit",
	0
};

Menu mmenu = 
{
	mbuttons,
};

Menu rmenu =
{
	rbuttons,
};

Image *
eallocimage(Rectangle r, int repl, uint color)
{
	Image *tmp;

	tmp = allocimage(display, r, screen->chan, repl, color);
	if(tmp == nil)
		sysfatal("cannot allocate buffer image: %r");

	return tmp;
}

void
allocimages(void)
{
	Rectangle one = Rect(0, 0, 1, 1);
	int i;
	for(i = 0; i < Ncolors; i++)
		colors[i] = eallocimage(one, 1, cc[i]);
	brdr = eallocimage(one, 1, 0x333333FF);
}


void
initlevel(void)
{
	int i;

	free(level);
	free(olevel);

	level = malloc(lvl*lvl*sizeof(int));
	olevel = malloc(lvl*lvl*sizeof(int));
	if(level == nil || olevel == nil)
		sysfatal("malloc");

	for(i = 0; i < lvl*lvl; i++)
		olevel[i] = nrand(Ncolors);

	memcpy(level, olevel, lvl*lvl*sizeof(int));
	moves = 0;
	selected=level[0];
}

void
drawlevel(void)
{
	Point p;
	char *s;
	int i;

	barsize = (Dy(screen->r)-lvl*Gap-barsize)/DEasy;
	sx = (Dx(screen->r)-lvl*Gap)/(lvl);
	sy = (Dy(screen->r)-lvl*Gap-barsize)/(lvl+1);

	draw(screen, screen->r, display->white, nil, ZP);

	for(i = 0; i < Ncolors; i++) {
		p = addpt(screen->r.min, Pt(i*(barsize/2+Gap)+5, 2));
		draw(screen, Rpt(p, addpt(p, Pt(barsize/2, barsize/2))), colors[i], nil, ZP);
		if(selected == i)
			border(screen, Rpt(p, addpt(p, Pt(barsize/2, barsize/2))), -1, brdr, ZP);
	}
	p = Pt((Ncolors+1)*sx, 2);
	if(finished)
		s = smprint("%s", finished == Won ? "won!" : "fail!");
	else
		s = smprint("%d/%d", moves, maxmoves[lvl]);
	string(screen, addpt(screen->r.min, p), display->black, ZP, font, s);
	free(s);

	for(i = 0; i < lvl*lvl; i++) {
		p = addpt(screen->r.min, Pt((i%lvl)*(sx+Gap)+5, (i/lvl)*(sy+Gap)));
		p = addpt(p, Pt(0, barsize)); /* move down */
		draw(screen, Rpt(p, addpt(p, Pt(sx, sy))), colors[level[i]], nil, ZP);
	}
	flushimage(display, 1);
}

void
domove(int x, int y, int nc)
{
	int oc = level[x*lvl+y];
	level[x*lvl+y] = nc;
	
	if(x > 0 && level[(x-1)*lvl+y] == oc)
		domove(x-1, y, nc);
	if(x+1 < lvl && level[(x+1)*lvl+y] == oc)
		domove(x+1, y, nc);
	if(y > 0 && level[x*lvl + y - 1] == oc)
		domove(x, y-1, nc);
	if(y+1 < lvl && level[x*lvl+y+1] == oc)
		domove(x, y+1, nc);

}


int
checkfinished(void)
{
	int i;

	for(i = 1; i < lvl*lvl; i++)
		if(level[i] != level[i-1])
			goto done;
	return Won;
done:
	if(moves >= maxmoves[lvl])
		return Lost;
	return 0;
}

void
move(Point m)
{
	Point p = subpt(m, screen->r.min);
	int ns;

	if(p.y < barsize/2+2 && p.x < Ncolors*barsize/2+Ncolors*Gap+5) {
		ns = (p.x-5)/(barsize/2+Gap);
		if(ns != selected) {
			selected = ns;
			domove(0, 0, selected);
			moves++;
			drawlevel();
		}
	}
	else if(ptinrect(p, Rect(5, barsize/2+2, 5+sx*lvl, barsize/2+2+sy*lvl))) {
		p=subpt(m,screen->r.min);
		p=subpt(p, Pt(5+Gap, barsize+Gap));
		ns=level[(p.x/(sx+Gap))+((p.y/(sy+Gap)))*lvl];
		if(ns != selected) {
			selected = ns;
			domove(0, 0, selected);
			moves++;
			drawlevel();
		}
	}
}

void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		sysfatal("can't reattach to window");
	drawlevel();
}

void 
main(int argc, char **argv)
{
	Mouse m;
	Event ev;
	int e, mousedown=0;
	char *fontname;

	USED(argv, argc);

	if(initdraw(nil, nil, "glendy") < 0)
		sysfatal("initdraw failed: %r");
	einit(Emouse);

	srand(time(0));

	allocimages();
	initlevel();	/* must happen before "eresized" */
	eresized(0);

	fontname = "/lib/font/bit/lucidasans/unicode.8.font";
	if((font = openfont(display, fontname)) == nil)
		sysfatal("font '%s' not found", fontname);	

	for(;;) {
		e = event(&ev);
		switch(e) {
		case Emouse:
			m = ev.mouse;
			if(m.buttons == 0) {
				if(mousedown && !finished) {
					mousedown = 0;
					move(m.xy);
					finished = checkfinished();
					drawlevel();
				}
			}
			if(m.buttons&1) {
				mousedown = 1;
			}
			if(m.buttons&2) {
				switch(emenuhit(2, &m, &mmenu)) {
				case 0:
					lvl = DEasy;
					initlevel();
					break;
				case 1:				
					lvl = DMed;
					initlevel();
					break;
				case 2:
					lvl = DHard;
					initlevel();
					break;
				}
				drawlevel();
			}
			if(m.buttons&4) {
				switch(emenuhit(3, &m, &rmenu)) {
				case 0:
					initlevel();
					break;
				case 1:
					memcpy(level, olevel, lvl*lvl*sizeof(int));
					finished = 0;
					moves = 0;
					break;
				case 2:
					exits(nil);
				}
				drawlevel();
			}
			break;
		}
	}
}
