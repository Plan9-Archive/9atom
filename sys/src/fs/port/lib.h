/*
 * functions (possibly) linked in, complete, from libc.
 */
#define	nelem(x)	(sizeof(x)/sizeof((x)[0]))
#define offsetof(s, m)	(ulong)(&(((s*)0)->m))

/*
 * mem routines
 */
extern	void*	memset(void*, int, ulong);
extern	int	memcmp(void*, void*, ulong);
extern	void*	memmove(void*, void*, ulong);
extern	void*	memchr(void*, int, ulong);

/*
 * string routines
 */
extern	int	cistrcmp(char*, char*);
extern	int	cistrncmp(char*, char*, int);
extern	char*	strcat(char*, char*);
extern	char*	strchr(char*, int);
extern	char*	strrchr(char*, int);
extern	int	strcmp(char*, char*);
extern	char*	strcpy(char*, char*);
extern	char*	strncat(char*, char*, long);
extern	char*	strncpy(char*, char*, long);
extern	int	strncmp(char*, char*, long);
extern	long	strlen(char*);

enum
{
	UTFmax		= 4,		/* maximum bytes per rune */
	Runesync	= 0x80,		/* cannot represent part of a UTF sequence (<) */
	Runeself	= 0x80,		/* rune and UTF sequences are the same (<) */
	Runeerror	= 0xFFFD,	/* decoding error in UTF */
	Runemax	= 0x10FFFF,	/* 32 bit rune */
};

/*
 * rune routines
 */
extern	int	runetochar(char*, Rune*);
extern	int	chartorune(Rune*, char*);
extern	char*	utfrune(char*, long);
extern	char*	utfrrune(char*, long);
extern	int	utflen(char*);
extern	int	runelen(long);

/*
 *  math
 */
extern	int	abs(int);

/*
 * print routines
 */
enum{
	FmtWidth	= 1,
	FmtLeft		= FmtWidth << 1,
	FmtPrec		= FmtLeft << 1,
	FmtSharp	= FmtPrec << 1,
	FmtSpace	= FmtSharp << 1,
	FmtSign		= FmtSpace << 1,
	FmtZero		= FmtSign << 1,
	FmtUnsigned	= FmtZero << 1,
	FmtShort	= FmtUnsigned << 1,
	FmtLong		= FmtShort << 1,
	FmtVLong	= FmtLong << 1,
	FmtComma	= FmtVLong << 1,
	FmtByte		= FmtComma << 1,

	FmtFlag		= FmtByte << 1
};

typedef struct Fmt	Fmt;
typedef int (*Fmts)(Fmt*);
struct Fmt{
	uchar	runes;			/* output buffer is runes or chars? */
	void	*start;			/* of buffer */
	void	*to;			/* current place in the buffer */
	void	*stop;			/* end of the buffer; overwritten if flush fails */
	int	(*flush)(Fmt *);	/* called when to == stop */
	void	*farg;			/* to make flush a closure */
	int	nfmt;			/* num chars formatted so far */
	va_list	args;			/* args passed to dofmt */
	int	r;			/* % format Rune */
	int	width;
	int	prec;
	ulong	flags;
};

extern	int	print(char*, ...);
extern	char*	seprint(char*, char*, char*, ...);
extern	char*	vseprint(char*, char*, char*, va_list);
extern	int	snprint(char*, int, char*, ...);
extern	int	sprint(char*, char*, ...);

extern	int	fmtinstall(int c, int (*f)(Fmt*));
extern	void	quotefmtinstall(void);
extern	int	fmtit(Fmt *f, char *fmt, ...);
extern	int	fmtstrcpy(Fmt *f, char *s);

#pragma	varargck	argpos	fmtit	2
#pragma	varargck	argpos	print	1
#pragma	varargck	argpos	seprint	3
#pragma	varargck	argpos	snprint	3
#pragma	varargck	argpos	sprint	2

#pragma	varargck	type	"lld"	vlong
#pragma	varargck	type	"llx"	vlong
#pragma	varargck	type	"lld"	uvlong
#pragma	varargck	type	"llx"	uvlong
#pragma	varargck	type	"ld"	long
#pragma	varargck	type	"lx"	long
#pragma	varargck	type	"ld"	ulong
#pragma	varargck	type	"lx"	ulong
#pragma	varargck	type	"d"	int
#pragma	varargck	type	"x"	int
#pragma	varargck	type	"c"	int
#pragma	varargck	type	"C"	int
#pragma	varargck	type	"d"	uint
#pragma	varargck	type	"x"	uint
#pragma	varargck	type	"c"	uint
#pragma	varargck	type	"C"	uint
/* no floating-point verbs */
#pragma	varargck	type	"s"	char*
#pragma	varargck	type	"q"	char*
#pragma	varargck	type	"S"	Rune*
#pragma	varargck	type	"Q"	Rune*
#pragma	varargck	type	"r"	void
#pragma	varargck	type	"%"	void
#pragma	varargck	type	"n"	int*
#pragma	varargck	type	"p"	void*
#pragma	varargck	type	"p"	ulong
#pragma	varargck	flag	','
#pragma varargck	type	"<" void*
#pragma varargck	type	"[" void*
#pragma varargck	type	"H" void*
#pragma varargck	type	"lH" void*

/*
 * one-of-a-kind
 */
extern	void	qsort(void*, long, long, int (*)(void*, void*));
extern	char	end[];

extern	int	decrypt(void*, void*, int);
extern	int	encrypt(void*, void*, int);
extern	int	nrand(int);
extern	void	srand(long);

#define	OREAD	0	/* open for read */
#define	OWRITE	1	/* write */
#define	ORDWR	2	/* read and write */
#define	OEXEC	3	/* execute, == read but check execute permission */
#define	OTRUNC	16	/* or'ed in (except for exec), truncate file first */
#define	OCEXEC	32	/* or'ed in, close on exec */
#define	ORCLOSE	64	/* or'ed in, remove on close */
