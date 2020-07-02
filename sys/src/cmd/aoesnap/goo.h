#define	nelem(x)	(sizeof(x)/sizeof((x)[0]))
#define fmtinstall fmtinstall_
enum{
	UTFmax		= 4,		/* maximum bytes per rune */
	Runesync	= 0x80,		/* cannot represent part of a UTF sequence (<) */
	Runeself	= 0x80,		/* rune and UTF sequences are the same (<) */
	Runeerror	= 0xFFFD,	/* decoding error in UTF */
	Runemax	= 0x10FFFF,	/* 32 bit rune */
};
int	chartorune(Rune *rune, char *str);

int	getfields(char*, char**, int, int, char*);
char*	strchr(char*, int);
int	strcmp(char*, char*);
uvlong	strtoull(char*, char**, int);

/* only the supported modes */
enum{
	OREAD		= 0,
	OWRITE	= 1,
	ORDWR		= 2,
	OTRUNC	= 16,
};

int	close(int);
int	create(char*, int, int);
int	open(char*, int);
long	read(int, void*, long);
long	write(int, void*, long);
long	pread(int, void*, long, vlong);
long	pwrite(int, void*, long, vlong);

int	atoi(char*);
int	atexit(void (*)(void));
void	exits(char*);
int	memcmp(void*, void*, ulong);
void*	memset(void*, int, ulong);
void*	memmove(void*, void*, ulong);
void*	malloc(ulong);
void*	realloc(void*, ulong);
void	free(void*);
int	errstr(char*, uint);
char*	strstr(char*, char*);
long	time(long*);

enum {
	FmtSharp	= 0200,
	FmtSign		= 0100,
	FmtZero 	= 040,
	FmtComma 		= 020,
	FmtVLong		= 010,
	FmtUnsigned 	= 004,
	FmtShort 		= 002,
	FmtLong 		= 001,
};

typedef struct Fmt	Fmt;
struct Fmt 
{
	char	*buf;
	char	*p;
	char	*ep;
	va_list	args;
	int	f1;
	int	f2;
	int	f3;
	int	verb;
	int	(*flush)(Fmt*);
	void	*farg;
	int	n;
};

extern void exits(char*);
extern long write(int, void*, long);
extern long strlen(char*);

extern int vfmtprint(Fmt*, char*, va_list);
extern int fmtprint(Fmt*, char*, ...);
extern int fmtinstall(int, int(*)(Fmt*));
extern int print(char *, ...);
extern int fprint(int, char*, ...);
extern int vsnprint(char*, int, char*, va_list);
extern int snprint(char*, int, char*, ...);
extern char *seprint(char*, char*, char*, ...);
extern void sysfatal(char*, ...);

#ifndef LINUX
#pragma	varargck	argpos	fprint		2
#pragma	varargck	argpos	print		1
#pragma	varargck	argpos	seprint		3
#pragma	varargck	argpos	snprint		3

#pragma	varargck	type	"lld"	vlong
#pragma	varargck	type	"llx"	vlong
#pragma	varargck	type	"lld"	uvlong
#pragma	varargck	type	"llx"	uvlong
#pragma	varargck	type	"ld"	long
#pragma	varargck	type	"lx"	long
#pragma	varargck	type	"lb"	long
#pragma	varargck	type	"ld"	ulong
#pragma	varargck	type	"lx"	ulong
#pragma	varargck	type	"lb"	ulong
#pragma	varargck	type	"d"	int
#pragma	varargck	type	"x"	int
#pragma	varargck	type	"c"	int
#pragma	varargck	type	"b"	int
#pragma	varargck	type	"d"	uint
#pragma	varargck	type	"x"	uint
#pragma	varargck	type	"c"	uint
#pragma	varargck	type	"b"	uint
#pragma	varargck	type	"s"	char*
#pragma	varargck	type	"r"	void
#pragma	varargck	type	"%"	void
#pragma	varargck	type	"p"	uintptr
#pragma	varargck	type	"p"	void*
#pragma	varargck	flag	','
#pragma	varargck	flag	'h'
#endif

enum{
	ERRMAX	= 128,
};

typedef	struct	Biobuf	Biobuf;
typedef	struct	Biobufhdr	Biobufhdr;

enum
{
	Bsize		= 8*1024,
	Bungetsize	= 4,		/* space for ungetc */
	Bmagic		= 0x314159,
	Beof		= -1,
	Bbad		= -2,

	Binactive	= 0,		/* states */
	Bractive,
	Bwactive,
	Bracteof,
};

struct	Biobufhdr
{
	int	icount;		/* neg num of bytes at eob */
	int	ocount;		/* num of bytes at bob */
	int	rdline;		/* num of bytes after rdline */
	int	runesize;	/* num of bytes of last getrune */
	int	state;		/* r/w/inactive */
	int	fid;		/* open file */
	int	flag;		/* magic if malloc'ed */
	vlong	offset;		/* offset of buffer in file */
	int	bsize;		/* size of buffer */
	uchar*	bbuf;		/* pointer to beginning of buffer */
	uchar*	ebuf;		/* pointer to end of buffer */
	uchar*	gbuf;		/* pointer to good data in buf */
};

struct	Biobuf
{
	Biobufhdr h;
	uchar	b[Bungetsize+Bsize];
};

int	Bfildes(Biobufhdr*);
int	Bflush(Biobufhdr*);
int	Bgetc(Biobufhdr*);
int	Binit(Biobuf*, int, int);
int	Binits(Biobufhdr*, int, int, uchar*, int);
int	Blinelen(Biobufhdr*);
vlong	Boffset(Biobufhdr*);
Biobuf*	Bopen(char*, int);
int	Bprint(Biobufhdr*, char*, ...);
int	Bvprint(Biobufhdr*, char*, va_list);
int	Bputc(Biobufhdr*, int);
long	Bread(Biobufhdr*, void*, long);
int	Bterm(Biobufhdr*);
int	Bungetc(Biobufhdr*);
long	Bwrite(Biobufhdr*, void*, long);

#ifndef LINUX
#pragma	varargck	argpos	Bprint	2
#endif

extern char *argv0;
#define	ARGBEGIN	if(!argv0) argv0=*argv;\
			for(argv++,argc--;\
			    argv[0] && argv[0][0]=='-' && argv[0][1];\
			    argc--, argv++) {\
				char *_args, *_argt;\
				Rune _argc;\
				_args = &argv[0][1];\
				if(_args[0]=='-' && _args[1]==0){\
					argc--; argv++; break;\
				}\
				_argc = 0;\
				while(*_args && (_args += chartorune(&_argc, _args)))\
				switch(_argc)
#define	ARGEND	SET(_argt);USED(_argt);USED(_argc);USED(_args);}USED(argv);USED(argc);
#define	EARGF(x)	(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): ((x, (char*)0))))
