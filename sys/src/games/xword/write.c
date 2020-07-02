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

int
Bwrxword(Biobuf *b, Xword *x)
{
	int i, r;

	if(runestrlen(x->board) < x->wid*x->ht) {
		werrstr("board data incomplete");
		return -1;
	}

	if(Bprint(b, "TITLE=%S\n", x->title) < 0
	|| Bprint(b, "AUTHOR=%S\n", x->author) < 0
	|| Bprint(b, "COPY=%S\n", x->copyr) < 0
	|| Bprint(b, "DIM=%d %d\n", x->wid, x->ht) < 0)
		return -1;

	Bprint(b, "\n");
	for(r=0; r<x->ht; r++)
		if(Bprint(b, "%.*S\n", x->wid, x->board+r*x->wid) < 0)
			return -1;

	rot13(x->ans);
	for(r=0; r<x->ht; r++)
		if(Bprint(b, "%.*S\n", x->wid, x->ans+r*x->wid) < 0) {
			rot13(x->ans);
			return -1;
		}
	rot13(x->ans);

	for(i=0; i<x->nclue; i++) {
		if(Bprint(b, "%d%c %S\n", x->clue[i].num+1,
			x->clue[i].isdown ? 'd' : 'a', x->clue[i].text) < 0)
			return -1;
	}

	if(Bflush(b) < 0)
		return -1;

	return 0;
}
