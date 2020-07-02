#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "adr.h"

/*
 * Where configuration info is left for the loaded programme.
 * This will turn into a structure as more is done by the boot loader
 * (e.g. why parse the .ini file twice?).
 * There are 3584 bytes available at CONFADDR.
 */
#define	CONFADDR	PTR2UINT(KADDR(0x0001200))

#define BOOTLINE	((char*)CONFADDR)
#define BOOTLINELEN	64
#define BOOTARGS	((char*)(CONFADDR+BOOTLINELEN))
#define	BOOTARGSLEN	(4096-0x200-BOOTLINELEN)

enum {
	Maxconf		= 64,
};

typedef struct C C;
struct C {
	char	*name;
	char	*val;
};

static	C	cfg[Maxconf];
static	int	ncfg;

static void
parseoptions(void)
{
	long i, n;
	char *cp, *line[Maxconf];

	/*
	 *  parse configuration args from dos file plan9.ini
	 */
	cp = BOOTARGS;	/* where b.com leaves its config */
	cp[BOOTARGSLEN-1] = 0;

	n = getfields(cp, line, Maxconf, 1, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		cfg[ncfg].name = line[i];
		cfg[ncfg].val = cp;
		ncfg++;
	}
}

static void
cmdline(void)
{
	char *p, *f[32], **argv, buf[200];
	int argc, n, o;

	p = getconf("*cmdline");
	if(p == nil)
		return;
	snprint(buf, sizeof buf, "%s", p);
	argc = tokenize(buf, f, nelem(f));
	argv = f;

	/*
	 * Process flags.
	 * Flags [A-Za-z] may be optionally followed by
	 * an integer level between 1 and 127 inclusive
	 * (no space between flag and level).
	 * '--' ends flag processing.
	 */
	while(--argc > 0 && (*++argv)[0] == '-' && (*argv)[1] != '-'){
		while(o = *++argv[0]){
			if(!(o >= 'A' && o <= 'Z') && !(o >= 'a' && o <= 'z'))
				continue;
			n = strtol(argv[0]+1, &p, 0);
			if(p == argv[0]+1 || n < 1 || n > 127)
				n = 1;
			argv[0] = p-1;
			dbgflg[o] = n;
		}
	}
}

static int typemap[] = {
	Anone,
	Amemory,
	Areserved,
	Aacpireclaim,
	Aacpinvs,
	Aunusable,
	Adisable,
};

static void
e820(void)
{
	char *p, *s;
	uvlong base, len, type;

	p = getconf("*e820");
	if(p == nil)
		return;
	for(s = p;;){
		if(*s == 0)
			break;
		type = strtoull(s, &s, 16);
		if(*s != ' ')
			break;
		base = strtoull(s, &s, 16);
		if(*s != ' ')
			break;
		len = strtoull(s, &s, 16) - base;
		if(*s != ' ' && *s != 0 || len == 0)
			break;
		if(type >= nelem(typemap))
			continue;
		adrmapinit(base, len, typemap[type], Mfree);
	}
}

void
options(void)
{
	parseoptions();
	e820();
	cmdline();
}


char*
getconf(char *name)
{
	int i;

	for(i = 0; i < ncfg; i++)
		if(cistrcmp(cfg[i].name, name) == 0)
			return cfg[i].val;
	return nil;
}

void
confsetenv(void)
{
	int i;

	for(i = 0; i < ncfg; i++){
		if(cfg[i].name[0] != '*')
			ksetenv(cfg[i].name, cfg[i].val, 0);
		ksetenv(cfg[i].name, cfg[i].val, 1);
	}
}

int
pciconfig(char *class, int ctlrno, Pciconf *pci)
{
	char cc[32], *p;
	int i;

	snprint(cc, sizeof cc, "%s%d", class, ctlrno);
	p = getconf(cc);
	if(p == nil)
		return 0;

	pci->type = "";
	snprint(pci->optbuf, sizeof pci->optbuf, "%s", p);
	pci->nopt = tokenize(pci->optbuf, pci->opt, nelem(pci->opt));
	for(i = 0; i < pci->nopt; i++){
		p = pci->opt[i];
		if(cistrncmp(p, "type=", 5) == 0)
			pci->type = p + 5;
		else if(cistrncmp(p, "port=", 5) == 0)
			pci->port = strtoul(p+5, &p, 0);
		else if(cistrncmp(p, "irq=", 4) == 0)
			pci->irq = strtoul(p+4, &p, 0);
		else if(cistrncmp(p, "mem=", 4) == 0)
			pci->mem = strtoul(p+4, &p, 0);
		else if(cistrncmp(p, "tbdf=", 5) == 0)
			pci->tbdf = strtotbdf(p+5, &p, 0);
	}
	return 1;
}
