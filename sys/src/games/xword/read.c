#include <u.h>
#include <libc.h>
#include <bio.h>
#include "xword.h"

static void
rot13(Rune *s)
{
	for(; *s; s++) {
		if(('A' <= *s && *s <= 'M') || ('a' <= *s && *s <= 'm'))
			*s += 13;
		else if(('N' <= *s && *s <= 'Z') || ('n' <= *s && *s <= 'z'))
			*s -= 13;
	}
}

static Rune*
readboard(Biobuf *b, Xword *x)
{
	Rune *s, *p;
	int r, c;
	long ch;

	s = malloc((x->wid*x->ht+1)*sizeof(Rune));
	if(s == nil)
		return nil;
	
	p = s;
	for(r=0; r<x->ht; r++) {
		for(c=0; c<x->wid; c++) {
			if((ch = Bgetrune(b)) < 0) {
				werrstr("out of input");
				free(s);
				return nil;
			}
			if(ch == '.' || ch == '-' || ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z'))
				*p++ = ch;
			else if(ch == ' ')
				*p++ = '-';
			else {
				werrstr("unexpected char '%C'", (Rune)ch);
				free(s);
				return nil;
			}
		}
		while((ch = Bgetrune(b)) != '\n') {
			if(ch < 0) {
				werrstr("out of input");
				free(s);
				return nil;
			}
			if(ch != ' ' && ch != '\t' && ch != '\r') {
				werrstr("board line too long");
				free(s);
				return nil;
			}
		}
	}
	*p = '\0';
	return s;
}

Rune*
strtorune(char *s)
{
	Rune *t, *ot, *et;

	t = malloc(sizeof(Rune)*(utflen(s)+1));
	if(t == nil)
		return nil;

	ot = t;
	et = t+utflen(s);
	while(*s && t<et)
		s += chartorune(t++, s);
	*t = '\0';
	return ot;
}

static void
freexword(Xword *x)
{
	free(x->title);
	free(x->author);
	free(x->copyr);
	free(x->board);
	free(x->ans);
	free(x);
}

static int
getclue(Biobuf *b, Xclue *c)
{
	char *p, *q;
	int n;

	if((p = Brdline(b, '\n')) == nil) {
		werrstr("end of file");
		return -1;
	}
	p[Blinelen(b)-1] = '\0';

	if((n = strtol(p, &q, 10)) == 0 || (*q != 'a' && *q != 'd') || *(q+1) != ' ') {
		werrstr("bad format");
		return -1;
	}

	c->num = n-1;
	c->isdown = *q=='d';

	if((c->text = strtorune(q+2)) == nil)
		return -1;

	return 0;
}


static int
calclocs(Xword *x)
{
	int inc, nsq, nclue, r, c, ht, wid;

	ht = x->ht;
	wid = x->wid;

	nsq = 0;
	nclue = 0;
	for(r=0; r<ht; r++)
	for(c=0; c<wid; c++) {
		inc = 0;
		if(x->board[r*wid+c] != '.') {
			if(c == 0 || x->board[r*wid+c-1] == '.' && c != wid-1)
				nclue++, inc++;
			if(r == 0 || x->board[(r-1)*wid+c] == '.' && r != ht-1)
				nclue++, inc++;
			if(inc)
				nsq++;
		}
	}

	x->loc = malloc(nsq*sizeof(x->loc[0]));
	x->nloc = nsq;
	x->clue = malloc(nclue*sizeof(x->clue[0]));
	x->nclue = nclue;
	if(x->loc == nil || x->clue == nil)
		return -1;
	memset(x->loc, 0, nsq*sizeof(x->loc[0]));
	memset(x->clue, 0, nclue*sizeof(x->clue[0]));

	nsq = 0;
	for(r=0; r<ht; r++)
	for(c=0; c<wid; c++) {
		if(x->board[r*wid+c] != '.') {
			if((c == 0 || x->board[r*wid+c-1] == '.' && c != wid-1)
			|| (r == 0 || x->board[(r-1)*wid+c] == '.' && r != ht-1)) {
				x->loc[nsq] = (Xpt){r,c};
				nsq++;
			}
		}
	}
	assert(nsq == x->nloc);
	return 0;
}

Xword*
Brdxword(Biobuf *b)
{
	int i;
	char *p, *q;
	Xword *x;

	x = malloc(sizeof(*x));
	if(x == nil)
		return nil;
	memset(x, 0, sizeof(*x));

	while(p = Brdline(b, '\n')) {
		p[Blinelen(b)-1] = '\0';
		if(strcmp(p, "") == 0)
			break;
		if((q = strchr(p, '=')) == nil) {
			werrstr("bad format");
			freexword(x);
			return nil;
		}
		*q++ = '\0';
		if(strcmp(p, "TITLE") == 0)
			x->title = strtorune(q);
		else if(strcmp(p, "AUTHOR") == 0)
			x->author = strtorune(q);
		else if(strcmp(p, "COPY") == 0)
			x->copyr = strtorune(q);
		else if(strcmp(p, "DIM") == 0) {
			p = q;
			q = strchr(p, ' ');
			if(q == nil) {
				werrstr("bad dimensions");
				freexword(x);
				return nil;
			}
			x->wid = atoi(p);
			x->ht = atoi(q+1);
			if(x->wid*x->ht > 10000 || x->wid <= 0 || x->ht <= 0) {
				werrstr("bad dimensions %d %d", x->wid, x->ht);
				freexword(x);
				return nil;
			}
		}
	}
	if(p == nil) {
		werrstr("unexpected end of file");
		freexword(x);
		return nil;
	}

	/* read board */
	if((x->board = readboard(b, x)) == nil) {
		freexword(x);
		return nil;
	}

	/* read answers */
	if((x->ans = readboard(b, x)) == nil) {
		freexword(x);
		return nil;
	}
	rot13(x->ans);

	/* find locations */
	if(calclocs(x) < 0) {
		freexword(x);
		return nil;
	}

	/* read clues */
	for(i=0; i<x->nclue; i++) {
		if(getclue(b, &x->clue[i]) < 0) {
			freexword(x);
			return nil;
		}
	}

	return x;
}
