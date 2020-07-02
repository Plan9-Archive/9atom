#pragma	src	"/sys/src/libregexp"
#pragma	lib	"libregexp.a"

enum {
	NSPANS	= 128,		/* max rune ranges per character class */
	NCLASS	= 16,		/* max character classes per program */
};

typedef struct Resub		Resub;
typedef struct Reclass		Reclass;
typedef struct Reinst		Reinst;
typedef struct Reprog		Reprog;

/*
 *	Sub expression matches
 */
struct Resub{
	union
	{
		char *sp;
		Rune *rsp;
	};
	union
	{
		char *ep;
		Rune *rep;
	};
};

/*
 *	character class, each pair of rune's defines a range
 */
struct Reclass{
	Rune	*end;
	Rune	spans[NSPANS*2];
};

/*
 *	Machine instructions
 */
struct Reinst{
	int	type;
	union	{
		Reclass	*cp;		/* class pointer */
		Rune	r;		/* character */
		int	subid;		/* sub-expression id for RBRA and LBRA */
		Reinst	*right;		/* right child of OR */
	};
	union {	/* regexp relies on these two being in the same union */
		Reinst *left;		/* left child of OR */
		Reinst *next;		/* next instruction for CAT & LBRA */
	};
};

/*
 *	Reprogram definition
 */
struct Reprog{
	Reinst	*startinst;	/* start pc */
	Reclass	class[NCLASS];	/* .data */
	Reinst	firstinst[5];	/* .text */
};

extern Reprog	*regcomp(char*);
extern Reprog	*regcomplit(char*);
extern Reprog	*regcompnl(char*);
extern void	regerror(char*);
extern int	regexec(Reprog*, char*, Resub*, int);
extern void	regsub(char*, char*, int, Resub*, int);
extern int	rregexec(Reprog*, Rune*, Resub*, int);
extern void	rregsub(Rune*, Rune*, int, Resub*, int);
