#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

Image	*hrhand;
Image	*minhand;
Image	*dots;
Image	*back;
int	sflag;

Point
circlept(Point c, int r, int degrees)
{
	double rad;

	rad = (double) degrees * PI/180.0;
	c.x += cos(rad)*r;
	c.y -= sin(rad)*r;
	return c;
}

Point
clockpoint(Point c, int r, double pt)
{
	double rad;

	rad = PI/2.0 - (double)pt*2.0*PI/60.0;
	c.x += cos(rad)*r;
	c.y -= sin(rad)*r;
	return c;
}

char *ntab[] = {
	0,
	"sqrt 9 - {sqrt {9}!} over {sqrt 9}")
	"sqrt 9 - 9 over 9")
	"sqrt {9}! - 9 over {sqrt 9}")
	"sqrt 9 + 9 over 9")
	"sqrt {9}! - 9 over 9")
	"9-{9 over {sqrt 9}}")
	"sqrt {9}! + 9 over 9")
	"9 - 9 over 9")
	"sqrt {9}! + 9 over {sqrt 9}")
	"sqrt 9 + 9 over 9")
	"9 +  {sqrt {9}!} over {sqrt 9}")
	"9+{9 over {sqrt 9}}"
};

void
redraw(Image *screen)
{
	Rectangle r;
	Point c;
	Tm *tm;
	int rad, i;
	static ulong t;
	double pthr;

	tm = localtime(time(0));

	pthr = 5*(tm->hour + (tm->min*60+tm->sec)/3600);
	r = screen->r;
	c = divpt(addpt(r.min, r.max), 2);
	c.x += font->height;
	rad = Dx(r) < Dy(r) ? Dx(r) : Dy(r);
	rad /= 2;
	rad -= 8+2*font->height;

	draw(screen, screen->r, back, nil, ZP);
	for(i=0; i<12; i++)
{
		char buf[10];
		snprint(buf, sizeof buf, "%d", i);
		string(screen, clockpoint(c, rad, i*60/12.0), display->black, ZP, font, buf);
}
//		fillellipse(screen, circlept(c, rad, i*(360/12)), 2, 2, dots, ZP);

	if(sflag)
		line(screen, c, clockpoint(c, rad*7/8, tm->sec), 0, 0, 0, minhand, ZP);
	line(screen, c, clockpoint(c, (rad*3)/4, tm->min), 0, Endarrow, 1, minhand, ZP);
	line(screen, c, clockpoint(c, rad/2, pthr), 0, Endarrow, 1, hrhand, ZP);

	flushimage(display, 1);
}

void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		fprint(2,"can't reattach to window");
	redraw(screen);
}

void
usage(void)
{
	fprint(2, "usage: 9clock [-s]\n");
}

void
main(int argc, char **argv)
{
	Event e;
	Menu menu;
	char *mstr[] = {"exit", 0};
	int timer;

	ARGBEGIN{
	case 's':
		sflag ^= 1;
		break;
	default:
		usage();
	}ARGEND

	if(initdraw(0, 0, "9clock") < 0)
		sysfatal("initdraw: %r");
	back = allocimagemix(display, DPalebluegreen, DWhite);

	hrhand = allocimage(display, Rect(0,0,1,1), CMAP8, 1, DDarkblue);
	minhand = allocimage(display, Rect(0,0,1,1), CMAP8, 1, DPaleblue);
	dots = allocimage(display, Rect(0,0,1,1), CMAP8, 1, DBlue);
	redraw(screen);

	einit(Emouse|Ekeyboard);
	if(sflag)
		timer = etimer(0, 1000);
	else
		timer = etimer(0, 30000);

	menu.item = mstr;
	menu.lasthit = 0;
	for(;;) {
		switch(eread(Emouse|Ekeyboard|timer, &e)){
		case Ekeyboard:
			if(e.kbdc==0x7F || e.kbdc=='q')
				exits("");
			break;
		case Emouse:
			if(e.mouse.buttons & 4)
			if(emenuhit(3, &e.mouse, &menu))
				exits("");
			break;
		default:
			redraw(screen);
			break;
		}
	}	
}
