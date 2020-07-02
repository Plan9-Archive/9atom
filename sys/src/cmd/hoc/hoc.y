%{
#include <u.h>
#include <libc.h>

#include "hoc.h"
#define	code2(c1,c2)	code(c1); code(c2)
#define	code3(c1,c2,c3)	code(c1); code(c2); code(c3)
%}
%union {
	Symbol	*sym;	/* symbol table pointer */
	Inst	*inst;	/* machine instruction */
	int	narg;	/* number of arguments */
	Formal	*formals;	/* list of formal parameters */
}
%token	<sym>	NUMBER STRING PRINT VAR BLTIN UNDEF WHILE FOR IF ELSE
%token	<sym>	FUNCTION PROCEDURE RETURN FUNC PROC READ
%type	<formals>	formals
%type	<inst>	expr stmt asgn prlist stmtlist
%type	<inst>	cond while for if begin end 
%type	<sym>	procname
%type	<narg>	arglist
%right	'=' ADDEQ SUBEQ MULEQ DIVEQ MODEQ
%left	OR
%left	AND
%left	GT GE LT LE EQ NE
%left	'+' '-'
%left	'*' '/' '%'
%left	UNARYMINUS NOT INC DEC
%right	'^'
%%
list:	  /* nothing */
	| list '\n'
	| list defn '\n'
	| list asgn '\n'  { code2(xpop, STOP); return 1; }
	| list stmt '\n'  { code(STOP); return 1; } 
	| list expr '\n'  { code2(printtop, STOP); return 1; }
	| list error '\n' { yyerrok; }
	;
asgn:	  VAR '=' expr { code3(varpush,(Inst)$1,assign); $$=$3; }
	| VAR ADDEQ expr	{ code3(varpush,(Inst)$1,addeq); $$=$3; }
	| VAR SUBEQ expr	{ code3(varpush,(Inst)$1,subeq); $$=$3; }
	| VAR MULEQ expr	{ code3(varpush,(Inst)$1,muleq); $$=$3; }
	| VAR DIVEQ expr	{ code3(varpush,(Inst)$1,diveq); $$=$3; }
	| VAR MODEQ expr	{ code3(varpush,(Inst)$1,modeq); $$=$3; }
	;
stmt:	  expr	{ code(xpop); }
	| RETURN { defnonly("return"); code(procret); }
	| RETURN expr
	        { defnonly("return"); $$=$2; code(funcret); }
	| PROCEDURE begin '(' arglist ')'
		{ $$ = $2; code3(call, (Inst)$1, (Inst)$4); }
	| PRINT prlist	{ $$ = $2; }
	| while '(' cond ')' stmt end {
		($1)[1] = (Inst)$5;	/* body of loop */
		($1)[2] = (Inst)$6; }	/* end, if cond fails */
	| for '(' cond ';' cond ';' cond ')' stmt end {
		($1)[1] = (Inst)$5;	/* condition */
		($1)[2] = (Inst)$7;	/* post loop */
		($1)[3] = (Inst)$9;	/* body of loop */
		($1)[4] = (Inst)$10; }	/* end, if cond fails */
	| if '(' cond ')' stmt end {	/* else-less if */
		($1)[1] = (Inst)$5;	/* thenpart */
		($1)[3] = (Inst)$6; }	/* end, if cond fails */
	| if '(' cond ')' stmt end ELSE stmt end {	/* if with else */
		($1)[1] = (Inst)$5;	/* thenpart */
		($1)[2] = (Inst)$8;	/* elsepart */
		($1)[3] = (Inst)$9; }	/* end, if cond fails */
	| '{' stmtlist '}'	{ $$ = $2; }
	;
cond:	   expr 	{ code(STOP); }
	;
while:	  WHILE	{ $$ = code3(whilecode,STOP,STOP); }
	;
for:	  FOR	{ $$ = code(forcode); code3(STOP,STOP,STOP); code(STOP); }
	;
if:	  IF	{ $$ = code(ifcode); code3(STOP,STOP,STOP); }
	;
begin:	  /* nothing */		{ $$ = progp; }
	;
end:	  /* nothing */		{ code(STOP); $$ = progp; }
	;
stmtlist: /* nothing */		{ $$ = progp; }
	| stmtlist '\n'
	| stmtlist stmt
	;
expr:	  NUMBER { $$ = code2(constpush, (Inst)$1); }
	| VAR	 { $$ = code3(varpush, (Inst)$1, eval); }
	| asgn
	| FUNCTION begin '(' arglist ')'
		{ $$ = $2; code3(call,(Inst)$1,(Inst)$4); }
	| READ '(' VAR ')' { $$ = code2(varread, (Inst)$3); }
	| BLTIN '(' expr ')' { $$=$3; code2(bltin, (Inst)$1->u.ptr); }
	| '(' expr ')'	{ $$ = $2; }
	| expr '+' expr	{ code(add); }
	| expr '-' expr	{ code(sub); }
	| expr '*' expr	{ code(mul); }
	| expr '/' expr	{ code(div); }
	| expr '%' expr	{ code(mod); }
	| expr '^' expr	{ code (power); }
	| '-' expr   %prec UNARYMINUS   { $$=$2; code(negate); }
	| '+' expr   %prec UNARYMINUS   { $$=$2; }
	| expr GT expr	{ code(gt); }
	| expr GE expr	{ code(ge); }
	| expr LT expr	{ code(lt); }
	| expr LE expr	{ code(le); }
	| expr EQ expr	{ code(eq); }
	| expr NE expr	{ code(ne); }
	| expr AND expr	{ code(and); }
	| expr OR expr	{ code(or); }
	| NOT expr	{ $$ = $2; code(not); }
	| INC VAR	{ $$ = code2(preinc,(Inst)$2); }
	| DEC VAR	{ $$ = code2(predec,(Inst)$2); }
	| VAR INC	{ $$ = code2(postinc,(Inst)$1); }
	| VAR DEC	{ $$ = code2(postdec,(Inst)$1); }
	;
prlist:	  expr			{ code(prexpr); }
	| STRING		{ $$ = code2(prstr, (Inst)$1); }
	| prlist ',' expr	{ code(prexpr); }
	| prlist ',' STRING	{ code2(prstr, (Inst)$3); }
	;
defn:	  FUNC procname { $2->type=FUNCTION; indef=1; }
	    '(' formals ')' stmt { code(procret); define($2, $5); indef=0; }
	| PROC procname { $2->type=PROCEDURE; indef=1; }
	    '(' formals ')' stmt { code(procret); define($2, $5); indef=0; }
	;
formals:	{ $$ = 0; }
	| VAR			{ $$ = formallist($1, 0); }
	| VAR ',' formals	{ $$ = formallist($1, $3); }
	;
procname: VAR
	| FUNCTION
	| PROCEDURE
	;
arglist:  /* nothing */ 	{ $$ = 0; }
	| expr			{ $$ = 1; }
	| arglist ',' expr	{ $$ = $1 + 1; }
	;
%%
	/* end of grammar */
#include <bio.h>
#include <ctype.h>
char	*progname;
int	lineno = 1;
jmp_buf	begin;
int	indef;
char	*infile;	/* input file name */
Biobuf	*bin;		/* input file descriptor */
Biobuf	binbuf;
char	**gargv;	/* global argument list */
int	gargc;

int r = L'\n';	/* global for use by warning() */

int	backslash(int), follow(int, int, int);
void	defnonly(char*), run(void);
void	warning(char*, char*);

yylex(void)		/* hoc6 */
{
	while ((r=Bgetrune(bin)) == L' ' || r == L'\t')
		;
	if (r < 0)
		return 0;
	if (r == L'\\') {
		r = Bgetrune(bin);
		if (r == L'\n') {
			lineno++;
			return yylex();
		}
	}
	if (r == L'#') {		/* comment */
		while ((r = Bgetrune(bin)) != L'\n' && r >= 0)
			;
		if (r == L'\n')
			lineno++;
		return r;
	}
	if (r == L'.' || isdigit(r)) {	/* number */
		double d;
		Bungetrune(bin);
		Bgetd(bin, &d);
		yylval.sym = install("", NUMBER, d);
		return NUMBER;
	}
	if (isalpharune(r) || r == L'_') {
		Symbol *s;
		Rune sbuf[100], *p = sbuf;
		char   cbuf[100*UTFmax];
		do {
			if (p >= sbuf + sizeof(sbuf) - 1) {
				*p = 0;
				runeexecerror("name too long", sbuf);
			}
			*p++ = r;
		} while ((r = Bgetrune(bin)) >= 0 && (isalpharune(r) || r == L'_'));
		Bungetrune(bin);
		*p = 0;
		snprint(cbuf, sizeof cbuf, "%S", sbuf);
		if ((s=lookup(cbuf)) == 0)
			s = install(cbuf, UNDEF, 0.0);
		yylval.sym = s;
		return s->type == UNDEF ? VAR : s->type;
	}
	if (r == L'"') {	/* quoted string */
		Rune sbuf[100], *p;
		for (p = sbuf; (r = Bgetrune(bin)) != L'"'; p++) {
			if (r == L'\n' || r == Beof)
				execerror("missing quote", 0);
			if (p >= sbuf + sizeof(sbuf) - 1) {
				*p =0;
				runeexecerror("string too long", sbuf);
			}
			*p = backslash(r);
		}
		*p = 0;
		yylval.sym = (Symbol*)smprint("%S", sbuf);
		return STRING;
	}
	switch (r) {
	case '+':	return follow('+', INC, follow('=', ADDEQ, '+'));
	case '-':	return follow('-', DEC, follow('=', SUBEQ, '-'));
	case '*':	return follow('=', MULEQ, '*');
	case '/':	return follow('=', DIVEQ, '/');
	case '%':	return follow('=', MODEQ, '%');
	case '>':	return follow('=', GE, GT);
	case '<':	return follow('=', LE, LT);
	case '=':	return follow('=', EQ, '=');
	case '!':	return follow('=', NE, NOT);
	case '|':	return follow('|', OR, '|');
	case '&':	return follow('&', AND, '&');
	case '\n':	lineno++; return '\n';
	default:	return r;
	}
}

int
backslash(int r)	/* get next char with \'s interpreted */
{
	static char transtab[] = "b\bf\fn\nr\rt\t";
	char *p;

	if (r != L'\\')
		return r;
	r = Bgetrune(bin);
	if(r >= Runeerror)
		return r;
	if (islower(r) && (p = strchr(transtab, (char)r)))
		return (int)p[1];
	return r;
}

int
follow(int expect, int ifyes, int ifno)	/* look ahead for >=, etc. */
{
	int r = Bgetrune(bin);

	if (r == expect)
		return ifyes;
	Bungetrune(bin);
	return ifno;
}

void
yyerror(char* s)	/* report compile-time error */
{
/*rob
	warning(s, (char *)0);
	longjmp(begin, 0);
rob*/
	execerror(s, 0);
}

void
runeexecerror(char* s, Rune *t)	/* recover from run-time error */
{
	char buf[256];

	snprint(buf, sizeof buf, "%S", t);
	warning(s, buf);
	Bseek(bin, 0L, 2);		/* flush rest of file */
	restoreall();
	longjmp(begin, 0);
}

void
execerror(char* s, char *t)	/* recover from run-time error */
{
	warning(s, t);
	Bseek(bin, 0L, 2);		/* flush rest of file */
	restoreall();
	longjmp(begin, 0);
}

void
fpecatch(void)	/* catch floating point exceptions */
{
	execerror("floating point exception", 0);
}

void
intcatch(void)	/* catch interrupts */
{
	execerror("interrupt", 0);
}

void
run(void)	/* execute until EOF */
{
	setjmp(begin);
	for (initcode(); yyparse(); initcode())
		execute(progbase);
}

void
main(int argc, char* argv[])	/* hoc6 */
{
	static int first = 1;
#ifdef YYDEBUG
	extern int yydebug;
	yydebug=3;
#endif
	progname = argv[0];
	init();
	if (argc == 1) {	/* fake an argument list */
		static char *stdinonly[] = { "-" };

		gargv = stdinonly;
		gargc = 1;
	} else if (first) {	/* for interrupts */
		first = 0;
		gargv = argv+1;
		gargc = argc-1;
	}
	Binit(&binbuf, 0, OREAD);
	bin = &binbuf;
	while (moreinput())
		run();
	exits(0);
}

moreinput(void)
{
	char *expr;
	static char buf[64];
	int fd;
	static Biobuf b;

	if (gargc-- <= 0)
		return 0;
	if (bin && bin != &binbuf)
		Bterm(bin);
	infile = *gargv++;
	lineno = 1;
	if (strcmp(infile, "-") == 0) {
		bin = &binbuf;
		infile = 0;
		return 1;
	}
	if(strncmp(infile, "-e", 2) == 0) {
		if(infile[2]==0){
			if(gargc == 0){
				fprint(2, "%s: no argument for -e\n", progname);
				return 0;
			}
			gargc--;
			expr = *gargv++;
		}else
			expr = infile+2;
		sprint(buf, "/tmp/hocXXXXXXX");
		infile = mktemp(buf);
		fd = create(infile, ORDWR|ORCLOSE, 0600);
		if(fd < 0){
			fprint(2, "%s: can't create temp. file: %r\n", progname);
			return 0;
		}
		fprint(fd, "%s\n", expr);
		/* leave fd around; file will be removed on exit */
		/* the following looks weird but is required for unix version */
		bin = &b;
		seek(fd, 0, 0);
		Binit(bin, fd, OREAD);
	} else {
		bin=Bopen(infile, OREAD);
		if (bin == 0) {
			fprint(2, "%s: can't open %s\n", progname, infile);
			return moreinput();
		}
	}
	return 1;
}

void
warning(char *s, char *t) /* print warning message */
{
	fprint(2, "%s: %s", progname, s);
	if (t)
		fprint(2, " %s", t);
	if (infile)
		fprint(2, " in %s", infile);
	fprint(2, " near line %d\n", lineno);
	while (r != L'\n' && r != Beof)
		if((r = Bgetrune(bin)) == L'\n')	/* flush rest of input line */
			lineno++;
}

void
defnonly(char *s)	/* warn if illegal definition */
{
	if (!indef)
		execerror(s, "used outside definition");
}
