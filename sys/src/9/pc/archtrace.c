#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"archtrace.h"

#pragma	profile	0

/*
 * this is the wrong place for this, and a very large hammer,
 * but at least we are controlling access to kernel memory.
 * must match mmuinit()
 */
static void
kmem(int write)
{
	PTE x, *p;

	for(x = KTZERO; x < (ulong)etext; x += BY2PG){
		p = mmuwalk(m->pdb, x, 2, 0);
		if(p == nil)
			panic("kmem");
		if(write)
			*p |= PTEWRITE;
		else
			*p &= ~PTEWRITE;
	}
}

enum {
	CALL	= 0xe8,
	JMP	= 0xeb,
	RET	= 0xc3,
	NOP	= 0x90,
};

char*
archtracectlr(Archtrace *a, char *p, char *e)
{
	int i;

	for(i = 0; i < a->nprobe; i++)
		p = seprint(p, e, "#  tracept %c %#.8p\n",
			a->probe[i]==NOP? 'X': 'E', a->text[i]);
	return p;
}

void
archtraceinstall(Archtrace *a)
{
	int i;

	kmem(1);
	for(i = 0; i < a->nprobe; i++)
		*a->text[i] = a->probe[i];
	kmem(0);
}

void
archtraceuninstall(Archtrace *a)
{
	int i;

	kmem(1);
	for(i = 0; i < a->nprobe; i++)
		*a->text[i] = a->orig[i];
	kmem(0);
}

static int
call4(uchar *call, uchar *targ)
{
	uchar buf[5];
	ulong l;

	buf[0] = CALL;
	l = targ - (call + 5);
	buf[1] = l;
	buf[2] = l>>8;
	buf[3] = l>>16;
	buf[4] = l>>24;
	return memcmp(buf, call, 5);
}

static uchar startsig[] = {JMP, 0x05};

int
mkarchtrace(Archtrace *a, uchar *start, uchar **endp)
{
	uchar *p, *end;

	/*
	 * search by signatures.  we don't really
	 * care we get a whole function or not.
	 */
	end = *endp;
	for(p = start; p + 6 <= end; p++)
		if(memcmp(p, startsig, sizeof startsig) == 0){
			if(call4(p + 2, (uchar*)_tracein) == 0){
				a->text[a->nprobe] = start + 1;
				a->orig[a->nprobe] = *a->text[a->nprobe];
				a->probe[a->nprobe] = 0;		/* jmp 2f */
				a->nprobe++;
			}
		}else  if(p[0] == RET && p[6] == RET){
			if(call4(p + 1, (uchar*)_traceout) == 0){
				if(a->nprobe == Maxprobe)
					error("too many exits");
				a->text[a->nprobe] = p;
				a->orig[a->nprobe] = *a->text[a->nprobe];
				a->probe[a->nprobe] = NOP;
				a->nprobe++;
			}
		}
//	*endp = a->text[a->nprobe - 1];
	return 0;
}
