#include <u.h>
#include <libc.h>

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

enum{
	Vbox,
	Hbox
	Tbox,
};

typedef struct Box Box;
struct Box{
	Box	*next;
	Point	sz;		/* computed size */
	Rect	glue;		/* additional space around edges */
	Image	*img;
	int	type;

	int	nelem;		/* leaves */
	Font	*font;
	union{
		char *s;
		Point pt;
	} e[];
};

typedef struct{
	char	tok[50];
	int	ntok;
	int	ttype;
	int	eof;
	Font	*font;
	Box	*b, *c;
	char	*s;
}Lex;

Lex ldat;

enum{
	End,
	Sqrt,
	Over,
	Text,
	Left,
};

Point
boxsize(Box *b)
{
	Box *prev;
	Pt p, p1;
	int mode;

	p = Pt(0, 0);
	mode = Hbox;
loop:
	switch(t->type){
	case Vbox:
	case Hbox:
		mode = t->type;
		goto step;
	}

	if(b->sz.x > 0 || b->sz.y > 0)
		p1 = b->sz;
	else
		p1 = Point(stringwidth(b->font, b->tok), font->height);

	switch(mode){
	case Hbox:
		if(p1.y > p.y)
			p.y = p1.y;
		p.x += p1.x;
		break;
	case Vbox:
		if(p1.x > p.x)
			p.x = p1.x;
		p.y += p1.y;
	}

step:
	b = b->next;
	if(b)
		goto loop;
	return p;
}

Box*
appendbox(Box *h, Box *b)
{
	if(h){
		p = h;
		while(p->next)
			p = p->next;
		p->next = b;
	} else
		h = b;
	return h;
}

Box*
newbox(int type, void *e, int ne)
{
	Box *b, *p;

	b = malloc(sizeof *b+ne);
	b->font = ldat.font;
	memset(b, 0, sizeof b);
	memcpy(b->e, e, ne);
	return b;
}

struct{
	int	type;
	char	*tok;
	int	len;
}key[] = {
	Over,	"over",	4,
	Sqrt,	"sqrt",	4,
};

int
lex0(Lex *l)
{
	int c;

	while(isspace(*l->s))
		l->s++;
	l->ntok = 0;

	switch(c = *l->s++){
	case 0:
		return 0;
	case '{':
	case '}':
		return c;
	default:
	loop:
		if(l->ntok == nelem(l->tok)-1)
			sysfatal("token too long");
		l->tok[l->ntok++] = c;
		c = *l->s++;
		if(isalpha(c) || isdigit(c))
			goto loop;
		for(i = 0; i < nelem(key); i++)
			if(key[i].len == l->ntok)
			if(memcmp(key[i].tok, l->tok, l->ntok) == 0){
				l->ntok = 0;
				return key[i].type;
			}
		l->ok[l->ntok] = 0;		/* cheet */
		return Text;
	}
}

void
lexinit(char *s)
{
	ldat.s = s;
	ldat.eof = 0;
	ldat.ntok = -1;
	ldat.c = 0;
	ldat.b = 0;
	ldat.font = font;
}

int
lex(void)
{
	int t;

	if(ldat.eof)
		return 0;
	t = lex(&ldat);
	if(t == 0)
		ldat.eof = 1;
	ldat.ttype = t;
	return t;
}

Point sqrtpt[] = {
	-0.3, .6,
	-0.6, 1.0,
	-1.6, 0.0,
	1.0, 0.0,
	1.0, 0.2,
};

Point
scale(Pt scale, Point *t, Point *d, int n)
{
	Point bb;
	int i;

	for(i = 0; i < n; i++){
		if(t[i].x < 0)
			d[i].x = t[i].x*scale.y;
		else
			d[i].x = t[i].x.scale.x;
		if(t[i].y < 0)
			d[i].y = t[i].y*scale.x;
		else
			d[i].y = t[i].y.scale.y;
	}
}
	
void
elem(void)
{
	Box *b, *b1;
	Pt p, sq[4];

loop:
	lex();
	switch(ldat.ttype){
	case '{':
		parse0(s);
		if(lex(s) != '}')
			print("parse error: expecting '{'");
		break;
	case Text:
		ldat.c = appendbox(ldat.c, newbox(Tbox, ldat.tok, ldat.ntok+1));
		break;
	case Sqrt:
		b = ldat.c;
		ldat.c = 0;
		elem();
		b1 = newbox(Tbox, sqrtpt, sizeof sqrtpt)
		p = boxsize(ldat.c);
		p.x += font.height/3;
		scale(p, sqrtpt, b1->e, nelem(sqrtpt));

		ldat.c = appendbox(b, );
		break;
	default:
		return;
	}
	goto loop;
}

Box*
parse0(void)
{
	Box *b0, *b1, b2;

loop:
	elem();
	switch(ldat.ttype){
	case Over:
		b0 = ldat.c;
		ldat.c = 0;
		elem();
		b1 = ldat.c;
		ldat.c = newbox(Vbox, 0, 0);
		ldat.c = appendbox(ldat.c, b0);

		ldat.c = appendbox(ldat.c, b1);
	case End:
		return;
	default:
		print("parse error\n");
	}
	goto loop;
}

Box*
parse(char *s)
{
	lexinit(s);
	return parse0(s);
}

/*
 * testing crapo
 */
Box *thebox;
Image *back;

void
redraw(Image *screen)
{
	draw(screen, screen->r, thebox->image, nil, ZP);
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
main(int argc, char **argv)
{
	Event e;
	Menu menu;
	char *mstr[] = {"exit", 0};

	if(initdraw(0, 0, "parse") < 0)
		sysfatal("initdraw: %r");
	back = allocimagemix(display, DPalebluegreen, DWhite);
	thebox = parse0(ntab[1]);

	einit(Emouse|Ekeyboard);

	menu.item = mstr;
	menu.lasthit = 0;
	for(;;) {
		switch(eread(Emouse|Ekeyboard, &e)){
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
