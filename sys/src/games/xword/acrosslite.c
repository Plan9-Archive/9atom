#include <u.h>
#include <libc.h>
#include <bio.h>
#include "xword.h"

/* turn latin1 to runes */
static Rune*
latin1runedup(char *s)
{
	char *p;
	Rune *q, *ns;

	ns = malloc(sizeof(Rune)*(strlen(s)+1));
	if(ns == nil)
		return nil;

	for(p=s, q=ns; *q++ = (uchar)*p++; )
		;

	return ns;
}

static int
getclue(Xclue *clue, char **p, char *edata, int n, int isdown)
{
	clue->num = n;
	clue->isdown = isdown;
	clue->text = latin1runedup(*p);
	if(clue->text == nil)
		return -1;

	*p += strlen(*p)+1;
	if(*p > edata)
		return -1;
	return 0;
}

Xword*
readacrosslite(char *fn)
{
	Biobuf *b;
	Xword *x;
	char *p, data[8192], *edata;
	int i, inc, r, c, n, wid, ht, nclue, nsq;

	if((b = Bopen(fn, OREAD)) == nil)
		return nil;

	x = malloc(sizeof(*x));
	if(x == nil) {
	Error0:
		Bterm(b);
		return nil;
	}
	memset(x, 0, sizeof(*x));

	werrstr("bad file format");
	memset(data, 0, sizeof data);
	if((n=Bread(b, data, sizeof data)) == sizeof data) {
	Error1:
		free(x);
		goto Error0;
	}
	edata = data+n;

	x->ht = ht = data[0x2C];
	x->wid = wid = data[0x2D];
	if(x->ht <= 0 || x->wid <= 0 || 0x34+2*wid*ht >= n)
		goto Error1;

	x->board = malloc(sizeof(Rune)*(wid*ht+1));
	x->ans = malloc(sizeof(Rune)*(wid*ht+1));
	if(x->board == nil || x->ans == nil) {
	Error2:
		free(x->board);
		free(x->ans);
		goto Error1;
	}

	for(i=0; i<wid*ht; i++)
		x->ans[i] = (uchar)data[0x34+i];
	x->ans[wid*ht] = '\0';

	for(i=0; i<wid*ht; i++)
		x->board[i] = (uchar)data[0x34+wid*ht+i];
	x->board[wid*ht] = '\0';

	p = data+0x34+2*wid*ht;

	x->title = latin1runedup(p);
	p += strlen(p)+1;
	if(x->title == nil)
		goto Error2;
	if(p >= edata) {
	Error3:
		free(x->title);
		goto Error2;
	}

	x->author = latin1runedup(p);
	p += strlen(p)+1;
	if(x->author == nil)
		goto Error3;
	if(p >= edata) {
	Error4:
		free(x->author);
		goto Error3;
	}

	x->copyr = latin1runedup(p);
	p += strlen(p)+1;
	if(x->copyr == nil)
		goto Error4;
	if(p >= edata) {
	Error5:
		free(x->copyr);
		goto Error4;
	}

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

	nsq = 0;
	nclue = 0;
	for(r=0; r<ht; r++)
	for(c=0; c<wid; c++) {
		inc = 0;
		if(x->board[r*wid+c] != '.') {
			if(c == 0 || x->board[r*wid+c-1] == '.' && c != wid-1) {
				if(getclue(&x->clue[nclue++], &p, edata, nsq, 0) < 0) {
				Error6:
					for(nclue--; nclue>=0; nclue--)
						free(x->clue[nclue].text);
					goto Error5;
				}
				inc = 1;
			}
			if(r == 0 || x->board[(r-1)*wid+c] == '.' && r != ht-1) {
				if(getclue(&x->clue[nclue++], &p, edata, nsq, 1) < 0)
					goto Error6;
				inc = 1;
			}
			if(inc) {
				x->loc[nsq] = (Xpt){r, c};
				nsq++;
			}
		}
	}
	assert(nclue == x->nclue);
	assert(nsq == x->nloc);
	Bterm(b);

	return x;
}

void
usage(void)
{
	fprint(2, "usage: acrosslite file.puz\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	Biobuf bout;
	Xword *x;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	if((x=readacrosslite(argv[0])) == nil)
		sysfatal("cannot read puzzle: %r");

	Binit(&bout, 1, OWRITE);
	if(Bwrxword(&bout, x) < 0)
		sysfatal("write error: %r");

	Bterm(&bout);
	exits(nil);
}
