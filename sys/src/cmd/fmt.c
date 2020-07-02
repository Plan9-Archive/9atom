#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>

/*
 * block up paragraphs, possibly with indentation
 */

int extraindent = 0;		/* how many spaces to indent all lines */
int indent = 0;			/* current value of indent, before extra indent */
int length = 70;		/* how many columns per output line */
int join = 1;			/* can lines be joined? */
int maxtab = 8;
char *quote;
Biobuf bin;
Biobuf bout;

typedef struct Word Word;
struct Word{
	int	bol;
	int	indent;
	int	quotes;
	char	text[1];
};

int
indentof(char **linep)
{
	int i, ind;
	char *line;

	ind = 0;
	line = *linep;
	for(i=0; line[i]; i++)
		switch(line[i]){
		default:
			*linep = line;
			return ind;
		case ' ':
			ind++;
			break;
		case '\t':
			ind += maxtab;
			ind -= ind%maxtab;
			break;
		}
			
	/* plain white space doesn't change the indent */
	*linep = "";
	return indent;
}

int
quoteof(char **p)
{
	char *s;
	int i, len;

	if(quote == nil)
		return 0;
	s = *p;
	len = strlen(quote);
	for(i = 0;; i++){
		if(strncmp(s, quote, len) != 0){
			*p = s;
			return i;
		}
		s += len;
	}
}
		
Word**
addword(Word **words, int *nwordp, char *s, int l, int indent, int bol, int quotes)
{
	Word *w;

	w = malloc(sizeof(Word)+l+1);
	memmove(w->text, s, l);
	w->text[l] = '\0';
	w->indent = indent;
	w->bol = bol;
	w->quotes = quotes;
	words = realloc(words, (*nwordp+1)*sizeof(Word*));
	words[(*nwordp)++] = w;
	return words;
}

Word**
parseline(char *line, Word **words, int *nwordp)
{
	int l, bol, quotes;

	quotes = quoteof(&line);
	indent = indentof(&line);
	bol = 1;
	for(;;){
		/* find next word */
		while(*line==' ' || *line=='\t')
			line++;
		if(*line == '\0'){
			if(bol)
				return addword(words, nwordp, "", 0, -1, bol, quotes);
			break;
		}
		/* how long is this word? */
		for(l=0; line[l]; l++)
			if(line[l]==' ' || line[l]=='\t')
				break;
		words = addword(words, nwordp, line, l, indent, bol, quotes);
		bol = 0;
		line += l;
	}
	return words;
}

void
printindent(int w)
{
	while(w >= maxtab){
		Bputc(&bout, '\t');
		w -= maxtab;
	}
	while(w > 0){
		Bputc(&bout, ' ');
		w--;
	}
}

/* give extra space if word ends with period, etc. */
int
nspaceafter(char *s)
{
	int n;

	n = strlen(s);
	if(n < 2)
		return 1;
	if(isupper(s[0]) && n < 4)
		return 1;
	if(strchr(".!?", s[n-1]) != nil)
		return 2;
	return 1;
}
	
int
quoteout(Word *line)
{
	int i;

	if(quote == nil)
		return 0;
	for(i = 0; i < line->quotes; i++)
		Bprint(&bout, "%s", quote);
	return line->quotes * strlen(quote);
}

void
printwords(Word **w, int nw)
{
	int i, j, n, col, nsp, fold;
	Word *line;

	/* one output line per loop */
	line = *w;
	fold = 0;
	for(i=0; i<nw; ){
		col = 0;

		/* if it's a blank line, print it */
		if(w[i]->indent == -1){
			quoteout(line);
			Bputc(&bout, '\n');
			if(++i == nw)	/* out of words */
				break;
		}
		if(!fold && w[i]->bol)
			line = w[i];
		fold = 0;
		/* emit leading indent */
		col += quoteout(line);
		j = extraindent+w[i]->indent;
		col += j;
		printindent(j);
		/* emit words until overflow; always emit at least one word */
		for(n=0;; n++){
			Bprint(&bout, "%s", w[i]->text);
			col += utflen(w[i]->text);
			if(++i == nw)
				break;	/* out of words */
			if(w[i]->bol && line != w[i] && w[i]->quotes != line->quotes)
				break;
			if(w[i]->indent != w[i-1]->indent)
				break;	/* indent change */
			nsp = nspaceafter(w[i-1]->text);
			if(col+nsp+utflen(w[i]->text) > extraindent+length){
				fold = 1;
				break;	/* fold line */
			}
			if(!join && w[i]->bol)
				break;
			for(j=0; j<nsp; j++)
				Bputc(&bout, ' ');	/* emit space; another word will follow */
			col += nsp;
		}
		/* emit newline */
		Bputc(&bout, '\n');
	}
}

void
fmt(void)
{
	char *s;
	int i, nw;
	Word **w;

	nw = 0;
	w = nil;
	while((s = Brdstr(&bin, '\n', 1)) != nil){
		w = parseline(s, w, &nw);
		free(s);
	}
	printwords(w, nw);
	for(i=0; i<nw; i++)
		free(w[i]);
	free(w);
}

void	fmt(void);

void
usage(void)
{
	fprint(2, "usage: %s [-j] [-i indent] [-l length] [file...]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, f;
	char *s, *err;

	ARGBEGIN{
	case 'i':
		extraindent = atoi(EARGF(usage()));
		break;
	case 'j':
		join = 0;
		break;
	case 'w':
	case 'l':
		length = atoi(EARGF(usage()));
		break;
	case 'q':
		quote = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(length <= indent){
		fprint(2, "%s: line length<=indentation\n", argv0);
		exits("length");
	}

	s=getenv("tabstop");
	if(s!=nil && atoi(s)>0)
		maxtab=atoi(s);
	err = nil;
	Binit(&bout, 1, OWRITE);
	if(argc <= 0){
		Binit(&bin, 0, OREAD);
		fmt();
	}else{
		for(i=0; i<argc; i++){
			f = open(argv[i], OREAD);
			if(f < 0){
				fprint(2, "%s: can't open %s: %r\n", argv0, argv[i]);
				err = "open";
			}else{
				Binit(&bin, f, OREAD);
				fmt();
				Bterm(&bin);
				if(i != argc-1)
					Bputc(&bout, '\n');
			}
		}
	}
	exits(err);
}

