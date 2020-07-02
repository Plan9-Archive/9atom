#include <u.h>
#include <libc.h>
#include <bio.h>

#include "scsireq.h"

enum {					/* fundamental constants/defaults */
	/*
	 * default & maximum `maximum i/o size'; overridden by -m.
	 * limits kernel memory consumption.
	 * 240K is exabyte maximum block size.
	 */
	MaxIOsize	= 240*1024,
};

#define MIN(a, b)	((a) < (b) ? (a): (b))

static char rwbuf[MaxIOsize];
static int verbose = 1;

Biobuf bin, bout;
long maxiosize = MaxIOsize;
int exabyte = 0;
int force6bytecmds = 0;

typedef struct {
	char *name;
	long (*f)(ScsiReq *, int, char *[]);
	int open;
	char *help;
} ScsiCmd;

static ScsiCmd scsicmd[];

static vlong
vlmin(vlong a, vlong b)
{
	if (a < b)
		return a;
	else
		return b;
}

static long
cmdready(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SRready(rp);
}

static long
cmdrewind(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SRrewind(rp);
}

static long
cmdreqsense(ScsiReq *rp, int argc, char *argv[])
{
	long nbytes;

	USED(argc, argv);
	if((nbytes = SRreqsense(rp)) != -1)
		makesense(rp);
	return nbytes;
}

static long
cmdformat(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SRformat(rp);
}

static long
cmdrblimits(ScsiReq *rp, int argc, char *argv[])
{
	uchar l[6];
	long n;

	USED(argc, argv);
	if((n = SRrblimits(rp, l)) == -1)
		return -1;
	Bprint(&bout, " %2.2uX %2.2uX %2.2uX %2.2uX %2.2uX %2.2uX\n",
		l[0], l[1], l[2], l[3], l[4], l[5]);
	return n;
}

static int
mkfile(char *file, int omode, int *pid)
{
	int fd[2];

	if(*file != '|'){
		*pid = -1;
		if(omode == OWRITE)
			return create(file, OWRITE, 0666);
		else if(omode == OREAD)
			return open(file, OREAD);
		return -1;
	}

	file++;
	if(*file == 0 || pipe(fd) == -1)
		return -1;
	if((*pid = fork()) == -1){
		close(fd[0]);
		close(fd[1]);
		return -1;
	}
	if(*pid == 0){
		switch(omode){

		case OREAD:
			dup(fd[0], 1);
			break;

		case OWRITE:
			dup(fd[0], 0);
			break;
		}
		close(fd[0]);
		close(fd[1]);
		execl("/bin/rc", "rc", "-c", file, nil);
		exits("exec");
	}
	close(fd[0]);
	return fd[1];
}

int
waitfor(int pid)
{
	int msg;
	Waitmsg *w;

	while((w = wait()) != nil){
		if(w->pid != pid){
			free(w);
			continue;
		}
		msg = (w->msg[0] != '\0');
		free(w);
		return msg;
	}
	return -1;
}

static long
cmdread(ScsiReq *rp, int argc, char *argv[])
{
	long n, iosize, prevsize = 0;
	vlong nbytes, total;
	int fd, pid;
	char *p;

	iosize = maxiosize;
	nbytes = ~0ULL >> 1;
	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 2:
		nbytes = strtoll(argv[1], &p, 0);
		if(nbytes == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((fd = mkfile(argv[0], OWRITE, &pid)) == -1){
			rp->status = Status_BADARG;
			return -1;
		}
		break;
	}
	print("bsize=%lud\n", rp->lbsize);
	total = 0;
	while(nbytes){
		n = vlmin(nbytes, iosize);
		if((n = SRread(rp, rwbuf, n)) == -1){
			if(total == 0)
				total = -1;
			break;
		}
		if (n == 0)
			break;
		if (prevsize != n) {
			print("tape block size=%ld\n", n);
			prevsize = n;
		}
		if(write(fd, rwbuf, n) != n){
			if(total == 0)
				total = -1;
			if(rp->status == STok)
				rp->status = Status_SW;
			break;
		}
		nbytes -= n;
		total += n;
	}
	close(fd);
	if(pid >= 0 && waitfor(pid)){
		rp->status = Status_SW;
		return -1;
	}
	return total;
}

static long
cmdwrite(ScsiReq *rp, int argc, char *argv[])
{
	long n, prevsize = 0;
	vlong nbytes, total;
	int fd, pid;
	char *p;

	nbytes = ~0ULL >> 1;
	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 2:
		nbytes = strtoll(argv[1], &p, 0);
		if(nbytes == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((fd = mkfile(argv[0], OREAD, &pid)) == -1){
			rp->status = Status_BADARG;
			return -1;
		}
		break;
	}
	total = 0;
	while(nbytes){
		n = vlmin(nbytes, maxiosize);
		if((n = read(fd, rwbuf, n)) == -1){
			if(total == 0)
				total = -1;
			break;
		}
		if (n == 0)
			break;
		if (prevsize != n) {
			print("tape block size=%ld\n", n);
			prevsize = n;
		}
		if(SRwrite(rp, rwbuf, n) != n){
			if(total == 0)
				total = -1;
			if(rp->status == STok)
				rp->status = Status_SW;
			break;
		}
		nbytes -= n;
		total += n;
	}
	close(fd);
	if(pid >= 0 && waitfor(pid)){
		rp->status = Status_SW;
		return -1;
	}
	return total;
}

static long
cmdseek(ScsiReq *rp, int argc, char *argv[])
{
	char *p;
	long offset;
	int type;

	type = 0;
	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 2:
		if((type = strtol(argv[1], &p, 0)) == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((offset = strtol(argv[0], &p, 0)) == 0 && p == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		break;
	}
	return SRseek(rp, offset, type);
}

static long
cmdfilemark(ScsiReq *rp, int argc, char *argv[])
{
	char *p;
	ulong howmany;

	howmany = 1;
	if(argc && (howmany = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
		rp->status = Status_BADARG;
		return -1;
	}
	return SRfilemark(rp, howmany);
}

static long
cmdspace(ScsiReq *rp, int argc, char *argv[])
{
	uchar code;
	long howmany;
	char option, *p;

	code = 0x00;
	howmany = 1;
	while(argc && (*argv)[0] == '-'){
		while(option = *++argv[0]){
			switch(option){

			case '-':
				break;

			case 'b':
				code = 0x00;
				break;

			case 'f':
				code = 0x01;
				break;

			default:
				rp->status = Status_BADARG;
				return -1;
			}
			break;
		}
		argc--; argv++;
		if(option == '-')
			break;
	}
	if(argc && ((howmany = strtol(argv[0], &p, 0)) == 0 && p == argv[0])){
		rp->status = Status_BADARG;
		return -1;
	}
	return SRspace(rp, code, howmany);
}

static long
cmdinquiry(ScsiReq *rp, int argc, char *argv[])
{
	long status;
	int i, n;
	uchar *p;

	USED(argc, argv);
	if((status = SRinquiry(rp)) != -1){
		n = rp->inquiry[4]+4;
		for(i = 0; i < MIN(8, n); i++)
			Bprint(&bout, " %2.2uX", rp->inquiry[i]);
		p = &rp->inquiry[8];
		n = MIN(n, sizeof(rp->inquiry)-8);
		while(n && (*p == ' ' || *p == '\t' || *p == '\n')){
			n--;
			p++;
		}
		Bprint(&bout, "\t%.*s\n", n, (char*)p);
	}
	return status;
}

static long
cmdmodeselect6(ScsiReq *rp, int argc, char *argv[])
{
	uchar list[MaxDirData];
	long nbytes, ul;
	char *p;

	memset(list, 0, sizeof list);
	for(nbytes = 0; argc; argc--, argv++, nbytes++){
		if((ul = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		list[nbytes] = ul;

	}
	if(!(rp->flags & Finqok) && SRinquiry(rp) == -1)
		Bprint(&bout, "warning: couldn't determine whether SCSI-1/SCSI-2 mode");
	return SRmodeselect6(rp, list, nbytes);
}

long
sel(ScsiReq *rp, uchar *list, long nbytes, int save)
{
	uchar cmd[10];

	memset(cmd, 0, sizeof cmd);
	cmd[0] = 0x55;
	cmd[1] = 0x10 | save!=0;		/* standard (scsi-2) format */
	cmd[7] = nbytes>>8;
	cmd[8] = nbytes;
	rp->cmd.p = cmd;
	rp->cmd.count = sizeof cmd;
	rp->data.p = list;
	rp->data.count = nbytes;
	rp->data.write = 1;
	return SRrequest(rp);
}

static long
cmdmodeselect10(ScsiReq *rp, int argc, char *argv[])
{
	uchar list[MaxDirData];
	int nbytes, save;
	char *p;

	save = 0;
	if(argc >= 1 && strcmp(*argv, "-s") == 0){
		save = 1;
		argv++;
		argc--;
	}

	if(argc >= nelem(list)){
		rp->status = Status_BADARG;
		return -1;
	}
	memset(list, 0, sizeof list);
	for(nbytes = 0; argc; argc--, argv++, nbytes++)
		if((list[nbytes] = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
	if(!(rp->flags & Finqok) && SRinquiry(rp) == -1)
		Bprint(&bout, "warning: couldn't determine whether SCSI-1/SCSI-2 mode");
print("%d %d\n", nbytes, (rp->flags & Finqok) && (rp->inquiry[2] & 0x07) >= 2);
//	return SRmodeselect10(rp, list, nbytes);
	return sel(rp, list, nbytes, save);
}

static char*
pflag(char *s, char *e, uint f, char **tab, uint ntab)
{
	char *s0;
	uchar i;

	s0 = s;
	for(i = 0; i < ntab; i++)
		if(f & (1 << i))
			s = seprint(s, e, "%s ", tab[i]);
	if(s > s0 && s[-1] == ' ')
		s--;
	*s = 0;
	return s;
}

static char*
xlatenum(uint f, char **tab, uint ntab)
{
	static char buf[64];

	if(f < ntab && tab[f])
		return tab[f];
	snprint(buf, sizeof buf, "unknown val %#ux", f);
	return buf;
}

static char *recftab[] = {
[0]	"dcr",
[1]	"dte",
[2]	"per",
[3]	"eer",
[4]	"rc",
[5]	"tb",
[6]	"arre",
[7]	"awre",
};
void
recoverpage(uchar *u)
{

	char buf[128];
	uint t;

	pflag(buf, buf + sizeof buf, u[2], recftab, nelem(recftab));
	t = u[10]<<8 | u[11];
	Bprint(&bout, "\tflags %s; retry %d/%d; timeout %udms\n", buf, u[3], u[8], t);

}

static char *cachetab[] = {
[0]	"rcd",
[1]	"mf",
[2]	"wce",
[3]	"size",
[4]	"disc",
[5]	"cap",
[6]	"abpf",
[7]	"ic",
};

static char *cachetab2[] = {
[0]	"nv_dis",
[5]	"dra",
[6]	"lbcss",
[7]	"fsw",
};

void
cachepage(uchar *u)
{

	char buf[128];

	pflag(buf, buf + sizeof buf, u[2], cachetab, nelem(cachetab));
	Bprint(&bout, "\tcache flags2: %s\n", buf);
	pflag(buf, buf + sizeof buf, u[12], cachetab2, nelem(cachetab2));
	Bprint(&bout, "\tcache flags12: %s\n", buf);
}

static char *verftab[] = {
[0]	"dcr",
[1]	"dte",
[2]	"per",
[3]	"eer",
};

void
verpage(uchar *u)
{

	char buf[128];
	uint t;

	pflag(buf, buf + sizeof buf, u[2], verftab, nelem(verftab));
	t = u[10]<<8 | u[11];
	Bprint(&bout, "\tverify flags %s; retry %d; timeout %udms\n", buf, u[3], t);
}

char *ieftab[] = {
[7]	"perf",
[5]	"ebf",
[4]	"ewasc",
[3]	"dexcpt",
[2]	"test",
[0]	"logerr",
};

char *iem[] = {
[0]	"no reporting",
[1]	"asynchronous (obs)",
[2]	"generate unit attn",
[3]	"cond gen error",
[4]	"uncond gen error",
[5]	"generate no sense",
[6]	"only on request",
};

void
iepage(uchar *u)
{
	char buf[128], *s;
	ulong t;

	pflag(buf, buf + sizeof buf, u[2], ieftab, nelem(ieftab));
	s = xlatenum(u[3], iem, nelem(iem));
	t = GETBELONG(u + 4);
	Bprint(&bout, "\tiem mode %s; flags (%s); it %lud.%ulds\n", s, buf, t/10, t%10);
}

static char *powertab[] = {
[0]	"standby",
[1]	"idle",
};

void
powerpage(uchar *u)
{
	ulong t0, t1;

	t0 = GETBELONG(u + 4);
	t1 = GETBELONG(u + 8);
	Bprint(&bout, "\tpower");
	if(u[3] & 1)
		Bprint(&bout, " it %lud.%ulds", t0/10, t0%10);
	if(u[3] & 2)
		Bprint(&bout, " st %lud.%ulds", t1/10, t1%10);
	if((u[3] & 3) == 0)
		Bprint(&bout, " n/a\n");
	else
		Bprint(&bout, "\n");
}

static char *prototab[] = {
[0]	"fc",
[1]	"parallel",
[2]	"ssa",
[3]	"ieee1394",
[4]	"rdma",
[5]	"iscsi",
[6]	"sas",
[7]	"adt",
[8]	"ata",
};

void
protopage(uchar *u)
{
	char *s;
	uint p;

	s = "unknown";
	p = u[2] & 0xf;
	if(p < nelem(prototab) && prototab[p])
		s = prototab[p];
	Bprint(&bout, "\tprotocol %s\n", s);
}

struct {
	uint	pc;
	void	(*f)(uchar*);
} pctab[] = {
	0x01,	recoverpage,
	0x07,	verpage,
	0x08,	cachepage,
	0x1c,	iepage,
	0x1a,	powerpage,
	0x19,	protopage,
};

static uchar*
prblock(uchar *lp, long *nbytesp, int type, int blen, int blockl, int llba)
{
	uchar *e, *lp0;
	int nbytes;
	uint i, j, n, page, bs;
	uvlong blocks;

	nbytes = *nbytesp;
	e = lp + blen;
	for(lp0 = lp; lp < e; lp += blockl){
		Bprint(&bout, " Block %ld\n   ", lp - lp0);
		for(i = 0; i < blockl; i++)
			Bprint(&bout, " %2.2uX", lp[i]);
		if(type == 0){
			if(llba == 1){
				blocks = GETBEVL(lp);
				bs = GETBELONG(lp + 12);
			}else{
				blocks = GETBELONG(lp);
				bs = GETBE24(lp + 5);
			}
			Bprint(&bout, "    (blocks %llud", blocks);
			Bprint(&bout, " length %ud)", bs);
		}else{
			Bprint(&bout, "    (density %2.2uX", lp[0]);
			Bprint(&bout, " blocks %lud", GETBE24(lp));
			Bprint(&bout, " length %lud)", GETBE24(lp + 5));
		}
		nbytes -= blockl;
		Bputc(&bout, '\n');
	}
	while(nbytes >= 2 && (i = lp[1])){				/* pages */
		nbytes -= i+2;
		page = lp[0] & 0x3f;
		Bprint(&bout, " Page %2.2uX %ud %s\n   ", page, i, lp[0]&0x80? "ps": "");
		for(n = 0; n < i; n++){
			if(n && ((n & 0x0F) == 0))
				Bprint(&bout, "\n   ");
			Bprint(&bout, " %2.2uX", lp[2 + n]);
		}
		if(n && (n & 0x0F))
			Bputc(&bout, '\n');
		for(j = 0; j < nelem(pctab); j++)
			if(pctab[j].pc == page)
				pctab[j].f(lp);
		lp += 2 + i;
	}
	*nbytesp = nbytes;
	return lp;
}

static long
cmdmodesense6(ScsiReq *rp, int argc, char *argv[])
{
	uchar list[MaxDirData], *lp, page;
	long i, nbytes, status;
	char *p;

	nbytes = sizeof list;
	switch(argc){
	default:
		rp->status = Status_BADARG;
		return -1;

	case 2:
		if((nbytes = strtoul(argv[1], &p, 0)) == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((page = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		break;

	case 0:
		page = Allmodepages;
		break;
	}
	if((status = SRmodesense6(rp, page, list, nbytes)) == -1)
		return -1;
	lp = list;
	nbytes = list[0];
	Bprint(&bout, " Header\n   ");
	for(i = 0; i < 4; i++){				/* header */
		Bprint(&bout, " %2.2uX", *lp);
		lp++;
	}
	Bputc(&bout, '\n');
	lp = prblock(lp, &nbytes, list[2], list[3], 8, 0);
	USED(lp);
	return status;
}

static long
cmdmodesense10(ScsiReq *rp, int argc, char *argv[])
{
	char *p;
	uchar *list, *lp, page;
	long i, n, type, nbytes, llba, blockl, status;

	nbytes = MaxDirData;
	switch(argc){
	default:
		rp->status = Status_BADARG;
		return -1;

	case 2:
		if((nbytes = strtoul(argv[1], &p, 0)) == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/
	case 1:
		if((page = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		break;

	case 0:
		page = Allmodepages;
		break;
	}
	list = malloc(nbytes);
	if(list == 0){
		rp->status = STnomem;
		return -1;
	}
	if((status = SRmodesense10(rp, page, list, nbytes)) == -1)
		return -1;
	lp = list;
	n = list[0]<<8 | list[1];
	if(n < nbytes)
		nbytes = n;
	type = lp[2];
	llba = lp[4] & 1;
	blockl = lp[7];
	Bprint(&bout, " Header\n   ");
	for(i = 0; i < 8; i++){				/* header */
		Bprint(&bout, " %2.2uX", *lp);
		lp++;
	}
	Bputc(&bout, '\n');
	lp = prblock(lp, &nbytes, type, list[6]<<8|list[7], blockl, llba);
	USED(lp);
	free(list);
	return status;
}

static long
cmdmediaserial(ScsiReq *r, int, char**)
{
	uchar c[512 + 1];
	int status;
	uint l;

	if((status = SRmediaserial(r, c, sizeof c - 1)) == -1)
		return -1;
	l = GETBELONG(c);
	while(l > 0 && c[--l] == ' ')
		;
	c[l] = 0;
	Bprint(&bout, "serial [%s]\n", (char*)c);
	return status;
}

static char *lftab[] = {
[0]	"lp",
[1]	"lbin",
[2]	"tmc0",
[3]	"tmc1",
[4]	"etc",
[5]	"tsd",
[6]	"ds",
[7]	"du",
};

char*
trim(char *buf, uchar *u, int l)
{
	char *p;

	memmove(buf, u, l);
	p = buf + l;
	do
		*p = 0;
	while(p > buf && *--p == ' ');
	while(p > buf && *p >= ' ' && *p < 0x7f)
		p--;
	if(p != buf)
		return 0;
	return buf;
}

static int
logpage(uchar *u)
{
	char buf[128], *s;
	uchar pc, *p;
	uint n, l, parm, i;
	uvlong b;

	pc = u[0] & 0x3f;
	n = GETBE16(u + 2);
	Bprint(&bout, "pc %.4ux\n", pc);
	for(p = u + 4; p < u + n; p += l + 4){
		parm = GETBE16(p);
		l = p[3];
		if(l == 0)
			break;
		pflag(buf, buf + sizeof buf, p[2], lftab, nelem(lftab));
		Bprint(&bout, "\tparm %.4ux %s\n\t    ", parm, buf);
		for(i = 0; i < l; i++)
			Bprint(&bout, "%.2ux ", p[i + 4]);
		Bprint(&bout, "\n");
		if(p[2] & 2){
			b = -1;
			switch(l){
			case 2:
				b = GETBE16(p + 4);
				break;
			case 4:
				b = GETBELONG(p + 4);
				break;
			case 8:
				b = GETBEVL(p + 4);
				break;
			}
			if(b != -1)
				Bprint(&bout, "\t    ->%llud\n", b);
		}else if(s = trim(buf, p + 4, l))
			Bprint(&bout, "\t    ->'%s'\n", s);
	}
	return p - u;
}

static long
cmdlogsense(ScsiReq *rp, int argc, char *argv[])
{
	char *p;
	uchar *buf;
	uint page;
	long status;

	switch(argc){
	default:
		rp->status = Status_BADARG;
		return -1;
	case 0:
		page = 0;
		break;
	case 1:
		if((page = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		break;
	}
	buf = malloc(64*1024);
	if(buf == 0){
		rp->status = STnomem;
		return -1;
	}
	memset(buf, 0, sizeof buf);
	if((status = SRlogsense(rp, 0, page, 0, buf, 1024)) == -1)
		goto out;
	for(int i = 0; i < 16; i++)
		Bprint(&bout, "%.2ux ", buf[i]);
	Bprint(&bout, "\n");
	logpage(buf);
out:
	free(buf);
	return status;
}

static char *protoid[] = {
	"fc",
	"parallel",
	"ssa",
	"ieee1394",
	"rdma",
	"iscsi",
	"sas",
};

static char *associd[] = {
	"dev",
	"port",
	"lun",
};

static char *idtypeid[] = {
	"none",
	"vendorid",
	"eui-64",
	"name_id",
	"relport4",
	"relport5",
	"relport6",
	"md5",
};

static char*
tabtr(int i, char **tab, int ntab, char *spare)
{
	if(i < ntab)
		return tab[i];
	snprint(spare, 12, "%d", i);
	return spare;
}

static int
vpd83(uchar *u, uchar *e)
{
	char *proto, *assoc, *idtype, buf0[12], buf1[12], buf2[12];
	int l;

	fmtinstall('H', encodefmt);
	if((u[1] & 0x80) != 0)
		proto = tabtr(u[0]>>4, protoid, nelem(protoid), buf0);
	else{
		proto = buf0;
		snprint(buf0, sizeof buf0, "invalid/%d", u[0]>>4);
	}
	assoc = tabtr(u[1]>>4 & 3, associd, nelem(associd), buf1);
	idtype = tabtr(u[1] & 0xf, idtypeid, nelem(idtypeid), buf2);
	Bprint(&bout, "  proto %s type %s assoc %s len %d\n", proto, idtype, assoc, l = u[3]);
	if(u + l > e)
		l = e - u;
	switch(u[0] & 0xf){
	case 2:
		Bprint(&bout, "    %.*s", l, (char*)u + 4);
		break;
	default:
		Bprint(&bout, "    %.*lH", l, u + 4);
		break;
	}

	return u[3];
}

static long
cmdvpd(ScsiReq *r, int argc, char **argv)
{
	char *p;
	uchar *u, *e, c[0x100];
	int page, status, i, l;

	page = 0x80;
	if(argc == 1)
		page = atoi(argv[0]);
	if((status = SRvpd(r, page, c, sizeof c - 1)) == -1)
		return -1;
	switch(page){
	case 0x00:
		Bprint(&bout, "supported pages:\n  ");
		l = c[3];
		if(l > sizeof c - 1)
			l = sizeof c - 1;
		u = c + 4;
		for(i = 0; i < l; i++)
			Bprint(&bout, "%.2ux ", u[i]);
		if(l > 0)
			Bprint(&bout, "\n");
		break;
	case 0x80:
		l = c[3];
		if(l > sizeof c - 1)
			l = sizeof c - 1;
		p = (char*)(c + 4);
		while(l > 0 && p[--l] == ' ')
			;
		p[l] = 0;
		Bprint(&bout, "unit serial: %s\n", (char*)p);
		break;
	case 0x83:
		l = c[3];
		if(l > sizeof c - 1)
			l = sizeof c - 1;
		u = c + 4;
		e = u + l;
		for(; u < e; ){
			u += vpd83(u, e);
			Bprint(&bout, "\n");
		}
		break;
	default:
		Bprint(&bout, "pages %.2ux len %d:\n  ", page, c[3]);
		l = c[3];
		if(l > sizeof c - 1)
			l = sizeof c - 1;
		u = c + 4;
		for(i = 0; i < l; i++)
			Bprint(&bout, "%.2ux ", u[i]);
		if(l > 0)
			Bprint(&bout, "\n");
	}
	return status;	
}

static long
start(ScsiReq *rp, int argc, char *argv[], uchar code)
{
	char *p;

	if(argc && (code = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
		rp->status = Status_BADARG;
		return -1;
	}
	return SRstart(rp, code);
}

static long
cmdstart(ScsiReq *rp, int argc, char *argv[])
{
	return start(rp, argc, argv, 1);
}

static long
cmdstop(ScsiReq *rp, int argc, char *argv[])
{
	return start(rp, argc, argv, 0);
}

static long
cmdeject(ScsiReq *rp, int argc, char *argv[])
{
	return start(rp, argc, argv, 2);
}

static long
cmdingest(ScsiReq *rp, int argc, char *argv[])
{
	return start(rp, argc, argv, 3);
}

static long
capacity(ScsiReq *rp, int nbyte)
{
	uchar d[32];
	long n, r;
	uvlong i;

	if(nbyte == 16){
		if((r = SRrcapacity16(rp, d)) == -1)
			return -1;
		i = GETBEVL(d);
		n = GETBELONG(d + 8);
	}else{
		if((r = SRrcapacity(rp, d)) == -1)
			return -1;
		i = GETBELONG(d);
		n = GETBELONG(d + 4);
	}
	Bprint(&bout, " %llud %lud\n", i, n);
	return r;
}

static long
cmdcapacity(ScsiReq *rp, int, char **)
{
	return capacity(rp, 10);
}

static long
cmdcapacity16(ScsiReq *rp, int, char **)
{
	return capacity(rp, 16);
}

static char* mamattr[] = {
[0]	"values",
[1]	"list",
[2]	"volume",
[3]	"partition",
};

void
prattr(void)
{
print("prattr\n");
}

static long
cmdreadattr(ScsiReq *rp, int argc, char **argv)
{
	uchar *list;
	int i, attr, nlist, status;

	attr = -1;
	switch(argc){
	case 0:
		attr = 0;
		break;
	case 1:
		for(i = 0; i < nelem(mamattr); i++)
			if(strcmp(mamattr[i], argv[0]) == 0)
				attr = i;
		break;
	}
	if(attr == -1){
		rp->status = Status_BADARG;
		return -1;
	}
	nlist = 1024;
	list = malloc(nlist);
	if(list == 0){
		rp->status = STnomem;
		return -1;
	}
	memset(list, 0, nlist);
	if((status = SRreadattr(rp, attr, list, nlist)) != -1)
		prattr();
	free(list);
	return status;
}

static long
cmdblank(ScsiReq *rp, int argc, char *argv[])
{
	uchar type, track;
	char *sp;

	type = track = 0;
	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 2:
		if((type = strtoul(argv[1], &sp, 0)) == 0 && sp == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		if(type > 6){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((track = strtoul(argv[0], &sp, 0)) == 0 && sp == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 0:
		break;
	}
	return SRblank(rp, type, track);
}

static long
cmdsynccache(ScsiReq *rp, int, char **)
{
	return SRsynccache(rp);
}

static long
cmdrtoc(ScsiReq *rp, int argc, char *argv[])
{
	uchar d[100*8+4], format, track, *p;
	char *sp;
	long n, nbytes;
	int tdl;

	format = track = 0;
	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 2:
		if((format = strtoul(argv[1], &sp, 0)) == 0 && sp == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		if(format > 4){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((track = strtoul(argv[0], &sp, 0)) == 0 && sp == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 0:
		break;
	}
	if((nbytes = SRTOC(rp, d, sizeof d, format, track)) == -1){
		if(rp->status == STok)
			Bprint(&bout, "\t(probably empty)\n");
		return -1;
	}
	tdl = (d[0]<<8)|d[1];
	switch(format){

	case 0:
		Bprint(&bout, "\ttoc/pma data length: 0x%uX\n", tdl);
		Bprint(&bout, "\tfirst track number: %d\n", d[2]);
		Bprint(&bout, "\tlast track number: %d\n", d[3]);
		for(p = &d[4], n = tdl-2; n; n -= 8, p += 8){
			Bprint(&bout, "\ttrack number: 0x%2.2uX\n", p[2]);
			Bprint(&bout, "\t\tcontrol: 0x%2.2uX\n", p[1] & 0x0F);
			Bprint(&bout, "\t\tblock address: 0x%uX\n",
				(p[4]<<24)|(p[5]<<16)|(p[6]<<8)|p[7]);
		}
		break;

	case 1:
		Bprint(&bout, "\tsessions data length: 0x%uX\n", tdl);
		Bprint(&bout, "\tnumber of finished sessions: %d\n", d[2]);
		Bprint(&bout, "\tunfinished session number: %d\n", d[3]);
		for(p = &d[4], n = tdl-2; n; n -= 8, p += 8){
			Bprint(&bout, "\tsession number: 0x%2.2uX\n", p[0]);
			Bprint(&bout, "\t\tfirst track number in session: 0x%2.2uX\n",
				p[2]);
			Bprint(&bout, "\t\tlogical start address: 0x%uX\n",
				(p[5]<<16)|(p[6]<<8)|p[7]);
		}
		break;

	case 2:
		Bprint(&bout, "\tfull TOC data length: 0x%uX\n", tdl);
		Bprint(&bout, "\tnumber of finished sessions: %d\n", d[2]);
		Bprint(&bout, "\tunfinished session number: %d\n", d[3]);
		for(p = &d[4], n = tdl-2; n > 0; n -= 11, p += 11){
			Bprint(&bout, "\tsession number: 0x%2.2uX\n", p[0]);
			Bprint(&bout, "\t\tcontrol: 0x%2.2uX\n", p[1] & 0x0F);
			Bprint(&bout, "\t\tADR: 0x%2.2uX\n", (p[1]>>4) & 0x0F);
			Bprint(&bout, "\t\tTNO: 0x%2.2uX\n", p[2]);
			Bprint(&bout, "\t\tPOINT: 0x%2.2uX\n", p[3]);
			Bprint(&bout, "\t\tMin: 0x%2.2uX\n", p[4]);
			Bprint(&bout, "\t\tSec: 0x%2.2uX\n", p[5]);
			Bprint(&bout, "\t\tFrame: 0x%2.2uX\n", p[6]);
			Bprint(&bout, "\t\tZero: 0x%2.2uX\n", p[7]);
			Bprint(&bout, "\t\tPMIN: 0x%2.2uX\n", p[8]);
			Bprint(&bout, "\t\tPSEC: 0x%2.2uX\n", p[9]);
			Bprint(&bout, "\t\tPFRAME: 0x%2.2uX\n", p[10]);
		}
		break;
	case 3:
		Bprint(&bout, "\tPMA data length: 0x%uX\n", tdl);
		for(p = &d[4], n = tdl-2; n > 0; n -= 11, p += 11){
			Bprint(&bout, "\t\tcontrol: 0x%2.2uX\n", p[1] & 0x0F);
			Bprint(&bout, "\t\tADR: 0x%2.2uX\n", (p[1]>>4) & 0x0F);
			Bprint(&bout, "\t\tTNO: 0x%2.2uX\n", p[2]);
			Bprint(&bout, "\t\tPOINT: 0x%2.2uX\n", p[3]);
			Bprint(&bout, "\t\tMin: 0x%2.2uX\n", p[4]);
			Bprint(&bout, "\t\tSec: 0x%2.2uX\n", p[5]);
			Bprint(&bout, "\t\tFrame: 0x%2.2uX\n", p[6]);
			Bprint(&bout, "\t\tZero: 0x%2.2uX\n", p[7]);
			Bprint(&bout, "\t\tPMIN: 0x%2.2uX\n", p[8]);
			Bprint(&bout, "\t\tPSEC: 0x%2.2uX\n", p[9]);
			Bprint(&bout, "\t\tPFRAME: 0x%2.2uX\n", p[10]);
		}
		break;

	case 4:
		Bprint(&bout, "\tATIP data length: 0x%uX\n", tdl);
		break;

	}
	for(n = 0; n < nbytes; n++){
		if(n && ((n & 0x0F) == 0))
			Bprint(&bout, "\n");
		Bprint(&bout, " %2.2uX", d[n]);
	}
	if(n && (n & 0x0F))
		Bputc(&bout, '\n');
	return nbytes;
}

static long
cmdrdiscinfo(ScsiReq *rp, int argc, char*[])
{
	uchar d[MaxDirData];
	int dl;
	long n, nbytes;

	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 0:
		break;
	}
	if((nbytes = SRrdiscinfo(rp, d, sizeof d)) == -1)
		return -1;

	dl = (d[0]<<8)|d[1];
	Bprint(&bout, "\tdata length: 0x%uX\n", dl);
	Bprint(&bout, "\tinfo[2] 0x%2.2uX\n", d[2]);
	switch(d[2] & 0x03){

	case 0:
		Bprint(&bout, "\t\tEmpty\n");
		break;

	case 1:
		Bprint(&bout, "\t\tIncomplete disc (Appendable)\n");
		break;

	case 2:
		Bprint(&bout, "\t\tComplete (CD-ROM or last session is closed and has no next session pointer)\n");
		break;

	case 3:
		Bprint(&bout, "\t\tReserved\n");
		break;
	}
	switch((d[2]>>2) & 0x03){

	case 0:
		Bprint(&bout, "\t\tEmpty Session\n");
		break;

	case 1:
		Bprint(&bout, "\t\tIncomplete Session\n");
		break;

	case 2:
		Bprint(&bout, "\t\tReserved\n");
		break;

	case 3:
		Bprint(&bout, "\t\tComplete Session (only possible when disc Status is Complete)\n");
		break;
	}
	if(d[2] & 0x10)
		Bprint(&bout, "\t\tErasable\n");
	Bprint(&bout, "\tNumber of First Track on Disc %ud\n", d[3]);
	Bprint(&bout, "\tNumber of Sessions %ud\n", d[4]);
	Bprint(&bout, "\tFirst Track Number in Last Session %ud\n", d[5]);
	Bprint(&bout, "\tLast Track Number in Last Session %ud\n", d[6]);
	Bprint(&bout, "\tinfo[7] 0x%2.2uX\n", d[7]);
	if(d[7] & 0x20)
		Bprint(&bout, "\t\tUnrestricted Use Disc\n");
	if(d[7] & 0x40)
		Bprint(&bout, "\t\tDisc Bar Code Valid\n");
	if(d[7] & 0x80)
		Bprint(&bout, "\t\tDisc ID Valid\n");
	Bprint(&bout, "\tinfo[8] 0x%2.2uX\n", d[8]);
	switch(d[8]){

	case 0x00:
		Bprint(&bout, "\t\tCD-DA or CD-ROM Disc\n");
		break;

	case 0x10:
		Bprint(&bout, "\t\tCD-I Disc\n");
		break;

	case 0x20:
		Bprint(&bout, "\t\tCD-ROM XA Disc\n");
		break;

	case 0xFF:
		Bprint(&bout, "\t\tUndefined\n");
		break;

	default:
		Bprint(&bout, "\t\tReserved\n");
		break;
	}
	Bprint(&bout, "\tLast Session lead-in Start Time M/S/F: 0x%2.2uX/0x%2.2uX/0x%2.2uX\n",
		d[17], d[18], d[19]);
	Bprint(&bout, "\tLast Possible Start Time for Start of lead-out M/S/F: 0x%2.2uX/0x%2.2uX/0x%2.2uX\n",
		d[21], d[22], d[23]);

	for(n = 0; n < nbytes; n++){
		if(n && ((n & 0x0F) == 0))
			Bprint(&bout, "\n");
		Bprint(&bout, " %2.2uX", d[n]);
	}
	if(n && (n & 0x0F))
		Bputc(&bout, '\n');

	return nbytes;
}

static long
cmdrtrackinfo(ScsiReq *rp, int argc, char *argv[])
{
	uchar d[MaxDirData], track;
	char *sp;
	long n, nbytes;
	int dl;

	track = 0;
	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 1:
		if((track = strtoul(argv[0], &sp, 0)) == 0 && sp == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 0:
		break;
	}
	if((nbytes = SRrtrackinfo(rp, d, sizeof d, track)) == -1)
		return -1;

	dl = (d[0]<<8)|d[1];
	Bprint(&bout, "\tdata length: 0x%uX\n", dl);
	Bprint(&bout, "\Track Number %d\n", d[2]);
	Bprint(&bout, "\Session Number %d\n", d[3]);
	Bprint(&bout, "\tinfo[4] 0x%2.2uX\n", d[5]);
	Bprint(&bout, "\t\tTrack Mode 0x%2.2uX: ", d[5] & 0x0F);
	switch(d[5] & 0x0F){
	case 0x00:
	case 0x02:
		Bprint(&bout, "2 audio channels without pre-emphasis\n");
		break;
	case 0x01:
	case 0x03:
		Bprint(&bout, "2 audio channels with pre-emphasis of 50/15µs\n");
		break;
	case 0x08:
	case 0x0A:
		Bprint(&bout, "audio channels without pre-emphasis (reserved in CD-R/RW)\n");
		break;
	case 0x09:
	case 0x0B:
		Bprint(&bout, "audio channels with pre-emphasis of 50/15µs (reserved in CD-R/RW)\n");
		break;
	case 0x04:
	case 0x06:
		Bprint(&bout, "Data track, recorded uninterrupted\n");
		break;
	case 0x05:
	case 0x07:
		Bprint(&bout, "Data track, recorded incremental\n");
		break;
	default:
		Bprint(&bout, "(mode unknown)\n");
		break;
	}
	if(d[5] & 0x10)
		Bprint(&bout, "\t\tCopy\n");
	if(d[5] & 0x20)
		Bprint(&bout, "\t\tDamage\n");
	Bprint(&bout, "\tinfo[6] 0x%2.2uX\n", d[6]);
	Bprint(&bout, "\t\tData Mode 0x%2.2uX: ", d[6] & 0x0F);
	switch(d[6] & 0x0F){
	case 0x01:
		Bprint(&bout, "Mode 1 (ISO/IEC 10149)\n");
		break;
	case 0x02:
		Bprint(&bout, "Mode 2 (ISO/IEC 10149 or CD-ROM XA)\n");
		break;
	case 0x0F:
		Bprint(&bout, "Data Block Type unknown (no track descriptor block)\n");
		break;
	default:
		Bprint(&bout, "(Reserved)\n");
		break;
	}
	if(d[6] & 0x10)
		Bprint(&bout, "\t\tFP\n");
	if(d[6] & 0x20)
		Bprint(&bout, "\t\tPacket\n");
	if(d[6] & 0x40)
		Bprint(&bout, "\t\tBlank\n");
	if(d[6] & 0x80)
		Bprint(&bout, "\t\tRT\n");
	Bprint(&bout, "\tTrack Start Address 0x%8.8uX\n",
		(d[8]<<24)|(d[9]<<16)|(d[10]<<8)|d[11]);
	if(d[7] & 0x01)
		Bprint(&bout, "\tNext Writeable Address 0x%8.8uX\n",
			(d[12]<<24)|(d[13]<<16)|(d[14]<<8)|d[15]);
	Bprint(&bout, "\tFree Blocks 0x%8.8uX\n",
		(d[16]<<24)|(d[17]<<16)|(d[18]<<8)|d[19]);
	if((d[6] & 0x30) == 0x30)
		Bprint(&bout, "\tFixed Packet Size 0x%8.8uX\n",
			(d[20]<<24)|(d[21]<<16)|(d[22]<<8)|d[23]);
	Bprint(&bout, "\tTrack Size 0x%8.8uX\n",
		(d[24]<<24)|(d[25]<<16)|(d[26]<<8)|d[27]);

	for(n = 0; n < nbytes; n++){
		if(n && ((n & 0x0F) == 0))
			Bprint(&bout, "\n");
		Bprint(&bout, " %2.2uX", d[n]);
	}
	if(n && (n & 0x0F))
		Bputc(&bout, '\n');

	return nbytes;
}

static long
cmdcdpause(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SRcdpause(rp, 0);
}

static long
cmdcdresume(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SRcdpause(rp, 1);
}

static long
cmdcdstop(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SRcdstop(rp);
}

static long
cmdcdplay(ScsiReq *rp, int argc, char *argv[])
{
	long length, start;
	char *sp;
	int raw;

	raw = 0;
	start = 0;
	if(argc && strcmp("-r", argv[0]) == 0){
		raw = 1;
		argc--, argv++;
	}

	length = 0xFFFFFFFF;
	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 2:
		if(!raw || ((length = strtol(argv[1], &sp, 0)) == 0 && sp == argv[1])){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((start = strtol(argv[0], &sp, 0)) == 0 && sp == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 0:
		break;
	}

	return SRcdplay(rp, raw, start, length);
}

static long
cmdcdload(ScsiReq *rp, int argc, char *argv[])
{
	char *p;
	ulong slot;

	slot = 0;
	if(argc && (slot = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
		rp->status = Status_BADARG;
		return -1;
	}
	return SRcdload(rp, 1, slot);
}

static long
cmdcdunload(ScsiReq *rp, int argc, char *argv[])
{
	char *p;
	ulong slot;

	slot = 0;
	if(argc && (slot = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
		rp->status = Status_BADARG;
		return -1;
	}
	return SRcdload(rp, 0, slot);
}

static long
cmdcdstatus(ScsiReq *rp, int argc, char *argv[])
{
	uchar *list, *lp;
	long nbytes, status;
	int i, slots;

	USED(argc, argv);

	nbytes = 4096;
	list = malloc(nbytes);
	if(list == 0){
		rp->status = STnomem;
		return -1;
	}
	status = SRcdstatus(rp, list, nbytes);
	if(status == -1){
		free(list);
		return -1;
	}

	lp = list;
	Bprint(&bout, " Header\n   ");
	for(i = 0; i < 8; i++){				/* header */
		Bprint(&bout, " %2.2uX", *lp);
		lp++;
	}
	Bputc(&bout, '\n');

	slots = ((list[6]<<8)|list[7])/4;
	Bprint(&bout, " Slots\n   ");
	while(slots--){
		Bprint(&bout, " %2.2uX %2.2uX %2.2uX %2.2uX\n   ",
			*lp, *(lp+1), *(lp+2), *(lp+3));
		lp += 4;
	}

	free(list);
	return status;
}

static long
cmdgetconf(ScsiReq *rp, int argc, char *argv[])
{
	uchar *list;
	long nbytes, status;

	USED(argc, argv);

	nbytes = 4096;
	list = malloc(nbytes);
	if(list == 0){
		rp->status = STnomem;
		return -1;
	}
	status = SRgetconf(rp, list, nbytes);
	if(status == -1){
		free(list);
		return -1;
	}
	/* to be done... */
	free(list);
	return status;
}

static long
cmdfwaddr(ScsiReq *rp, int argc, char *argv[])
{
	uchar d[MaxDirData], npa, track, mode;
	long n;
	char *p;

	npa = mode = track = 0;
	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 3:
		if((npa = strtoul(argv[1], &p, 0)) == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 2:
		if((mode = strtoul(argv[1], &p, 0)) == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((track = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		break;

	case 0:
		break;
	}
	if((n = SRfwaddr(rp, track, mode, npa, d)) == -1)
		return -1;
	Bprint(&bout, "%ud %ud\n", d[0], (d[1]<<24)|(d[2]<<16)|(d[3]<<8)|d[4]);
	return n;
}

static long
cmdtreserve(ScsiReq *rp, int argc, char *argv[])
{
	long nbytes;
	char *p;

	if(argc != 1 || ((nbytes = strtoul(argv[0], &p, 0)) == 0 && p == argv[0])){
		rp->status = Status_BADARG;
		return -1;
	}
	return SRtreserve(rp, nbytes);
}

static long
cmdtrackinfo(ScsiReq *rp, int argc, char *argv[])
{
	uchar d[MaxDirData], track;
	long n;
	ulong ul;
	char *p;

	track = 0;
	if(argc && (track = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
		rp->status = Status_BADARG;
		return -1;
	}
	if((n = SRtinfo(rp, track, d)) == -1)
		return -1;
	Bprint(&bout, "buffer length: 0x%uX\n", d[0]);
	Bprint(&bout, "number of tracks: 0x%uX\n", d[1]);
	ul = (d[2]<<24)|(d[3]<<16)|(d[4]<<8)|d[5];
	Bprint(&bout, "start address: 0x%luX\n", ul);
	ul = (d[6]<<24)|(d[7]<<16)|(d[8]<<8)|d[9];
	Bprint(&bout, "track length: 0x%luX\n", ul);
	Bprint(&bout, "track mode: 0x%uX\n", d[0x0A] & 0x0F);
	Bprint(&bout, "track status: 0x%uX\n", (d[0x0A]>>4) & 0x0F);
	Bprint(&bout, "data mode: 0x%uX\n", d[0x0B] & 0x0F);
	ul = (d[0x0C]<<24)|(d[0x0D]<<16)|(d[0x0E]<<8)|d[0x0F];
	Bprint(&bout, "free blocks: 0x%luX\n", ul);
	return n;
}

static long
cmdwtrack(ScsiReq *rp, int argc, char *argv[])
{
	uchar mode, track;
	long n, nbytes, total, x;
	int fd, pid;
	char *p;

	mode = track = 0;
	nbytes = 0;
	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 4:
		if((mode = strtoul(argv[3], &p, 0)) == 0 && p == argv[3]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 3:
		if((track = strtoul(argv[2], &p, 0)) == 0 && p == argv[2]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 2:
		if((nbytes = strtoul(argv[1], &p, 0)) == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((fd = mkfile(argv[0], OREAD, &pid)) == -1){
			rp->status = Status_BADARG;
			return -1;
		}
		break;
	}
	total = 0;
	n = MIN(nbytes, maxiosize);
	if((n = readn(fd, rwbuf, n)) == -1){
		fprint(2, "file read failed %r\n");
		close(fd);
		return -1;
	}
	if((x = SRwtrack(rp, rwbuf, n, track, mode)) != n){
		fprint(2, "wtrack: write incomplete: asked %ld, did %ld\n", n, x);
		if(rp->status == STok)
			rp->status = Status_SW;
		close(fd);
		return -1;
	}
	nbytes -= n;
	total += n;
	while(nbytes){
		n = MIN(nbytes, maxiosize);
		if((n = read(fd, rwbuf, n)) == -1){
			break;
		}
		if((x = SRwrite(rp, rwbuf, n)) != n){
			fprint(2, "write: write incomplete: asked %ld, did %ld\n", n, x);
			if(rp->status == STok)
				rp->status = Status_SW;
			break;
		}
		nbytes -= n;
		total += n;
	}
	close(fd);
	if(pid >= 0 && waitfor(pid)){
		rp->status = Status_SW;
		return -1;
	}
	return total;
}

static long
cmdload(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SRmload(rp, 0);
}

static long
cmdunload(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SRmload(rp, 1);
}

static long
cmdfixation(ScsiReq *rp, int argc, char *argv[])
{
	uchar type;
	char *p;

	type = 0;
	if(argc && (type = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
		rp->status = Status_BADARG;
		return -1;
	}
	return SRfixation(rp, type);
}

static long
cmdeinit(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SReinitialise(rp);
}

static long
cmdmmove(ScsiReq *rp, int argc, char *argv[])
{
	int transport, source, destination, invert;
	char *p;

	invert = 0;

	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 4:
		if((invert = strtoul(argv[3], &p, 0)) == 0 && p == argv[3]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 3:
		if((transport = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		if((source = strtoul(argv[1], &p, 0)) == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		if((destination = strtoul(argv[2], &p, 0)) == 0 && p == argv[2]){
			rp->status = Status_BADARG;
			return -1;
		}
		break;
	}

	return SRmmove(rp, transport, source, destination, invert);
}

static long
cmdestatus(ScsiReq *rp, int argc, char *argv[])
{
	uchar *list, *lp, type;
	long d, i, n, nbytes, status;
	char *p;

	type = 0;
	nbytes = 4096;

	switch(argc){

	default:
		rp->status = Status_BADARG;
		return -1;

	case 2:
		if((nbytes = strtoul(argv[1], &p, 0)) == 0 && p == argv[1]){
			rp->status = Status_BADARG;
			return -1;
		}
		/*FALLTHROUGH*/

	case 1:
		if((type = strtoul(argv[0], &p, 0)) == 0 && p == argv[0]){
			rp->status = Status_BADARG;
			return -1;
		}
		break;

	case 0:
		break;
	}

	list = malloc(nbytes);
	if(list == 0){
		rp->status = STnomem;
		return -1;
	}
	status = SRestatus(rp, type, list, nbytes);
	if(status == -1){
		free(list);
		return -1;
	}

	lp = list;
	nbytes = ((lp[5]<<16)|(lp[6]<<8)|lp[7])-8;
	Bprint(&bout, " Header\n   ");
	for(i = 0; i < 8; i++){				/* header */
		Bprint(&bout, " %2.2uX", *lp);
		lp++;
	}
	Bputc(&bout, '\n');

	while(nbytes > 0){				/* pages */
		i = ((lp[5]<<16)|(lp[6]<<8)|lp[7]);
		nbytes -= i+8;
		Bprint(&bout, " Type");
		for(n = 0; n < 8; n++)			/* header */
			Bprint(&bout, " %2.2uX", lp[n]);
		Bprint(&bout, "\n   ");
		d = (lp[2]<<8)|lp[3];
		lp += 8;
		for(n = 0; n < i; n++){
			if(n && (n % d) == 0)
				Bprint(&bout, "\n   ");
			Bprint(&bout, " %2.2uX", *lp);
			lp++;
		}
		if(n && (n % d))
			Bputc(&bout, '\n');
	}

	free(list);
	return status;
}

static long
cmdhelp(ScsiReq *rp, int argc, char *argv[])
{
	ScsiCmd *cp;
	char *p;

	USED(rp);
	if(argc)
		p = argv[0];
	else
		p = 0;
	for(cp = scsicmd; cp->name; cp++){
		if(p == 0 || strcmp(p, cp->name) == 0)
			Bprint(&bout, "%s\n", cp->help);
	}
	return 0;
}

static long
cmdprobe(ScsiReq *rp, int argc, char *argv[])
{
	char buf[32];
	ScsiReq scsireq;
	char *ctlr, *unit;

	USED(argc, argv);
	rp->status = STok;
	scsireq.flags = 0;

	for(ctlr="CDEFGHIJ0123456789abcdef"; *ctlr; ctlr++) {
		/*
		 * I can guess how many units you have.
		 * SATA controllers can have more than two drives each.
		 */
		if(*ctlr >= 'C' && *ctlr <= 'D')
			unit = "01";
		else if((*ctlr >= '0' && *ctlr <= '9')
		     || (*ctlr >= 'a' && *ctlr <= 'f'))
			unit = "0123456789abcdef";	/* allow wide scsi */
		else
			unit = "01234567";

		for(; *unit; unit++){
			sprint(buf, "/dev/sd%c%c", *ctlr, *unit);
			if(SRopenraw(&scsireq, buf) == -1)
				continue;
			SRreqsense(&scsireq);
			switch(scsireq.status){
			case STok:
			case Status_SD:
				Bprint(&bout, "%s: ", buf);
				cmdinquiry(&scsireq, 0, 0);
				break;
			}
			SRclose(&scsireq);
		}
	}
	return 0;
}

static long
cmdclose(ScsiReq *rp, int argc, char *argv[])
{
	USED(argc, argv);
	return SRclose(rp);
}

static long
cmdopen(ScsiReq *rp, int argc, char *argv[])
{
	int raw;
	long status;

	raw = 0;
	if(argc && strcmp("-r", argv[0]) == 0){
		raw = 1;
		argc--, argv++;
	}
	if(argc != 1){
		rp->status = Status_BADARG;
		return -1;
	}
	if(raw == 0){
		if((status = SRopen(rp, argv[0])) != -1 && verbose)
			Bprint(&bout, "%sblock size: %ld\n",
				rp->flags&Fbfixed? "fixed ": "", rp->lbsize);
	}
	else {
		status = SRopenraw(rp, argv[0]);
		rp->lbsize = 512;
	}
	return status;
}

static ScsiCmd scsicmd[] = {
	{ "ready",	cmdready,	1,		/*[0x00]*/
	  "ready",
	},
	{ "rewind",	cmdrewind,	1,		/*[0x01]*/
	  "rewind",
	},
	{ "rezero",	cmdrewind,	1,		/*[0x01]*/
	  "rezero",
	},
	{ "reqsense",	cmdreqsense,	1,		/*[0x03]*/
	  "reqsense",
	},
	{ "format",	cmdformat,	0,		/*[0x04]*/
	  "format",
	},
	{ "rblimits",	cmdrblimits,	1,		/*[0x05]*/
	  "rblimits",
	},
	{ "read",	cmdread,	1,		/*[0x08]*/
	  "read [|]file [nbytes]",
	},
	{ "write",	cmdwrite,	1,		/*[0x0A]*/
	  "write [|]file [nbytes]",
	},
	{ "seek",	cmdseek,	1,		/*[0x0B]*/
	  "seek offset [whence]",
	},
	{ "filemark",	cmdfilemark,	1,		/*[0x10]*/
	  "filemark [howmany]",
	},
	{ "space",	cmdspace,	1,		/*[0x11]*/
	  "space [-f] [-b] [[--] howmany]",
	},
	{ "inquiry",	cmdinquiry,	1,		/*[0x12]*/
	  "inquiry",
	},
	{ "logsense",	cmdlogsense, 1,		/*[0x4d]*/
	  "modesense [page [nbytes]]",
	},
	{ "modeselect6",cmdmodeselect6,	1,		/*[0x15] */
	  "modeselect6 bytes...",
	},
	{ "modeselect",	cmdmodeselect10, 1,		/*[0x55] */
	  "modeselect bytes...",
	},
	{ "modesense6",	cmdmodesense6,	1,		/*[0x1A]*/
	  "modesense6 [page [nbytes]]",
	},
	{ "modesense",	cmdmodesense10, 1,		/*[0x5A]*/
	  "modesense [page [nbytes]]",
	},
	{ "mediaserial",	cmdmediaserial, 1,		/*[0xab]*/
	  "mediaserial",
	},
	{ "vpd",	cmdvpd, 1,		/*[inquiry]*/
	  "vpd [page]",
	},
	{ "start",	cmdstart,	1,		/*[0x1B]*/
	  "start [code]",
	},
	{ "stop",	cmdstop,	1,		/*[0x1B]*/
	  "stop",
	},
	{ "eject",	cmdeject,	1,		/*[0x1B]*/
	  "eject",
	},
	{ "ingest",	cmdingest,	1,		/*[0x1B]*/
	  "ingest",
	},
	{ "capacity",	cmdcapacity,	1,		/*[0x25]*/
	  "capacity",
	},
	{ "capacity16",	cmdcapacity16,	1,		/*[0x9e]*/
	  "capacity16",
	},
	{ "readattr",	cmdreadattr,	1,		/*[0x8c]*/
	  "readattr",
	},

	{ "blank",	cmdblank,	1,		/*[0xA1]*/
	  "blank [track/LBA [type]]",
	},
	{ "synccache",	cmdsynccache,	1,		/*[0x35]*/
	  "synccache",
	},
	{ "rtoc",	cmdrtoc,	1,		/*[0x43]*/
	  "rtoc [track/session-number [format]]",
	},
	{ "rdiscinfo",	cmdrdiscinfo,	1,		/*[0x51]*/
	  "rdiscinfo",
	},
	{ "rtrackinfo",	cmdrtrackinfo,	1,		/*[0x52]*/
	  "rtrackinfo [track]",
	},

	{ "cdpause",	cmdcdpause,	1,		/*[0x4B]*/
	  "cdpause",
	},
	{ "cdresume",	cmdcdresume,	1,		/*[0x4B]*/
	  "cdresume",
	},
	{ "cdstop",	cmdcdstop,	1,		/*[0x4E]*/
	  "cdstop",
	},
	{ "cdplay",	cmdcdplay,	1,		/*[0xA5]*/
	  "cdplay [track-number] or [-r [LBA [length]]]",
	},
	{ "cdload",	cmdcdload,	1,		/*[0xA6*/
	  "cdload [slot]",
	},
	{ "cdunload",	cmdcdunload,	1,		/*[0xA6]*/
	  "cdunload [slot]",
	},
	{ "cdstatus",	cmdcdstatus,	1,		/*[0xBD]*/
	  "cdstatus",
	},
//	{ "getconf",	cmdgetconf,	1,		/*[0x46]*/
//	  "getconf",
//	},

//	{ "fwaddr",	cmdfwaddr,	1,		/*[0xE2]*/
//	  "fwaddr [track [mode [npa]]]",
//	},
//	{ "treserve",	cmdtreserve,	1,		/*[0xE4]*/
//	  "treserve nbytes",
//	},
//	{ "trackinfo",	cmdtrackinfo,	1,		/*[0xE5]*/
//	  "trackinfo [track]",
//	},
//	{ "wtrack",	cmdwtrack,	1,		/*[0xE6]*/
//	  "wtrack [|]file [nbytes [track [mode]]]",
//	},
//	{ "load",	cmdload,	1,		/*[0xE7]*/
//	  "load",
//	},
//	{ "unload",	cmdunload,	1,		/*[0xE7]*/
//	  "unload",
//	},
//	{ "fixation",	cmdfixation,	1,		/*[0xE9]*/
//	  "fixation [toc-type]",
//	},
	{ "einit",	cmdeinit,	1,		/*[0x07]*/
	  "einit",
	},
	{ "estatus",	cmdestatus,	1,		/*[0xB8]*/
	  "estatus",
	},
	{ "mmove",	cmdmmove,	1,		/*[0xA5]*/
	  "mmove transport source destination [invert]",
	},

	{ "help",	cmdhelp,	0,
	  "help",
	},
	{ "probe",	cmdprobe,	0,
	  "probe",
	},
	{ "close",	cmdclose,	1,
	  "close",
	},
	{ "open",	cmdopen,	0,
	  "open [-r] sddev",
	},
	{ 0, 0 },
};

#define	SEP(c)	(((c)==' ')||((c)=='\t')||((c)=='\n'))

static char *
tokenise(char *s, char **start, char **end)
{
	char *to;
	Rune r;
	int n;

	while(*s && SEP(*s))				/* skip leading white space */
		s++;
	to = *start = s;
	while(*s){
		n = chartorune(&r, s);
		if(SEP(r)){
			if(to != *start)		/* we have data */
				break;
			s += n;				/* null string - keep looking */
			while(*s && SEP(*s))
				s++;
			to = *start = s;
		}
		else if(r == '\''){
			s += n;				/* skip leading quote */
			while(*s){
				n = chartorune(&r, s);
				if(r == '\''){
					if(s[1] != '\'')
						break;
					s++;		/* embedded quote */
				}
				while (n--)
					*to++ = *s++;
			}
			if(!*s)				/* no trailing quote */
				break;
			s++;				/* skip trailing quote */
		}
		else  {
			while(n--)
				*to++ = *s++;
		}
	}
	*end = to;
	return s;
}

static int
parse(char *s, char *fields[], int nfields)
{
	int c, argc;
	char *start, *end;

	argc = 0;
	c = *s;
	while(c){
		s = tokenise(s, &start, &end);
		c = *s++;
		if(*start == 0)
			break;
		if(argc >= nfields-1)
			return -1;
		*end = 0;
		fields[argc++] = start;
	}
	fields[argc] = 0;
	return argc;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-6eq] [-m maxiosize] [[-r] /dev/sdXX]\n", argv0);
	exits("usage");
}

static struct {
	int	status;
	char*	description;
} description[] = {
	STnomem,	"buffer allocation failed",
	STtimeout,	"bus timeout",
	STharderr,	"controller error of some kind",
	STok,		"good",
	STcheck,	"check condition",
	STcondmet,	"condition met/good",
	STbusy,		"busy ",
	STintok,	"intermediate/good",
	STintcondmet,	"intermediate/condition met/good",
	STresconf,	"reservation conflict",
	STterminated,	"command terminated",
	STqfull,	"queue full",

	Status_SD,	"sense-data available",
	Status_SW,	"internal software error",
	Status_BADARG,	"bad argument to request",

	0, 0,
};

void
main(int argc, char *argv[])
{
	ScsiReq target;
	char *ap, *av[256];
	int ac, i, raw = 0;
	ScsiCmd *cp;
	long status;

	ARGBEGIN {
	case 'e':
		exabyte = 1;
		/* fallthrough */
	case '6':
		force6bytecmds = 1;
		break;
	case 'm':
		maxiosize = atol(EARGF(usage()));
		if(maxiosize < 512 || maxiosize > MaxIOsize)
			sysfatal("max-xfer < 512 or > %d", MaxIOsize);
		break;
	case 'r':			/* must be last option and not bundled */
		raw++;
		break;
	case 'q':
		verbose = 0;
		break;
	default:
		usage();
	} ARGEND

	if(Binit(&bin, 0, OREAD) == Beof || Binit(&bout, 1, OWRITE) == Beof){
		fprint(2, "%s: can't init bio: %r\n", argv0);
		exits("Binit");
	}

	memset(&target, 0, sizeof target);
	if (raw) {			/* hack for -r */
		++argc;
		--argv;
	}
	if(argc && cmdopen(&target, argc, argv) == -1) {
		fprint(2, "open failed\n");
		usage();
	}
	Bflush(&bout);

	while(ap = Brdline(&bin, '\n')){
		ap[Blinelen(&bin)-1] = 0;
		switch(ac = parse(ap, av, nelem(av))){

		default:
			for(cp = scsicmd; cp->name; cp++){
				if(strcmp(cp->name, av[0]) == 0)
					break;
			}
			if(cp->name == 0){
				Bprint(&bout, "eh?\n");
				break;
			}
			if((target.flags & Fopen) == 0 && cp->open){
				Bprint(&bout, "no current target\n");
				break;
			}
			if((status = (*cp->f)(&target, ac-1, &av[1])) != -1){
				if(verbose)
					Bprint(&bout, "ok %ld\n", status);
				break;
			}
			for(i = 0; description[i].description; i++){
				if(target.status != description[i].status)
					continue;
				if(target.status == Status_SD)
					makesense(&target);
				else
					Bprint(&bout, "%s\n", description[i].description);
				break;
			}
			break;

		case -1:
			Bprint(&bout, "eh?\n");
			break;

		case 0:
			break;
		}
		Bflush(&bout);
	}
	exits(0);
}

/* USB mass storage fake */
long
umsrequest(Umsc *umsc, ScsiPtr *cmd, ScsiPtr *data, int *status)
{
	USED(umsc, data, cmd);
	*status = STharderr;
	return -1;
}
