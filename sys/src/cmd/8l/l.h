#include	<u.h>
#include	<libc.h>
#include	<bio.h>
#include	"../8c/8.out.h"
#include	"../8l/elf.h"

#ifndef	EXTERN
#define	EXTERN	extern
#endif

#define	P		((Prog*)0)
#define	S		((Sym*)0)
#define	TNAME		(curtext?curtext->from.sym->name:noname)

#define	cput(c)\
	{ *cbp++ = c;\
	if(--cbc <= 0)\
		cflush(); }

typedef	struct	Adr	Adr;
typedef	struct	Prog	Prog;
typedef	struct	Sym	Sym;
typedef	struct	Auto	Auto;
typedef	struct	Optab	Optab;
typedef struct	Movtab	Movtab;

struct	Adr
{
	union
	{
		long	u0offset;
		char	u0scon[8];
		Prog	*u0cond;	/* not used, but should be D_BRANCH */
		Ieee	u0ieee;
	} u0;
	union
	{
		Auto*	u1autom;
		Sym*	u1sym;
	} u1;
	short	type;
	uchar	index;
	char	scale;
};

#define	offset	u0.u0offset
#define	scon	u0.u0scon
#define	cond	u0.u0cond
#define	ieee	u0.u0ieee

#define	autom	u1.u1autom
#define	sym	u1.u1sym

struct	Prog
{
	Adr	from;
	Adr	to;
	Prog	*forwd;
	Prog*	link;
	Prog*	pcond;	/* work on this */
	long	pc;
	long	line;
	short	as;
	char	width;		/* fake for DATA */
	char	ft;		/* oclass cache */
	char	tt;
	uchar	mark;	/* work on these */
	uchar	back;
};
struct	Auto
{
	Sym*	asym;
	Auto*	link;
	long	aoffset;
	short	type;
};
struct	Sym
{
	char	*name;
	short	type;
	short	version;
	short	become;
	short	frame;
	uchar	subtype;
	ushort	file;
	long	value;
	long	sig;
	Sym*	link;
};
struct	Optab
{
	short	as;
	uchar*	ytab;
	ushort	prefix;
	uchar	op[20];
};
struct	Movtab
{
	short	as;
	uchar	ft;
	uchar	tt;
	uchar	code;
	uchar	op[4];
};

enum
{
	STEXT		= 1,
	SDATA,
	SBSS,
	SDATA1,
	SXREF,
	SFILE,
	SCONST,
	SUNDEF,

	SIMPORT,
	SEXPORT,

	NHASH		= 10007,
	NHUNK		= 100000,
	MINSIZ		= 4,
	STRINGSZ	= 200,
	MINLC		= 1,
	MAXIO		= 8192,
	MAXHIST		= 20,				/* limit of path elements for history symbols */

	Yxxx		= 0,
	Ynone,
	Yi0,
	Yi1,
	Yi8,
	Yi32,
	Yiauto,
	Yal,
	Ycl,
	Yax,
	Ycx,
	Yrb,
	Yrl,
	Yrf,
	Yf0,
	Yrx,
	Ymb,
	Yml,
	Ym,
	Ybr,
	Ycol,

	Ycs,	Yss,	Yds,	Yes,	Yfs,	Ygs,
	Ygdtr,	Yidtr,	Yldtr,	Ymsw,	Ytask,
	Ycr0,	Ycr1,	Ycr2,	Ycr3,	Ycr4,	Ycr5,	Ycr6,	Ycr7,
	Ydr0,	Ydr1,	Ydr2,	Ydr3,	Ydr4,	Ydr5,	Ydr6,	Ydr7,
	Ytr0,	Ytr1,	Ytr2,	Ytr3,	Ytr4,	Ytr5,	Ytr6,	Ytr7,
	Yxr,	Yxm,	Yyr,	Yxyr,
	Ymax,

	Zxxx		= 0,

	Zlit,
	Z_rp,
	Zbr,
	Zcall,
	Zib_,
	Zib_rp,
	Zibo_m,
	Zibo_m_xm,
	Zil_,
	Zil_rp,
	Zilo_m,
	Zjmp,
	Zloop,
	Zo_iw,
	Zm_o,
	Zm_r,
	Zm_r_xm,
	Zm_r_i_xm,
	Zm_r_3d,
	Zm_r_xm_nr,
	Zr_m_xm_nr,
	Zibm_r,	/* mmx1,mmx2/mem64,imm8 */
	Zmb_r,
	Zaut_r,
	Zo_m,
	Zpseudo,
	Zr_m,
	Zr_m_xm,
	Zr_m_i_xm,
	Zrp_,
	Z_ib,
	Z_il,
	Zm_ibo,
	Zm_ilo,
	Zib_rr,
	Zil_rr,
	Zclr,
	Zbyte,
	Zmov,
	Zmax,

	Px		= 0,
	Pe		= 0x66,	/* operand escape */
	Pm		= 0x0f,	/* 2byte opcode escape */
	Pq		= 0xff,	/* both escape */
	Pb		= 0xfe,	/* byte operands */
	Pf2		= 0xf2,	/* xmm escape 1 */
	Pf3		= 0xf3,	/* xmm escape 2 */
	Pm38	= 0x38,	/* 0f.38 opcode */
	Pm3a	= 0x3a,	/* 0f.3a opcode */
	Pw		= 0x48,	/* Rex.w (avx only) */
	Py		=	0,	/* TO DO */

	P2		=	1<<9,	/* flag: two operand (avx only) */

	Rxf		= 1<<9,	/* internal flag for Rxr on from */
	Rxt		= 1<<8,	/* internal flag for Rxr on to */
	Rxw		= 1<<3,	/* =1, 64-bit operand size */
	Rxr		= 1<<2,	/* extend modrm reg */
	Rxx		= 1<<1,	/* extend sib index */
	Rxb		= 1<<0,	/* extend modrm r/m, sib base, or opcode reg */

	Vex2	= 0xc5,	/* 2-byte vex prefix */
	Vex3	= 0xc4,	/* 3-byte vex prefix */

	/* vex flags */
	Vexr	= 1<<7,		/* byte 1, both */
	Vexx	= 1<<6,		/* byte 1, 3-byte */
	Vexb	= 1<<5,		/* byte 1, 3-byte */
	Vexw	= 1<<7,		/* byte 2, 3-byte */
	Vexl	= 1<<2,		/* 256-bit vector */

	Vexnr	= 0xF<<3,	/* no reg */
	Vexp0	= 0,		/* no SIMD prefix */
	Vexp66	= 1,		/* prefix 66 */
	Vexpf3	= 2,		/* prefix f3 */
	Vexpf2	= 3,		/* prefix f2 */

	Vex0f	= 1,		/* 0F opcode byte */
	Vex0f38	= 2,		/* 0F 38 opcode bytes */
	Vex0f3a	= 3,		/* 0F 3A opcode bytes */

	Roffset	= 22,		/* no. bits for offset in relocation address */
	Rindex	= 10,		/* no. bits for index in relocation address */
};

EXTERN union
{
	struct
	{
		char	obuf[MAXIO];			/* output buffer */
		uchar	ibuf[MAXIO];			/* input buffer */
	} u;
	char	dbuf[1];
} buf;

#define	cbuf	u.obuf
#define	xbuf	u.ibuf

#pragma	varargck	type	"A"	int
#pragma	varargck	type	"A"	uint
#pragma	varargck	type	"D"	Adr*
#pragma	varargck	type	"P"	Prog*
#pragma	varargck	type	"R"	int
#pragma	varargck	type	"R"	uint
#pragma	varargck	type	"S"	char*

#pragma	varargck	argpos	diag 1

EXTERN	long	HEADR;
EXTERN	long	HEADTYPE;
EXTERN	long	INITDAT;
EXTERN	long	INITRND;
EXTERN	long	INITTEXT;
EXTERN	long	INITTEXTP;
EXTERN	char*	INITENTRY;		/* entry point */
EXTERN	Biobuf	bso;
EXTERN	long	bsssize;
EXTERN	long	casepc;
EXTERN	int	cbc;
EXTERN	char*	cbp;
EXTERN	char*	pcstr;
EXTERN	int	cout;
EXTERN	Auto*	curauto;
EXTERN	Auto*	curhist;
EXTERN	Prog*	curp;
EXTERN	Prog*	curtext;
EXTERN	Prog*	datap;
EXTERN	Prog*	edatap;
EXTERN	long	datsize;
EXTERN	char	debug[128];
EXTERN	char	literal[32];
EXTERN	Prog*	etextp;
EXTERN	Prog*	firstp;
EXTERN	char	fnuxi8[8];
EXTERN	char	fnuxi4[4];
EXTERN	Sym*	hash[NHASH];
EXTERN	Sym*	histfrog[MAXHIST];
EXTERN	int	histfrogp;
EXTERN	int	histgen;
EXTERN	char*	library[50];
EXTERN	char*	libraryobj[50];
EXTERN	int	libraryp;
EXTERN	int	xrefresolv;
EXTERN	char*	hunk;
EXTERN	char	inuxi1[1];
EXTERN	char	inuxi2[2];
EXTERN	char	inuxi4[4];
EXTERN	char	ycover[Ymax*Ymax];
EXTERN	uchar*	andptr;
EXTERN	uchar	and[30];
EXTERN	char	reg[D_XREG];
EXTERN	Prog*	lastp;
EXTERN	long	lcsize;
EXTERN	int	nerrors;
EXTERN	long	nhunk;
EXTERN	long	nsymbol;
EXTERN	char*	noname;
EXTERN	char*	outfile;
EXTERN	long	pc;
EXTERN	long	spsize;
EXTERN	Sym*	symlist;
EXTERN	long	symsize;
EXTERN	Prog*	textp;
EXTERN	long	textsize;
EXTERN	long	thunk;
EXTERN	int	version;
EXTERN	int	vexed;
EXTERN	int	vexbytes;
EXTERN	Prog	zprg;
EXTERN	int	dtype;

EXTERN	Adr*	reloca;
EXTERN	int	doexp, dlm;
EXTERN	int	imports, nimports;
EXTERN	int	exports, nexports, allexport;
EXTERN	char*	EXPTAB;
EXTERN	Prog	undefp;

#define	UP	(&undefp)

extern	Optab	optab[];
extern	Optab*	opindex[];
extern	char*	anames[];

int	Aconv(Fmt*);
int	Dconv(Fmt*);
int	Pconv(Fmt*);
int	Rconv(Fmt*);
int	Sconv(Fmt*);
void	addhist(long, int);
Prog*	appendp(Prog*);
void	asmb(void);
void	asmdyn(void);
void	asmins(Prog*);
void	asmlc(void);
void	asmsp(void);
void	asmsym(void);
long	atolwhex(char*);
Prog*	brchain(Prog*);
Prog*	brloop(Prog*);
void	cflush(void);
void	ckoff(Sym*, long);
Prog*	copyp(Prog*);
double	cputime(void);
void	datblk(long, long);
void	diag(char*, ...);
void	dodata(void);
void	doinit(void);
void	doprof1(void);
void	doprof2(void);
void	dostkoff(void);
void	dynreloc(Sym*, ulong, int);
long	entryvalue(void);
void	errorexit(void);
void	export(void);
int	find1(long, int);
int	find2(long, int);
void	follow(void);
void	gethunk(void);
void	histtoauto(void);
double	ieeedtod(Ieee*);
long	ieeedtof(Ieee*);
void	import(void);
int	isxyreg(int);
void	ldobj(int, long, char*);
void	loadlib(void);
void	listinit(void);
Sym*	lookup(char*, int);
void	lput(long);
void	lputl(long);
void	llput(vlong v);
void	llputl(vlong v);
void	main(int, char*[]);
void	mkfwd(void);
void*	mysbrk(ulong);
void	nuxiinit(void);
void	objfile(char*);
int	opsize(Prog*);
void	patch(void);
Prog*	prg(void);
void	readundefs(char*, int);
int	relinv(int);
long	reuse(Prog*, Sym*);
long	rnd(long, long);
void	span(void);
void	strnput(char*, int);
void	undef(void);
void	undefsym(Sym*);
long	vaddr(Adr*);
void	wput(long);
void	wputl(long);
void	xdefine(char*, int, long);
void	xfol(Prog*);
int	zaddr(uchar*, Adr*, Sym*[]);
void	zerosig(char*);

#pragma	varargck	type	"D"	Adr*
#pragma	varargck	type	"P"	Prog*
#pragma	varargck	type	"R"	int
#pragma	varargck	type	"A"	int
