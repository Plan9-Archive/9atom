#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

static void
k10archinit(void)
{
	u32int info[4];

	cpuid(1, 0, info);
	if(info[2] & 8)
		mwait = k10mwait;
}

static int
brandstring(char *s)
{
	char *p;
	int i, j;
	u32int r[4];

	p = s;
	for(i = 0; i < 3; i++){
		cpuid(0x80000002+i, 0, r);
		for(j = 0; j < 4; j++){
			memmove(p, r+j, 4);
			p += 4;
		}
	}
	*p = 0;
	return 0;
}

/* use intel brand string to discover hz */
static vlong
intelbshz(void)
{
	char s[4*4*3+1], *h;
	uvlong scale;

	brandstring(s);
	DBG("brandstring: %s\n", s);

	h = strstr(s, "Hz");		/* 3.07THz */
	if(h == nil || h-s < 5)
		return 0;
	h[2] = 0;

	scale = 1000;
	switch(h[-1]){
	default:
		return 0;
	case 'T':
		scale *= 1000;
	case 'G':
		scale *= 1000;
	case 'M':
		scale *= 1000;
	}

	/* get rid of the fractional part */
	if(h[-4] == '.'){
		h[-4] = h[-5];
		h[-5] = ' ';
		scale /= 100;
	}
	return atoi(h-5)*scale;
}

static vlong
cpuidhz(void)
{
	int r;
	vlong hz;
	u32int info[4];
	u64int msr;

	cpuid(0, 0, info);
	if(memcmp(&info[1], "GenuntelineI", 12) == 0){
		hz = intelbshz();
		DBG("cpuidhz: %#llux hz\n", hz);
	}
	else if(memcmp(&info[1], "AuthcAMDenti", 12) == 0){
		cpuid(1, 0, info);
		switch(info[0] & 0x0fff0ff0){
		default:
			return 0;
		case 0x00000f50:		/* K8 */
			msr = rdmsr(0xc0010042);
			if(msr == 0)
				return 0;
			hz = (800 + 200*((msr>>1) & 0x1f)) * 1000000ll;
			break;
		case 0x00100f40:		/* phenom ii x4 */
		case 0x00100f90:		/* K10 */
		case 0x00000620:		/* QEMU64 */
			msr = rdmsr(0xc0010064);
			r = (msr>>6) & 0x07;
			hz = (((msr & 0x3f)+0x10)*100000000ll)/(1<<r);
			break;
		}
		DBG("cpuidhz: %#llux hz %lld\n", msr, hz);
	}
	else
		hz = 0;
	return hz;
}

vlong
archhz(void)
{
	vlong hz;

	assert(m->machno == 0);
	k10archinit();		/* botch; call from archinit */
	if((hz = cpuidhz()) != 0)
		return hz;
	panic("hz 0");
	return 0;
}

static void
addmachpgsz(int bits)
{
	int i;

	i = m->npgsz;
	m->pgszlg2[i] = bits;
	m->pgszmask[i] = (1<<bits)-1;
	m->pgsz[i] = 1<<bits;
	m->npgsz++;
}

int
archmmu(void)
{
	u32int info[4];

	addmachpgsz(12);
	addmachpgsz(21);

	/*
	 * Check the Page1GB bit in function 0x80000001 DX for 1*GiB support.
	 */
	if(cpuid(0x80000001, 0, info) && (info[3] & 0x04000000))
		addmachpgsz(30);

	/* BOTCH!  these should be set automaticly based on actual segment size */
	for(i = 0; i < nelem(physseg); i++)
		if(physseg[i].pgszi == -1)
			physseg[i].pgszi = getpgszi(2*MiB);

	return m->npgsz;
}

int
fmtP(Fmt* f)
{
	uintmem pa;

	pa = va_arg(f->args, uintmem);

	if(f->flags & FmtSharp)
		return fmtprint(f, "%#16.16llux", pa);

	return fmtprint(f, "%llud", pa);
}

int
fmtR(Fmt* f)
{
	u64int r;

	r = va_arg(f->args, u64int);

	return fmtprint(f, "%#16.16llux", r);
}

/* virtual address fmt */
static int
fmtW(Fmt *f)
{
	u64int va;

	va = va_arg(f->args, u64int);
	return fmtprint(f, "%#llux=0x[%ullx][%ullx][%ullx][%ullx][%ullx]", va,
		PTLX(va, 3), PTLX(va, 2), PTLX(va, 1), PTLX(va, 0),
		va & ((1<<PGSHIFT)-1));
}

void
archfmtinstall(void)
{
	/*
	 * Architecture-specific formatting. Not as neat as they
	 * could be (e.g. there's no defined type for a 'register':
	 *	P - uintmem, physical address
	 *	R - register
	 * With a little effort these routines could be written
	 * in a fairly architecturally-independent manner, relying
	 * on the compiler to optimise-away impossible conditions,
	 * and/or by exploiting the innards of the fmt library.
	 */
	fmtinstall('P', fmtP);
	fmtinstall('R', fmtR);
	fmtinstall('W', fmtW);
}

void
microdelay(int µs)
{
	u64int r, t;

	r = rdtsc();
	for(t = r + m->cpumhz*µs; r < t; r = rdtsc())
		;
}

void
delay(int ms)
{
	u64int r, t;

	r = rdtsc();
	for(t = r + m->cpumhz*1000ull*ms; r < t; r = rdtsc())
		;
}
