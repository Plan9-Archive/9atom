#include "rc.h"
#include "io.h"
#include "exec.h"
#include "fns.h"
#include "getflags.h"
#define	c0	t->child[0]
#define	c1	t->child[1]
#define	c2	t->child[2]
int codep, ncode;
#define	emitf(x) ((codep!=ncode || morecode()), codebuf[codep].f = (x), codep++)
#define	emiti(x) ((codep!=ncode || morecode()), codebuf[codep].i = (x), codep++)
#define	emits(x) ((codep!=ncode || morecode()), codebuf[codep].s = (x), codep++)
void stuffdot(int);
char *fnstr(tree*);
void outcode(tree*, int, int);
void codeswitch(tree*, int, int);
int iscase(tree*);
code *codecopy(code*);
void codefree(code*);

int
morecode(void)
{
	ncode+=100;
	codebuf = (code *)erealloc(codebuf, ncode*sizeof codebuf[0]);
	return 0;
}

void
stuffdot(int a)
{
	if(a<0 || codep<=a)
		panic("Bad address %d in stuffdot", a);
	codebuf[a].i = codep;
}

void
backpatch(int q, int pc, int n)
{
	int i;
	for(i = q; i <= pc; i++)
		if(codebuf[i].i == n)
			codebuf[i].i = pc;
}

int
newbrkaddr(void)
{
	static int i = 1;

	return --i;
}

int
compile(tree *t)
{
	ncode = 100;
	codebuf = (code *)emalloc(ncode*sizeof codebuf[0]);
	codep = 0;
	emiti(0);			/* reference count */
	outcode(t, flag['e']?1:0, newbrkaddr());
	if(nerror){
		efree((char *)codebuf);
		return 0;
	}
	readhere();
	emitf(Xreturn);
	emitf(0);
	return 1;
}

void
cleanhere(char *f)
{
	emitf(Xdelhere);
	emits(strdup(f));
}

char*
fnstr(tree *t)
{
	io *f = openstr();
	void *v;
	extern char nl;
	char svnl = nl;

	nl = ';';
	pfmt(f, "%t", t);
	nl = svnl;
	v = f->strp;
	f->strp = 0;
	closeio(f);
	return v;
}

void
outcode(tree *t, int eflag, int brkaddr)
{
	int p, q, n;
	tree *tt;
	if(t==0)
		return;
	if(t->type!=NOT && t->type!=';')
		runq->iflast = 0;
	switch(t->type){
	default:
		pfmt(err, "bad type %d in outcode\n", t->type);
		break;
	case '$':
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		emitf(Xdol);
		break;
	case '"':
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		emitf(Xqdol);
		break;
	case SUB:
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		emitf(Xmark);
		outcode(c1, eflag, brkaddr);
		emitf(Xsub);
		break;
	case '&':
		emitf(Xasync);
		if(havefork){
			p = emiti(0);
			outcode(c0, eflag, brkaddr);
			emitf(Xexit);
			stuffdot(p);
		} else
			emits(fnstr(c0));
		break;
	case ';':
		outcode(c0, eflag, brkaddr);
		outcode(c1, eflag, brkaddr);
		break;
	case '^':
		emitf(Xmark);
		outcode(c1, eflag, brkaddr);
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		emitf(Xconc);
		break;
	case '`':
		emitf(Xmark);
		if(c0==0){
			emitf(Xword);
			emits(strdup("ifs"));
			emitf(Xdol);
		}else{
			outcode(c0, 0, brkaddr);
			emitf(Xglob);
		}
		emitf(Xbackq);
		if(havefork){
			p = emiti(0);
			outcode(c1, 0, brkaddr);
			emitf(Xexit);
			stuffdot(p);
		} else
			emits(fnstr(c1));
		break;
	case ANDAND:
		outcode(c0, 0, brkaddr);
		emitf(Xtrue);
		p = emiti(0);
		outcode(c1, eflag, brkaddr);
		stuffdot(p);
		break;
	case ARGLIST:
		outcode(c1, eflag, brkaddr);
		outcode(c0, eflag, brkaddr);
		break;
	case BANG:
		outcode(c0, eflag, brkaddr);
		emitf(Xbang);
		break;
	case PCMD:
	case BRACE:
		outcode(c0, eflag, brkaddr);
		break;
	case COUNT:
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		emitf(Xcount);
		break;
	case FN:
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		if(c1){
			emitf(Xfn);
			p = emiti(0);
			emits(fnstr(c1));
			outcode(c1, eflag, brkaddr);
			emitf(Xunlocal);	/* get rid of $* */
			emitf(Xreturn);
			stuffdot(p);
		}
		else
			emitf(Xdelfn);
		break;
	case IF:
		outcode(c0, 0, brkaddr);
		emitf(Xif);
		p = emiti(0);
		outcode(c1, eflag, brkaddr);
		emitf(Xwastrue);
		stuffdot(p);
		break;
	case NOT:
		if(!runq->iflast)
			yyerror("`if not' does not follow `if(...)'");
		emitf(Xifnot);
		p = emiti(0);
		outcode(c0, eflag, brkaddr);
		stuffdot(p);
		break;
	case OROR:
		outcode(c0, 0, brkaddr);
		emitf(Xfalse);
		p = emiti(0);
		outcode(c1, eflag, brkaddr);
		stuffdot(p);
		break;
	case PAREN:
		outcode(c0, eflag, brkaddr);
		break;
	case SIMPLE:
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		emitf(Xsimple);
		if(eflag)
			emitf(Xeflag);
		break;
	case SUBSHELL:
		emitf(Xsubshell);
		if(havefork){
			p = emiti(0);
			outcode(c0, eflag, brkaddr);
			emitf(Xexit);
			stuffdot(p);
		} else
			emits(fnstr(c0));
		if(eflag)
			emitf(Xeflag);
		break;
	case SWITCH:
		codeswitch(t, eflag, brkaddr);
		break;
	case TWIDDLE:
		emitf(Xmark);
		outcode(c1, eflag, brkaddr);
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		emitf(Xmatch);
		if(eflag)
			emitf(Xeflag);
		break;
	case BREAK:
		if(brkaddr == 0)
			yyerror("break outside of loop");
		emitf(Xjump);
		emiti(brkaddr);
		//print("brkaddr = %d\n", brkaddr);
		break;
	case WHILE:
		q = codep;
		outcode(c0, 0, brkaddr);
		if(q==codep)
			emitf(Xsettrue);	/* empty condition == while(true) */
		emitf(Xtrue);
		p = emiti(0);
		outcode(c1, eflag, n = newbrkaddr());
		emitf(Xjump);
		emiti(q);
		stuffdot(p);
		backpatch(q, codep, n);
		break;
	case WORDS:
		outcode(c1, eflag, brkaddr);
		outcode(c0, eflag, brkaddr);
		break;
	case FOR:
		emitf(Xmark);
		if(c1){
			outcode(c1, eflag, brkaddr);
			emitf(Xglob);
		}
		else{
			emitf(Xmark);
			emitf(Xword);
			emits(strdup("*"));
			emitf(Xdol);
		}
		emitf(Xmark);		/* dummy value for Xlocal */
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		emitf(Xlocal);
		p = emitf(Xfor);
		q = emiti(0);
		outcode(c2, eflag, n = newbrkaddr());
		emitf(Xjump);
		emiti(p);
		stuffdot(q);
		backpatch(q, codep, n);
		emitf(Xunlocal);
		break;
	case WORD:
		emitf(Xword);
		emits(strdup(t->str));
		break;
	case DUP:
		if(t->rtype==DUPFD){
			emitf(Xdup);
			emiti(t->fd0);
			emiti(t->fd1);
		}
		else{
			emitf(Xclose);
			emiti(t->fd0);
		}
		outcode(c1, eflag, brkaddr);
		emitf(Xpopredir);
		break;
	case PIPEFD:
		emitf(Xpipefd);
		emiti(t->rtype);
		if(havefork){
			p = emiti(0);
			outcode(c0, eflag, brkaddr);
			emitf(Xexit);
			stuffdot(p);
		} else {
			emits(fnstr(c0));
		}
		break;
	case REDIR:
		emitf(Xmark);
		outcode(c0, eflag, brkaddr);
		emitf(Xglob);
		switch(t->rtype){
		case APPEND:
			emitf(Xappend);
			break;
		case WRITE:
			emitf(Xwrite);
			break;
		case READ:
		case HERE:
			emitf(Xread);
			break;
		case RDWR:
			emitf(Xrdwr);
			break;
		}
		emiti(t->fd0);
		outcode(c1, eflag, brkaddr);
		emitf(Xpopredir);
		break;
	case '=':
		tt = t;
		for(;t && t->type=='=';t = c2);
		if(t){					/* var=value cmd */
			for(t = tt;t->type=='=';t = c2){
				emitf(Xmark);
				outcode(c1, eflag, brkaddr);
				emitf(Xmark);
				outcode(c0, eflag, brkaddr);
				emitf(Xlocal);		/* push var for cmd */
			}
			outcode(t, eflag, brkaddr);	/* gen. code for cmd */
			for(t = tt; t->type == '='; t = c2)
				emitf(Xunlocal);	/* pop var */
		}
		else{					/* var=value */
			for(t = tt;t;t = c2){
				emitf(Xmark);
				outcode(c1, eflag, brkaddr);
				emitf(Xmark);
				outcode(c0, eflag, brkaddr);
				emitf(Xassign);	/* set var permanently */
			}
		}
		t = tt;	/* so tests below will work */
		break;
	case PIPE:
		emitf(Xpipe);
		emiti(t->fd0);
		emiti(t->fd1);
		if(havefork){
			p = emiti(0);
			q = emiti(0);
			outcode(c0, eflag, brkaddr);
			emitf(Xexit);
			stuffdot(p);
		} else {
			emits(fnstr(c0));
			q = emiti(0);
		}
		outcode(c1, eflag, brkaddr);
		emitf(Xreturn);
		stuffdot(q);
		emitf(Xpipewait);
		break;
	}
	if(t->type!=NOT && t->type!=';')
		runq->iflast = t->type==IF;
	else if(c0) runq->iflast = c0->type==IF;
}
/*
 * switch code looks like this:
 *	Xmark
 *	(get switch value)
 *	Xjump	1f
 * out:	Xjump	leave
 * 1:	Xmark
 *	(get case values)
 *	Xcase	1f
 *	(commands)
 *	Xjump	out
 * 1:	Xmark
 *	(get case values)
 *	Xcase	1f
 *	(commands)
 *	Xjump	out
 * 1:
 * leave:
 *	Xpopm
 */

void
codeswitch(tree *t, int eflag, int brkaddr)
{
	int leave;		/* patch jump address to leave switch */
	int out;		/* jump here to leave switch */
	int nextcase;	/* patch jump address to next case */
	tree *tt;
	if(c1->child[0]==nil
	|| c1->child[0]->type!=';'
	|| !iscase(c1->child[0]->child[0])){
		yyerror("case missing in switch");
		return;
	}
	emitf(Xmark);
	outcode(c0, eflag, brkaddr);
	emitf(Xjump);
	nextcase = emiti(0);
	out = emitf(Xjump);
	leave = emiti(0);
	stuffdot(nextcase);
	t = c1->child[0];
	while(t->type==';'){
		tt = c1;
		emitf(Xmark);
		for(t = c0->child[0];t->type==ARGLIST;t = c0) outcode(c1, eflag, brkaddr);
		emitf(Xcase);
		nextcase = emiti(0);
		t = tt;
		for(;;){
			if(t->type==';'){
				if(iscase(c0)) break;
				outcode(c0, eflag, brkaddr);
				t = c1;
			}
			else{
				if(!iscase(t)) outcode(t, eflag, brkaddr);
				break;
			}
		}
		emitf(Xjump);
		emiti(out);
		stuffdot(nextcase);
	}
	stuffdot(leave);
	emitf(Xpopm);
}

int
iscase(tree *t)
{
	if(t->type!=SIMPLE)
		return 0;
	do t = c0; while(t->type==ARGLIST);
	return t->type==WORD && !t->quoted && strcmp(t->str, "case")==0;
}

code*
codecopy(code *cp)
{
	cp[0].i++;
	return cp;
}

void
codefree(code *cp)
{
	code *p;
	if(--cp[0].i!=0)
		return;
	for(p = cp+1;p->f;p++){
		if(p->f==Xappend || p->f==Xclose || p->f==Xread || p->f==Xwrite
		|| p->f==Xrdwr
		|| p->f==Xasync || p->f==Xbackq || p->f==Xcase || p->f==Xfalse
		|| p->f==Xfor || p->f==Xjump
		|| p->f==Xsubshell || p->f==Xtrue) p++;
		else if(p->f==Xdup || p->f==Xpipefd) p+=2;
		else if(p->f==Xpipe) p+=4;
		else if(p->f==Xword || p->f==Xdelhere) efree((++p)->s);
		else if(p->f==Xfn){
			efree(p[2].s);
			p+=2;
		}
	}
	efree((char *)cp);
}
