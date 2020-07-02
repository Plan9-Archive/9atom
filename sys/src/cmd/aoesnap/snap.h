/*
 * lots of this junk is extra -- please remove
 */
typedef struct Data	Data;
typedef struct Page	Page;
typedef struct Proc	Proc;
typedef struct Seg	Seg;

enum {
	Psegment = 0,
	Pfd,
	Pfpregs,
	Pkregs,
	Pnoteid,
	Pns,
	Pproc,
	Pregs,
	Pstatus,
	Npfile,		/* last one loaded by default */
	Pmem,

	Pagesize = 1024,	/* need not relate to kernel */
};

struct Data {
	ulong len;
	char data[1];
};

struct Seg {
	char*	name;
	uvlong	offset;
	uvlong	 len;
	Page**	pg;
	int	npg;
};

struct Page {
	Page*	link;
	ulong	len;
	ulong	sum;
	uchar	pfile;
	uchar	written;
	char	type;

	/* when page is written, these hold the ptr to it */
	ulong	pid;
	uvlong	offset;
	char	data[];
};

struct Proc {
	Proc *link;
	long	pid;
	Data*	d[Npfile];
	Seg**	seg;	/* memory segments */
	int	nseg;
	Seg*	text;	/* text file */
};

Proc*	snapw(Biobuf*, long);
void*	emalloc(ulong);
void*	erealloc(void*, ulong);
char*	estrdup(char*);

int	aoeopen(int);
long	aoepread(int, void*, long, vlong);
void	aoeclose(int);

#define dprint(...)		if(debug) fprint(2, _VA_ARGS_)

extern	int pfile[];
extern	int shelf;
int	debug;
