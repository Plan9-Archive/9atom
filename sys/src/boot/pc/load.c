#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "fs.h"

static char *diskparts[] = { "dos", "9fat", "fs", "data", "cdboot", 0 };
static char *etherparts[] = { "*", 0 };

static char *diskinis[] = {
	"plan9/plan9.ini",
	"plan9.ini",
	0
};
static char *etherinis[] = {
	"/cfg/pxe/%E",
	0
};

Type types[] = {
	{	Tfloppy, "#f",
		Fini|Ffs,
		floppyinit, floppyinitdev,
		floppygetfspart, 0, floppyboot,
		floppyprintdevs,
		diskparts,
		diskinis,
	},
	{	Tether, "#l" ,
		Fini|Fbootp,
		etherinit, etherinitdev,
		pxegetfspart, 0, bootpboot,
		etherprintdevs,
		etherparts,
		etherinis,
	},
	{	Tcd, "#S", 
		Fini|Ffs,
		cdinit, sdinitdev,
		sdgetfspart, sdaddconf, sdboot,
		sdprintdevs,
		diskparts,
		diskinis,
	},
	{	Tsd, "#S", 
		Fini|Ffs,
		sdinit, sdinitdev,
		sdgetfspart, sdaddconf, sdboot,
		sdprintdevs,
		diskparts,
		diskinis,
	},
	{	Tnil, 0,
		0,
		nil, nil, nil, nil, nil, nil,
		nil,
		nil,
		0,
		nil,
	},
};

#include "sd.h"

extern SDifc sdataifc;
extern SDifc sdiahciifc;
extern SDifc sdmylexifc;
extern SDifc sd53c8xxifc;
extern SDifc sdaoeifc;

#ifdef NOSCSI

SDifc* sdifc[] = {
	&sdaoeifc,
	&sdiahciifc,
	&sdataifc,
	nil,
};

#else

SDifc* sdifc[] = {
	&sdiahciifc,
	&sdataifc,
//	&sdmylexifc,
//	&sd53c8xxifc,
	&sdaoeifc,
	nil,
};

#endif NOSCSI

typedef struct Mode Mode;

enum {
	Maxdev		= 7,
	Dany		= -1,
	Nmedia		= 16,
	Nini		= 10,
};

enum {					/* mode */
	Mauto		= 0x00,
	Mlocal		= 0x01,
	Manual		= 0x02,
	NMode		= 0x03,
};

typedef struct Medium Medium;
struct Medium {
	Type*	type;
	int	flag;
	int	dev;
	char name[NAMELEN];

	Fs *inifs;
	char *part;
	char *ini;

	Medium*	next;
};

typedef struct Mode {
	char*	name;
	int	mode;
} Mode;

static Medium media[Nmedia];
static Medium *curmedium = media;

static Mode modes[NMode+1] = {
	[Mauto]		{ "auto",   Mauto,  },
	[Mlocal]	{ "local",  Mlocal, },
	[Manual]	{ "manual", Manual, },
};

char **ini;

int scsi0port;
char *defaultpartition;
int iniread;
int vga;

static Medium*
parse(char *line, char **file)
{
	char *p;
	Type *tp;
	Medium *mp;

	if(p = strchr(line, '!')) {
		*p++ = 0;
		*file = p;
	} else
		*file = "";

	for(tp = types; tp->type != Tnil; tp++)
		for(mp = tp->media; mp; mp = mp->next)
			if(strcmp(mp->name, line) == 0)
				return mp;
	if(p)
		*--p = '!';
	return nil;
}

static int
boot(Medium *mp, char *file)
{
	Type *tp;
	Medium *xmp;
	static int didaddconf;
	Boot b;

	memset(&b, 0, sizeof b);
	b.state = INITKERNEL;

	if(didaddconf == 0) {
		didaddconf = 1;
		for(tp = types; tp->type != Tnil; tp++)
			if(tp->addconf)
				for(xmp = tp->media; xmp; xmp = xmp->next)
					(*tp->addconf)(xmp->dev);
	}

	buildconf();
	sprint(BOOTLINE, "%s!%s", mp->name, file);
	return (*mp->type->boot)(mp->dev, file, &b);
}

static Medium*
allocm(Type *tp)
{
	Medium **l;

	if(curmedium >= &media[Nmedia])
		return 0;

	for(l = &tp->media; *l; l = &(*l)->next)
		;
	*l = curmedium++;
	return *l;
}

Medium*
probe(int type, int flag, int dev)
{
	Type *tp;
	int i;
	Medium *mp;
	File f;
	Fs *fs;
	char **partp;

	for(tp = types; tp->type != Tnil; tp++){
		if(type == Tnoether){
			if(tp->type == Tether)
				continue;
		}else if(type != Tany && type != tp->type)
			continue;

		if(flag != Fnone)
			for(mp = tp->media; mp; mp = mp->next)
				if((flag & mp->flag) && (dev == Dany || dev == mp->dev))
					return mp;

		if((tp->flag & Fprobe) == 0){
			tp->flag |= Fprobe;
			tp->mask = (*tp->init)();
		}

		for(i = 0; tp->mask; i++){
			if((tp->mask & (1<<i)) == 0)
				continue;
			tp->mask &= ~(1<<i);

			if((mp = allocm(tp)) == 0)
				continue;

			mp->dev = i;
			mp->flag = tp->flag;
			mp->type = tp;
			(*tp->initdev)(i, mp->name);		/* sets mp->name */

			if(mp->flag & Fini){
				mp->flag &= ~Fini;
				for(partp = tp->parts; *partp; partp++){
					if((fs = (*tp->getfspart)(i, *partp, 0)) == nil)
						continue;
					for(ini = tp->inis; *ini; ini++){
						if(fswalk(fs, *ini, &f) > 0){
							mp->inifs = fs;
							mp->part = *partp;
							mp->ini = f.path;
							mp->flag |= Fini;
							goto Break2;
						}
					}
				}
			}
		Break2:
			if((flag & mp->flag) && (dev == Dany || dev == i))
				return mp;
		}
	}

	return 0;
}

void
main(void)
{
	Medium *mp;
	int flag, i, mode, tried;
	char def[2*NAMELEN], line[80], *p, *file;
	Type *tp;

	i8042a20();
	memset(m, 0, sizeof(Mach));
//consinit("0 b19200");
	trapinit();
	clockinit();
	alarminit();
	meminit(0);
	spllo();

	/*
	 * the soekris machines have no video but each has a serial port.
	 * they must see serial output, if any, before cga output because
	 * otherwise the soekris bios will translate cga output to serial
	 * output, which will garble serial console output.
	 */
	pcimatch(nil, 0, 0);		/* force scan of pci table */
	if (!vga) {
		consinit("0 b19200");	/* e.g., for soekris debugging */
		print("no vga; serial console only\n");
	}
	kbdinit();
	if((ulong)&end > (KZERO|(640*1024)))
		panic("i'm too big");

	prcpuid();
	readlsconf();
	for(tp = types; tp->type != Tnil; tp++)
		if((mp = probe(tp->type, Fini, Dany)) && (mp->flag & Fini)){
			print("using %s!%s!%s\n", mp->name, mp->part, mp->ini);
			iniread = !dotini(mp->inifs, mp->name, mp->type->devstr);
			break;
		}

	consinit(getconf("console"));
	apminit();
	patchvesa();
	devpccardlink();
	devi82365link();

	/*
 	 * Even after we find the ini file, we keep probing disks,
	 * because we have to collect the partition tables and
	 * have boot devices for parse.
	 */
	if(pxe)
		probe(Tnoether, Fnone, Dany);		/* don't switch horses midstream. */
	else
		probe(Tany, Fnone, Dany);
	tried = 0;
	mode = Mauto;
	
	if(p = getconf("bootfile")) {
		snprint(def, sizeof def, "%s", p);
		p = def;
		mode = Manual;
		for(i = 0; i < NMode; i++){
			if(strcmp(p, modes[i].name) == 0){
				mode = modes[i].mode;
				goto done;
			}
		}
		if((mp = parse(p, &file)) == nil) {
			print("Unknown boot device: %s\n", p);
			goto done;
		}
		tried = boot(mp, file);
	}
done:
	if(tried == 0 && mode != Manual){
		flag = Fany;
		if(mode == Mlocal)
			flag &= ~Fbootp;
		if((mp = probe(Tany, flag, Dany)) && mp->type->type != Tfloppy)
			boot(mp, "");
	}

	def[0] = 0;
	probe(Tany, Fnone, Dany);
	if(p = getconf("bootdef"))
		strcpy(def, p);

	/* print possible boot methods */
	flag = 0;
	for(tp = types; tp->type != Tnil; tp++){
		for(mp = tp->media; mp; mp = mp->next){
			if(flag == 0){
				flag = 1;
				print("Boot devices:");
			}
			(*tp->printdevs)(mp->dev);
		}
	}
	if(flag)
		print("\n");

	for(;;){
		if(getstr("boot from", line, sizeof(line), def, (mode != Manual)*15) >= 0){
		//	changeonf("bootfile", line);
			if(mp = parse(line, &file))
				boot(mp, file);
		}
		def[0] = 0;
	}
}

int
getfields(char *lp, char **fields, int n, char sep)
{
	int i;

	for(i = 0; lp && *lp && i < n; i++){
		while(*lp == sep)
			*lp++ = 0;
		if(*lp == 0)
			break;
		fields[i] = lp;
		while(*lp && *lp != sep){
			if(*lp == '\\' && *(lp+1) == '\n')
				*lp++ = ' ';
			lp++;
		}
	}
	return i;
}

#define PSTART		(8*1024*1024)
#define PEND		(16*1024*1024)

ulong palloc = PSTART;

void*
ialloc(ulong n, int align)
{
	ulong p;
	int a;

	p = palloc;
	if(align <= 0)
		align = 4;
	if(a = n % align)
		n += align - a;
	if(a = p % align)
		p += align - a;


	palloc = p+n;
	if(palloc > PEND)
		panic("ialloc(%lud, %d) called from %#p",
			n, align, getcallerpc(&n));
	return memset((void*)(p|KZERO), 0, n);
}

void*
xspanalloc(ulong size, int align, ulong span)
{
	ulong a, v;

	if((palloc + (size+align+span)) > PEND)
		panic("xspanalloc(%lud, %d, 0x%lux) called from %#p",
			size, align, span, getcallerpc(&size));

	a = (ulong)ialloc(size+align+span, 0);

	if(span > 2)
		v = (a + span) & ~(span-1);
	else
		v = a;

	if(align > 1)
		v = (v + align) & ~(align-1);

	return (void*)v;
}

static Block *allocbp;
static int	nb;
static int ballocd;
static int reused;

Block*
allocb(int size)
{
	Block *bp, **lbp;
	ulong addr;

	lbp = &allocbp;
	for(bp = *lbp; bp; bp = bp->next){
		if((bp->lim - bp->base) >= size){
			*lbp = bp->next;
			reused++;
			break;
		}
		lbp = &bp->next;
	}
	if(bp == 0){
		if((palloc + (sizeof(Block)+size+64)) > PEND)
			panic("allocb(%d) called from %#p %d %d %d",
				size, getcallerpc(&size), nb, ballocd, reused);
		bp = ialloc(sizeof(Block)+size+64, 0);
		addr = (ulong)bp;
		addr = ROUNDUP(addr + sizeof(Block), 8);
		bp->base = (uchar*)addr;
		bp->lim = ((uchar*)bp) + sizeof(Block)+size+64;
	}
	nb++;
	ballocd++;
	if(bp->flag)
		panic("allocb reuse");

	bp->rp = bp->base;
	bp->wp = bp->rp;
	bp->next = 0;
	bp->flag = 1;

	return bp;
}

void
freeb(Block* bp)
{
	bp->next = allocbp;
	allocbp = bp;
	nb--;
	bp->flag = 0;
}

enum {
	Paddr=		0x70,	/* address port */
	Pdata=		0x71,	/* data port */
};

uchar
nvramread(int offset)
{
	outb(Paddr, offset);
	return inb(Pdata);
}

void (*etherdetach)(void);
void (*floppydetach)(void);
void (*sddetach)(void);

void
impulse(void)
{
	if(etherdetach)
		etherdetach();
	if(floppydetach)
		floppydetach();
	if(sddetach)
		sddetach();
	consdrain();

	splhi();
	trapdisable();
}
