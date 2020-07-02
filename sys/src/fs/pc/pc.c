#include	"all.h"
#include	"mem.h"
#include	"io.h"
#include	"ureg.h"

/*
 * Where configuration info is left for the loaded programme.
 * This will turn into a structure as more is done by the boot loader
 * (e.g. why parse the .ini file twice?).
 * There are 1024 bytes available at CONFADDR.
 */
#define BOOTLINE	((char*)CONFADDR)
#define BOOTLINELEN	64
#define BOOTARGS	((char*)(CONFADDR+BOOTLINELEN))
#define	BOOTARGSLEN	(1024-BOOTLINELEN)
#define	MAXCONF		32

char bootdisk[NAMELEN];
char *confname[MAXCONF];
char *confval[MAXCONF];
int nconf;

int
getcfields(char* lp, char** fields, int n, char* sep)
{
	int i;

	for(i = 0; lp && *lp && i < n; i++){
		while(*lp && strchr(sep, *lp) != 0)
			*lp++ = 0;
		if(*lp == 0)
			break;
		fields[i] = lp;
		while(*lp && strchr(sep, *lp) == 0){
			if(*lp == '\\' && *(lp+1) == '\n')
				*lp++ = ' ';
			lp++;
		}
	}

	return i;
}

static void
options(void)
{
	long i, n;
	char *cp, *line[MAXCONF], *p, *q;

	/*
	 *  parse configuration args from dos file plan9.ini
	 */
	cp = BOOTARGS;	/* where b.com leaves its config */
	cp[BOOTARGSLEN-1] = 0;

	/*
	 * Strip out '\r', change '\t' -> ' '.
	 */
	p = cp;
	for(q = cp; *q; q++){
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = getcfields(cp, line, MAXCONF, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == 0)
			continue;
		*cp++ = 0;
		if(cp - line[i] >= NAMELEN+1)
			*(line[i]+NAMELEN-1) = 0;
		confname[nconf] = line[i];
		confval[nconf] = cp;
		nconf++;
	}
}


/*
 * Vecinit is the first hook we have into configuring the machine,
 * so we do it all here. A pox on special fileserver code.
 * We do more in meminit below.
 */
void
vecinit(void)
{
	options();
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];
	return 0;
}

/*
 * old memory scan.  if no e820 information is available,
 * this kernel will see MAXMEG megabytes of RAM at most.
 * maxmeg is limited due the the fact the page table might be too large
 * for our temporary mapping to hold and depends on the size of 
 * our kernel.
 */
#ifndef MAXMEG
#define MAXMEG 2015
#endif

char mmap[MAXMEG+2];
Mconf mconf;

static void
mconfscan(void)
{
	ulong x, i, j, ktop;
	Mbank *b;

	/*
	 *  size memory above 4 meg. Kernel sits at 1 meg.  We
	 *  only recognize MB size chunks.
	 */
	x = 0x12345678;
	for(i = 4; i <= MAXMEG; i++){
		/*
		 *  write the first & last word in a megabyte of memory
		 */
		*mapaddr(i*MB) = x;
		*mapaddr((i+1)*MB-BY2WD) = x;

		/*
		 *  write the first and last word in all previous megs to
		 *  handle address wrap around
		 */
		for(j = 4; j < i; j++){
			*mapaddr(j*MB) = ~x;
			*mapaddr((j+1)*MB-BY2WD) = ~x;
		}

		/*
		 *  check for correct value
		 */
		if(*mapaddr(i*MB) == x && *mapaddr((i+1)*MB-BY2WD) == x)
			mmap[i] = 'x';
		x += 0x3141526;
	}

	b = mconf.bank;
	ktop = PGROUND((ulong)end);
	ktop = PADDR(ktop);
	b->base = ktop;
	/* careful with that ax, eugene */
	for(i = 4; mmap[i] == 'x'; i++)
		;
	b->limit = i*MB;
	mconf.topofmem = b->limit;
	b++;

	/*
	 * Look for any other chunks of memory.
	 */
	for(; i <= MAXMEG; i++){
		if(mmap[i] == 'x'){
			b->base = i*MB;
			for(j = i+1; mmap[j] == 'x'; j++)
				;
			b->limit = j*MB;
			mconf.topofmem = j*MB;
			b++;

			if(b - mconf.bank == MAXBANK)
				break;
		}
	}

	mconf.nbank = b - mconf.bank;
}

typedef struct {
	uvlong base;
	uvlong len;
	ulong type;
}Emap;

static char *etypes[] =
{
	"type=0",
	"memory",
	"reserved",
	"acpi reclaim",
	"acpi nvs",
};

#define	smap		0x534d4150
#define	e820tab		0x2d0
#define	e820sz		20
#define	e820end		(16*e820sz+e820tab)

/* debugging crap */
ulong n820;
ulong n820m;
static Emap emap[16];

int
mconf820(void)
{
	ulong i;
	Emap *e, *t;
	Mbank *b;
	uchar *a;
	vlong sz;

	a = (uchar*)mapaddr(0);
	i = *(ulong*)(a+e820end);
	if(i == 0 || i > 16)
		return -1;
	e = (Emap*)(a+e820tab);
	t = e+i;
	b = mconf.bank;
	mconf.topofmem = MB;		// this is used to calculate pgtable sz; allow pci space.
	for(; e<=t; e++){
		emap[n820].base = e->base;
		emap[n820].len = e->len;
		emap[n820++].type = e->type;

		if(e->type != 1)
			continue;
		if(e->base >= 1ULL<<32 || e->base == 0)
			continue;
		sz = e->len;
		b->base = e->base;
		b->limit = e->base+sz;
		mconf.topofmem += sz;
		if(++b-mconf.bank == MAXBANK)
			break;
	}
	mconf.topofmem &= ~(4*MB-1);

	n820m = b-mconf.bank;
	if(b-mconf.bank < 1)
		return 0;
	mconf.nbank = b-mconf.bank;
	/* careful with that axe, eugene */
	mconf.bank[0].base += PADDR(PGROUND((ulong)end));

	return mconf.nbank;
}

/* debugging aide -- please remove */
void
cmd_e820(int, char **)
{
	ulong n;
	Emap *e, *end;
	vlong sz, ex, lim;

	print("found %uld e820 entries %uld banks\n", n820, n820m);

	e = emap;
	end = e+n820;
	
	n = 0;
	sz = 0;
	ex = 0;
	for(; e<end; e++){
		print("e820: %.8llux %.8llux ", e->base, e->base+e->len);
		if(e->type < nelem(etypes))
			print("%s\n", etypes[e->type]);
		else
			print("type=%lud\n", e->type);

		if(e->type != 1 || e->base == 0)
			continue;
		if(e->base >= 1ULL<<32){
			ex += e->len;
			continue;
		}
		lim = e->base+e->len;
		sz += e->len;
		if(++n == MAXBANK)
			continue;

		print("\t" "bank %ullx %ullx\n", e->base, lim);
	}

	print("found %ld e820 memory banks %ulldMB+%ulldMB\n", n, sz/MB, ex/MB);
	print("	topofmem = %p\n", mconf.topofmem);
	print("	cpuiddx %p; cycles %p\n", MACHP(0)->cpuiddx, cycles);
	if((MACHP(0)->cpuiddx & 0x08) && (getcr4() & 0x10))
		print("	pse: yes\n");
}

ulong
meminit(void)
{
	ulong i, sz;

	conf.nmach = 1;
	if(mconf820() <= 0)
		mconfscan();
	mmuinit();
	trapinit();

	sz = 0;
	for(i = 0; i < mconf.nbank; i++)
		sz += mconf.bank[i].limit-mconf.bank[i].base;
	return sz;
}

void
userinit(void (*f)(void), void *arg, char *text)
{
	User *p;

	p = newproc();

	/*
	 * Kernel Stack.
	 * The -4 is because the path sched()->gotolabel()->init0()->f()
	 * uses a stack location without creating any local space.
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->stack + sizeof(p->stack) - 4;
	p->start = f;
	p->text = text;
	p->arg = arg;

	dofilter(&p->time);
	ready(p);
}

static int useuart;
static void (*intrputs)(char*, int);

static int
pcgetc(void)
{
	int c;

	if(c = kbdgetc())
		return c;
	if(c = cecgetc())
		return c;
	if(useuart)
		return uartgetc();
	return 0;
}

static void
pcputc(int c)
{
	if(predawn)
		cgaputc(c);
	if(useuart)
		uartputc(c);
}

static void
pcputs(char* s, int n)
{
	if(!predawn){
		cgaputs(s, n);
		cecputs(s, n);
	}
	if(intrputs)
		(*intrputs)(s, n);
}

void
consinit(void (*puts)(char*, int))
{
	char *p;
	int baud, port;

	kbdinit();

	consgetc = pcgetc;
	consputc = pcputc;
	consputs = pcputs;
	intrputs = puts;

	if((p = getconf("console")) == 0 || cistrcmp(p, "cga") == 0)
		return;

	port = strtoul(p, &p, 0);
	if(port < 0 || port > 1)
		return;
	while(*p == ' ' || *p == '\t')
		p++;
	if(*p != 'b' || (baud = strtoul(p+1, 0, 0)) == 0)
		baud = 9600;

	uartspecial(port, kbdchar, conschar, baud);
	useuart = 1;
}

void
consreset(void)
{
}

void
firmware(void)
{
	char *p;

	/*
	 * Always called splhi().
	 */
	if((p = getconf("reset")) && cistrcmp(p, "manual") == 0){
		predawn = 1;
		print("\nHit Reset\n");
		for(;;);
	}
	pcireset();
	i8042reset();
}

int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	char cc[NAMELEN], *p, *q, *r;
	int n;

	sprint(cc, "%s%d", class, ctlrno);
	for(n = 0; n < nconf; n++){
		if(cistrncmp(confname[n], cc, NAMELEN))
			continue;
		isa->nopt = 0;
		p = confval[n];
		while(*p){
			while(*p == ' ' || *p == '\t')
				p++;
			if(*p == '\0')
				break;
			if(cistrncmp(p, "type=", 5) == 0){
				p += 5;
				for(q = isa->type; q < &isa->type[NAMELEN-1]; q++){
					if(*p == '\0' || *p == ' ' || *p == '\t')
						break;
					*q = *p++;
				}
				*q = '\0';
			}
			else if(cistrncmp(p, "port=", 5) == 0)
				isa->port = strtoul(p+5, &p, 0);
			else if(cistrncmp(p, "irq=", 4) == 0)
				isa->irq = strtoul(p+4, &p, 0);
			else if(cistrncmp(p, "dma=", 4) == 0)
				isa->dma = strtoul(p+4, &p, 0);
			else if(cistrncmp(p, "mem=", 4) == 0)
				isa->mem = strtoul(p+4, &p, 0);
			else if(cistrncmp(p, "size=", 5) == 0)
				isa->size = strtoul(p+5, &p, 0);
			else if(cistrncmp(p, "freq=", 5) == 0)
				isa->freq = strtoul(p+5, &p, 0);
			else if(isa->nopt < NISAOPT){
				r = isa->opt[isa->nopt];
				while(*p && *p != ' ' && *p != '\t'){
					*r++ = *p++;
					if(r-isa->opt[isa->nopt] >= ISAOPTLEN-1)
						break;
				}
				*r = '\0';
				isa->nopt++;
			}
			while(*p && *p != ' ' && *p != '\t')
				p++;
		}
		return 1;
	}
	return 0;
}

void
lockinit(void)
{
}

void
launchinit(void)
{
}

void
lights(int, int)
{
}

/* in assembly language
Float
famd(Float a, int b, int c, int d)
{
	return ((a+b) * c) / d;
}

ulong
fdf(Float a, int b)
{
	return a / b;
}
*/
