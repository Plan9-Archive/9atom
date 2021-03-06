/*
 * fundamental constants and types of the implementation
 * changing any of these changes the layout on disk
 */

#define SUPER_ADDR	2		/* block address of superblock */
#define ROOT_ADDR	3		/* block address of root directory */

#ifdef OLD
/*
 * compatible on disk with the old 32-bit file server.
 * this lets people run this kernel on their old file systems.
 */
#define	NAMELEN		28		/* max size of file name components */
#define	NDBLOCK		6		/* number of direct blocks in Dentry */
#define NIBLOCK		2		/* max depth of indirect blocks */

typedef long Off;	/* file offsets & sizes, in bytes & blocks */

#else			/* OLD */

/* the glorious new, incompatible (on disk) 64-bit world */

/* keeping NAMELEN ≤ 50 bytes permits 3 Dentrys per mag disk sector */
#define	NAMELEN		56		/* max size of file name components */
#define	NDBLOCK		6		/* number of direct blocks in Dentry */
#define NIBLOCK		4		/* max depth of indirect blocks */

/*
 * file offsets & sizes, in bytes & blocks.  typically long or vlong.
 * vlong is used in the code where would be needed if Off were just long.
 */
typedef vlong Off;

#endif			/* OLD */

/* constants that don't affect disk layout */
#define	MAXDAT		8192		/* max allowable data message */
#define	MAXMSG		128		/* max size protocol message sans data */
#define	OFFMSG		60		/* offset of msg in buffer */

/* more fundamental types */
typedef vlong Wideoff; /* type to widen Off to for printing; ≥ as wide as Off */
typedef short	Userid;		/* signed internal representation of user-id */
typedef long	Timet;		/* in seconds since epoch */
typedef vlong	Devsize;	/* in bytes */

/*
 * tunable parameters
 */
enum {
	Maxword = 200,			/* max bytes per command-line word */
};
#define NTLOCK		200		/* number of active file Tlocks */
#define	LRES		3		/* profiling resolution */
#define NATTID		10		/* the last 10 ID's in attaches */

/*
 * derived constants
 */
#define	BUFSIZE		(RBUFSIZE-sizeof(Tag))
#define DIRPERBUF	(BUFSIZE/sizeof(Dentry))
#define INDPERBUF	(BUFSIZE/sizeof(Off))
#define FEPERBUF	((BUFSIZE-sizeof(Super1)-sizeof(Off))/sizeof(Off))
#define	SMALLBUF	(MAXMSG)
#define	LARGEBUF	(MAXMSG+MAXDAT+256)
#define	RAGAP		(300*1024)/BUFSIZE		/* readahead parameter */
#define CEPERBK		((BUFSIZE-BKPERBLK*sizeof(Off))/\
				(sizeof(Centry)*BKPERBLK))
#define	BKPERBLK	10

typedef struct	Alarm	Alarm;
typedef struct	Auth	Auth;
typedef	struct	Conf	Conf;
typedef	struct	Label	Label;
typedef	struct	Lock	Lock;
typedef	struct	Mach	Mach;
typedef	struct	QLock	QLock;
typedef	struct	Ureg	Ureg;
typedef	struct	User	User;
typedef	struct	Fbuf	Fbuf;
typedef	struct	Super1	Super1;
typedef	struct	Superb	Superb;
typedef	struct	Filsys	Filsys;
typedef	struct	Startsb	Startsb;
typedef	struct	Dentry	Dentry;
typedef	struct	Tag	Tag;
typedef	struct	Talarm	Talarm;
typedef	struct	Uid	Uid;
typedef	struct	Device	Device;
typedef	struct	Devtab	Devtab;
typedef	struct	Qid9p1	Qid9p1;
typedef	struct	Iobuf	Iobuf;
typedef	struct	Wpath	Wpath;
typedef	struct	File	File;
typedef	struct	Chan	Chan;
typedef	struct	Cons	Cons;
typedef	struct	Time	Time;
typedef	struct	Tm	Tm;
typedef	struct	Rtc	Rtc;
typedef	struct	Hiob	Hiob;
typedef	struct	RWlock	RWlock;
typedef	struct	Msgbuf	Msgbuf;
typedef	struct	Queue	Queue;
typedef	struct	Command	Command;
typedef	struct	Flag	Flag;
typedef	struct	Bp	Bp;
typedef	struct	Rabuf	Rabuf;
typedef	struct	Rendez	Rendez;
typedef	struct	Filter	Filter;
typedef		ulong	Float;
typedef	struct	Tlock	Tlock;
typedef	struct	Cache	Cache;
typedef	struct	Centry	Centry;
typedef	struct	Bucket	Bucket;

#pragma incomplete Auth
#pragma incomplete Ureg

struct	Lock
{
	ulong*	sbsem;		/* addr of sync bus semaphore */
	ulong	pc;
	ulong	sr;

	Mach	*m;
	User	*p;
	char	isilock;
};

struct	Rendez
{
	Lock;
	User*	p;
};

struct	Filter
{
	ulong	count;		/* count and old count kept separate */
	ulong	oldcount;	/*	so interrput can read them */
	Float	filter[3];		/* filter */
};

struct	QLock
{
	Lock;			/* to use object */
	User*	head;		/* next process waiting for object */
	User*	tail;		/* last process waiting for object */
	int	locked;		/* flag, is locked */
	char	namebuf[10];
	char*	name;		/* for diagnostics */
};

struct	RWlock
{
	int	nread;
	QLock	wr;
	QLock	rd;
};

/*
 * send/recv queue structure
 */
struct	Queue
{
	Lock;			/* to manipulate values */
	int	size;		/* size of queue */
	int	loc;		/* circular pointer */
	int	count;		/* how many in queue */
	User*	rhead;		/* process's waiting for send */
	User*	rtail;
	User*	whead;		/* process's waiting for recv */
	User*	wtail;
	void*	args[1];	/* list of saved pointers, [->size] */
};

struct	Tag
{
	short	pad;		/* make tag end at a long boundary */
	short	tag;
	Off	path;
};

struct	Device
{
	uchar	type;
	uchar	init;
	ushort	dno;
	Device*	link;			/* link for mcat/mlev/mirror */
	Device*	dlink;			/* link all devices */
	void*	private;
	Devsize	size;
	union
	{
		struct			/* wren, ide, mvsata, (l)worm in targ */
		{
			int	ctrl;	/* disks only */
			int	targ;
			int	lun;	/* wren only */
		} wren;
		struct			/* mcat mlev mirror */
		{
			Device*	first;
			Device*	last;
			int	ndev;
		} cat;
		struct			/* cw */
		{
			Device*	c;	/* cache device */
			Device*	w;	/* worm device */
			Device*	ro;	/* dump - readonly */
		} cw;
		struct			/* juke */
		{
			Device*	j;	/* (robotics, worm drives) - wrens */
			Device*	m;	/* (sides) - r or l devices */
		} j;
		struct			/* ro */
		{
			Device*	parent;
		} ro;
		struct			/* fworm */
		{
			Device*	fw;
		} fw;
		struct			/* part */
		{
			Device*	d;
			char*	name;	/* partition tables */
			uvlong	base;	/* percentages */
			uvlong	size;
		} part;
		struct			/* byte-swapped */
		{
			Device*	d;
		} swab;
	};
};

struct	Devtab
{
	char	c;
	char	c1;
	int	(*read)(Device*, Off, void*);
	int	(*write)(Device*, Off, void*);
	Devsize	(*size)(Device*);
	Off	(*superaddr)(Device*);
	Off	(*getraddr)(Device*);
	void	(*ream)(Device*, int);
	void	(*recover)(Device*);
	void	(*init)(Device*);
	int	(*secsize)(Device*);
	int	(*fmt)(Device*);
};

extern Devtab devtab[];

typedef struct Sidestarts {
	Devsize	sstart;			/* blocks before start of side */
	Devsize	s1start;		/* blocks before start of next side */
} Sidestarts;

struct	Rabuf
{
	union
	{
		struct
		{
			Device*	dev;
			Off	addr;
		};
		Rabuf*	link;
	};
};

/* user-visible Qid, from <libc.h> */
typedef
struct Qid
{
	uvlong	path;			/* Off */
	ulong	vers;			/* should be Off */
	uchar	type;
} Qid;

/* bits in Qid.type */
#define QTDIR		0x80		/* type bit for directories */
#define QTAPPEND	0x40		/* type bit for append only files */
#define QTEXCL		0x20		/* type bit for exclusive use files */
#define QTMOUNT		0x10		/* type bit for mounted channel */
#define QTAUTH		0x08		/* type bit for authentication file */
#define QTFILE		0x00		/* plain file */

/* DONT TOUCH, this is the disk structure */
struct	Qid9p1
{
	Off	path;			/* was long */
	ulong	version;		/* should be Off */
};

struct	Hiob
{
	Iobuf*	link;
	QLock;
};

struct	Chan
{
	char	type;			/* major driver type i.e. Dev* */
	int	(*protocol)(Msgbuf*);	/* version */
	int	msize;			/* version */
	char	whochan[50];
	char	whoname[NAMELEN];
	void	(*whoprint)(Chan*);
	ulong	flags;
	int	chan;			/* overall channel number, mostly for printing */
	int	nmsgs;			/* outstanding messages, set under flock -- for flush */
	Timet	whotime;
	Filter	work;
	Filter	rate;
	int	nfile;			/* used by cmd_files */
	char	rname[20];
	char	wname[20];
	RWlock	reflock;
	Chan*	next;			/* link list of chans */
	Queue*	send;
	Queue*	reply;

	uchar	authinfo[64];

	void*	ifc;
	void*	pdata;
};

struct	Filsys
{
	char*	name;			/* name of filsys */
	char*	conf;			/* symbolic configuration */
	Device*	dev;			/* device that filsys is on */
	int	flags;
		#define	FREAM		(1<<0)	/* mkfs */
		#define	FRECOVER	(1<<1)	/* install last dump */
		#define	FEDIT		(1<<2)	/* modified */
};

struct	Startsb
{
	char*	name;
	Off	startsb;
};

struct	Time
{
	Timet	lasttoy;
	Timet	bias;
	Timet	offset;
};

/*
 * array of qids that are locked
 */
struct	Tlock
{
	Device*	dev;
	Timet	time;
	Off	qpath;
	File*	file;
};

struct	Cons
{
	ulong	flags;		/* overall flags for all channels */
	QLock;			/* generic qlock for mutex */
	int	uid;		/* botch -- used to get uid on cons_create */
	int	gid;		/* botch -- used to get gid on cons_create */
	int	nuid;		/* number of uids */
	int	ngid;		/* number of gids */
	Off	offset;		/* used to read files, c.f. fchar */
	int	chano;		/* generator for channel numbers */
	Chan*	chan;		/* console channel */
	Filsys*	curfs;		/* current filesystem */

	int	profile;	/* are we profiling? */
	long*	profbuf;
	ulong	minpc;
	ulong	maxpc;
	ulong	nprofbuf;

	long	nlarge;		/* number of large message buffers */
	long	nsmall;		/* ... small ... */
	long	nwormre;	/* worm read errors */
	long	nwormwe;	/* worm write errors */
	long	nwormhit;	/* worm read cache hits */
	long	nwormmiss;	/* worm read cache non-hits */
	int	noage;		/* dont update cache age, dump and check */
	long	nwrenre;	/* disk read errors */
	long	nwrenwe;	/* disk write errors */
	long	nreseq;		/* cache bucket resequence */

	Filter	work;	/* thruput in messages */
	Filter	rate;	/* thruput in bytes */
	Filter	bhit;	/* getbufs that hit */
	Filter	bread;	/* getbufs that miss and read */
	Filter	brahead;	/* messages to readahead */
	Filter	binit;	/* getbufs that miss and dont read */

	ulong	cwio[10];		/* ticks spent in various cwio phases */
};

struct	File
{
	QLock;
	Qid	qid;
	Wpath*	wpath;
	Chan*	cp;		/* null means a free slot */
	Tlock*	tlock;		/* if file is locked */
	File*	next;		/* in cp->flist */
	Filsys*	fs;
	Off	addr;
	long	slot;		/* ordinal # of Dentry with a directory block */
	Off	lastra;		/* read ahead address */
	ulong	fid;
	Userid	uid;
	Auth	*auth;
	char	open;
		#define	FREAD	1
		#define	FWRITE	2
		#define	FREMOV	4

	Off	doffset;	/* directory reading */
	ulong	dvers;
	long	dslot;
};

struct	Wpath
{
	Wpath*	up;		/* pointer upwards in path */
	Off	addr;		/* directory entry addr */
	long	slot;		/* directory entry slot */
	short	refs;		/* number of files using this structure */
};

struct	Iobuf
{
	QLock;
	Device*	dev;
	Iobuf*	fore;		/* for lru */
	Iobuf*	back;		/* for lru */
	char*	iobuf;		/* only active while locked */
	char*	xiobuf;		/* "real" buffer pointer */
	Off	addr;
	int	flags;
};

struct	Uid
{
	Userid	uid;		/* user id */
	Userid	lead;		/* leader of group */
	Userid	*gtab;		/* group table */
	int	ngrp;		/* number of group entries */
	char	name[NAMELEN];	/* user name */
};

/* DONT TOUCH, this is the disk structure */
struct	Dentry
{
	char	name[NAMELEN];
	Userid	uid;
	Userid	gid;
	ushort	mode;
		#define	DALLOC	0x8000
		#define	DDIR	0x4000
		#define	DAPND	0x2000
		#define	DLOCK	0x1000
		#define	DREAD	0x4
		#define	DWRITE	0x2
		#define	DEXEC	0x1
	Userid	muid;
	Qid9p1	qid;
	Off	size;
	Off	dblock[NDBLOCK];
	Off	iblocks[NIBLOCK];
	long	atime;
	long	mtime;
};

/* DONT TOUCH, this is the disk structure */
struct	Super1
{
	Off	fstart;
	Off	fsize;
	Off	tfree;
	Off	qidgen;		/* generator for unique ids */
	/*
	 * Stuff for WWC device
	 */
	Off	cwraddr;	/* cfs root addr */
	Off	roraddr;	/* dump root addr */
	Off	last;		/* last super block addr */
	Off	next;		/* next super block addr */
#ifdef AUTOSWAB
	vlong	magic;		/* for byte-order detection */
	/* in memory only, not on disk (maybe) */
	int	flags;
#endif
};

/* DONT TOUCH, this is the disk structure */
struct	Fbuf
{
	Off	nfree;
	Off	free[FEPERBUF];
};

/* DONT TOUCH, this is the disk structure */
struct	Superb
{
	Fbuf	fbuf;
	Super1;
};

struct	Label
{
	ulong	pc;
	ulong	sp;
};

struct Talarm
{
	Lock;
	User	*list;
};

struct	Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	ulong	mem;		/* total physical bytes of memory */
	ulong	sparemem;	/* memory left for check/dump and chans */
	ulong	nuid;		/* distinct uids */
	ulong	nserve;		/* server processes */
	ulong	nrahead;		/* read ahead processes */
	ulong	nfile;		/* number of fid -- system wide */
	ulong	nwpath;		/* number of active paths, derived from nfile */
	ulong	gidspace;	/* space for gid names -- derived from nuid */
	ulong	nlgmsg;		/* number of large message buffers */
	ulong	nsmmsg;		/* number of small message buffers */
	Off	recovcw;		/* recover addresses */
	Off	recovro;
	Off	firstsb;
	Off	recovsb;
	ulong	nauth;		/* number of Auth structs */
	uchar	nodump;		/* no periodic dumps */
	uchar	ripoff;
	uchar	dumpreread;	/* read and compare in dump copy */
	uchar	idedma;		/* flag: use DMA on IDE disks? */
	uchar	fastworm;	/* flag: don't cache cw reads in c device */
	ulong	uartonly;		/* botch? work around soekris with no vga. */
};

/*
 * message buffers
 * 2 types, large and small
 */

enum {
	LARGE	=	1<<0,
	FREE	=	1<<1,
	Budpck	=	1<<3,		/* udp checksum */
	Btcpck	=	1<<4,		/* tcp checksum */
	Bpktck	=	1<<5,		/* packet checksum */
	Bipck	=	1<<6,		/* ip checksum */
	BTRACE =	1<<7,
};
struct	Msgbuf
{
	int	count;
	short	flags;
	Chan*	chan;
	Msgbuf*	next;
	ulong	param;
	int	category;
	uchar*	data;		/* rp or wp: current processing point */
	uchar*	xdata;		/* base of allocation */
	void	(*free)(Msgbuf *);
};

/*
 * message buffer categories
 */
enum
{
	Mxxx		= 0,
	Mbreply1,
	Mbreply2,
	Mbreply3,
	Mbreply4,
	Mbarp1,
	Mbarp2,
	Mbip1,
	Mbip2,
	Mbip3,
	Mbil1,
	Mbil2,
	Mbil3,
	Mbil4,
	Mbilauth,
	Mbeth1,
	Mbeth2,
	Mbeth3,
	Mbeth82563,
	Mbeth10gbesm,
	Mbeth10gbebg,
	Mbrcvbuf,
	Mbsntp,
	Mbcec,
	Mbaoe,
	Mbaoesrb,
	MAXCAT,
};

struct	Mach
{
	int	machno;		/* physical id of processor */
	int	mmask;		/* 1<<m->machno */
	Timet	ticks;		/* of the clock since boot time */
	int	lights;		/* light lights, this processor */
	Filter	idle;

	User*	proc;		/* current process on this processor */
	Label	sched;		/* scheduler wakeup */
//	Lock	alarmlock;	/* access to alarm list */
//	void*	alarm;		/* alarms bound to this clock */

	void	(*intr)(Ureg*, ulong);	/* pending interrupt */
	User*	intrp;		/* process that was interrupted */
	ulong	cause;		/* arg to intr */
	Ureg*	ureg;		/* arg to intr */
	int	loopconst;

//	Lock	apictimerlock;
	int	cpumhz;
	uvlong	cpuhz;
	ulong	cpuidax;
	ulong	cpuiddx;
	char	cpuidid[16];
	int	havetsc;
//	int	havepge;
//	uvlong	tscticks;
//	uchar	stack[1];
};

#define	MAXSTACK 16000
#define	NHAS	200
struct	User
{
	Label	sched;
	Mach*	mach;		/* machine running this proc */
	User*	rnext;		/* next process in run queue */
	User*	qnext;		/* next process on queue for a QLock */
	void	(*start)(void);	/* startup function */
	char*	text;		/* name of this process */
	void*	arg;
	Filter	time;	/* cpu time used */
	int	exiting;
	int	pid;
	int	state;
	Rendez	tsleep;

	Timet	twhen;
	Rendez	*trend;
	User	*tlink;
	int	(*tfn)(void*);

	long	nlock;
	long	delaysched;
	Lock	*lstack[NHAS];
	ulong	pstack[NHAS];

	struct
	{
		ulong	pc[NHAS];	/* list of pcs for locks this process has */
		QLock*	q[NHAS];	/* list of locks this process has */
		QLock*	want;		/* lock waiting */
	} has;
	uchar	stack[MAXSTACK];
};

#define	PRINTSIZE	256
struct
{
	Lock;
	int	machs;
	int	exiting;
} active;

struct	Command
{
	char*	arg0;
	char*	help;
	void	(*func)(int, char*[]);
};

struct	Flag
{
	char*	arg0;
	char*	help;
	ulong	flag;
};

struct	Tm
{
	/* see ctime(3) */
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	mon;
	int	year;
	int	wday;
	int	yday;
	char	zone[4];
	int	tzoff;
};

struct	Rtc
{
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	mon;
	int	year;
};

typedef struct
{
	/* constants during a given truncation */
	Dentry	*d;
	Iobuf	*p;			/* the block containing *d */
	int	uid;
	Off	newsize;
	Off	lastblk;		/* last data block of file to keep */

	/* variables */
	Off	relblk;			/* # of current data blk within file */
	int	pastlast;		/* have we walked past lastblk? */
	int	err;
} Truncstate;

/*
 * cw device
 */

/* DONT TOUCH, this is the disk structure */
struct	Cache
{
	Off	maddr;		/* cache map addr */
	Off	msize;		/* cache map size in buckets */
	Off	caddr;		/* cache addr */
	Off	csize;		/* cache size */
	Off	fsize;		/* current size of worm */
	Off	wsize;		/* max size of the worm */
	Off	wmax;		/* highwater write */

	Off	sbaddr;		/* super block addr */
	Off	cwraddr;	/* cw root addr */
	Off	roraddr;	/* dump root addr */

	Timet	toytime;	/* somewhere convienent */
	Timet	time;
};

/* DONT TOUCH, this is the disk structure */
struct	Centry
{
	ushort	age;
	short	state;
	Off	waddr;		/* worm addr */
};

/* DONT TOUCH, this is the disk structure */
struct	Bucket
{
	long	agegen;		/* generator for ages in this bkt */
	Centry	entry[CEPERBK];
};

/*
 * scsi i/o
 */
enum
{
	SCSIread = 0,
	SCSIwrite = 1,
};

/*
 * Process states
 */
enum
{
	Dead = 0,
	Moribund,
	Zombie,
	Ready,
	Scheding,
	Running,
	Queueing,
	Sending,
	Recving,
	MMUing,
	Exiting,
	Inwait,
	Wakeme,
	Broken,
};

/*
 * Lights
 */
enum
{
	Lreal	= 0,	/* blink in clock interrupt */
	Lintr,		/* on while in interrupt */
	Lpanic,		/* in panic */
	Lcwmap,		/* in cw lookup */
};

/*
 * devnone block numbers
 */
enum
{
	Cwio1 	= 1,
	Cwio2,
	Cwxx1,
	Cwxx2,
	Cwxx3,
	Cwxx4,
	Cwdump1,
	Cwdump2,
	Cuidbuf,
	Cckbuf,
};

/*
 * error codes generated from the file server
 */
enum
{
	Ebadspc = 1,
	Efid,
	Echar,
	Eopen,
	Ecount,
	Ealloc,
	Eqid,
	Eaccess,
	Eentry,
	Emode,
	Edir1,
	Edir2,
	Ephase,
	Eexist,
	Edot,
	Eempty,
	Ebadu,
	Enoattach,
	Ewstatb,
	Ewstatd,
	Ewstatg,
	Ewstatl,
	Ewstatm,
	Ewstato,
	Ewstatp,
	Ewstatq,
	Ewstatu,
	Ewstatv,
	Ename,
	Ewalk,
	Eronly,
	Efull,
	Eoffset,
	Elocked,
	Ebroken,
	Eauth,
	Eauth2,
	Efidinuse,
	Etoolong,
	Econvert,
	Eversion,
	Eauthdisabled,
	Eauthnone,
	Eauthfile,
	Eedge,
	MAXERR
};

/*
 * device types
 */
enum
{
	Devnone 	= 0,
	Devcon,			/* console */
	Devwren,		/* scsi disk drive */
	Devworm,		/* scsi video drive */
	Devlworm,		/* scsi video drive (labeled) */
	Devfworm,		/* fake read-only device */
	Devjuke,		/* jukebox */
	Devcw,			/* cache with worm */
	Devro,			/* readonly worm */
	Devmcat,		/* multiple cat devices */
	Devmlev,		/* multiple interleave devices */
	Devil,			/* internet link */
	Devpart,		/* partition */
	Devfloppy,		/* floppy drive */
	Devide,			/* IDE drive */
	Devswab,		/* swab data between mem and device */
	Devmirr,		/* mirror devices */
	Devmv,		/* Marvell sata disk drive */
	Devaoe,
	Devia,
	MAXDEV
};

/*
 * tags on block
 */
/* DONT TOUCH, this is in disk structures */
/* also, the order from Tdir to Tind4 (Tmaxind) is exploited in indirck() */
enum
{
	Tnone		= 0,
	Tsuper,			/* the super block */
#ifdef OLD
	Tdir,			/* directory contents */
	Tind1,			/* points to blocks */
	Tind2,			/* points to Tind1 */
#else
	Tdirold,
	Tind1old,
	Tind2old,
#endif
	Tfile,			/* file contents */
	Tfree,			/* in free list */
	Tbuck,			/* cache fs bucket */
	Tvirgo,			/* fake worm virgin bits */
	Tcache,			/* cw cache things */
	Tconfig,		/* configuration block */
#ifndef OLD
	/* Tdir & indirect blocks are last to allow for greater depth */
	Tdir,			/* directory contents */
	Tind1,			/* points to blocks */
	Tind2,			/* points to Tind1 */
	Tind3,			/* points to Tind2 */
	Tind4,			/* points to Tind3 */
	Maxtind,
#endif
	MAXTAG,

#ifdef OLD
	Tmaxind = Tind2,
#else
	Tmaxind = Maxtind - 1,
#endif
};

/*
 * flags to getbuf
 */
enum
{
	Bread	= 1<<0,	/* read the block if miss */
	Bprobe	= 1<<1,	/* return null if miss */
	Bmod	= 1<<2,	/* buffer is dirty, needs writing */
	Bimm	= 1<<3,	/* write immediately on putbuf */
	Bres	= 1<<4,	/* reserved, never renamed */
};

extern	/*register*/	Mach*	m;
extern	/*register*/	User*	u;
extern	Talarm		talarm;

Conf	conf;
Cons	cons;
#define	MACHP(n)	((Mach*)(MACHADDR+n*BY2PG))
#define	Ticks		MACHP(0)->ticks

#pragma	varargck	type	"Z"	Device*
#pragma	varargck	type	"T"	Timet
#pragma	varargck	type	"I"	uchar*
#pragma	varargck	type	"E"	uchar*
#pragma	varargck	type	"W"	Filter*
#pragma	varargck	type	"w"	Filter*
#pragma	varargck	type	"G"	int
#pragma	varargck	type	"φ"	uint

extern	Rendez dawnrend;
extern	ulong	nhiob;
extern	Hiob	*hiob;
