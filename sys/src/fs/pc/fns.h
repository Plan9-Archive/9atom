#include "../port/portfns.h"

void	aamloop(int);
void	cgaputc(int);
void	cgaputs(char*, int);
void	cmd_e820(int, char**);
void	(*coherence)(void);
void	cpuid(char*, ulong*, ulong*);
void	etherinit(void);
void	etherstart(void);
int	floppyinit(void);
void	floppyproc(void);
Off	floppyread(int, void*, long, Devsize);
Off	floppywrite(int, void*, long, Devsize);
void	fpinit(void);
vlong	getatapartoff(int, char*);
char*	getconf(char*);
ulong	getcr0(void);
ulong	getcr2(void);
ulong	getcr3(void);
ulong	getcr4(void);
int	getfields(char*, char**, int, int, char*);
ulong	getstatus(void);
int	atainit(void);
Off	ataread(int, void*, long, Devsize);
Off	atawrite(int, void*, long, Devsize);
void	i8042a20(void);
void	i8042reset(void);
void	idle(void);
int	inb(int);
void	insb(int, void*, int);
ushort	ins(int);
void	inss(int, void*, int);
ulong	inl(int);
void	insl(int, void*, int);
void	kbdinit(void);
int	kbdintr0(void);
int	kbdgetc(void);
ulong*	mapaddr(ulong);
void	mb386(void);
void	mb586(void);
void	mfence(void);
void	microdelay(int);
void	mmuinit(void);
uchar	nvramread(int);
void	outb(int, int);
void	outsb(int, void*, int);
void	outs(int, ushort);
void	outss(int, void*, int);
void	outl(int, ulong);
void	outsl(int, void*, int);
void	printcpufreq(void);
void	putgdt(Segdesc*, int);
void	putidt(Segdesc*, int);
void	putcr3(ulong);
void	putcr4(ulong);
void	puttr(ulong);
void	rdmsr(int, vlong*);
void	wrmsr(int, vlong);
void	(*cycles)(uvlong*);
void	scsiinit(void);
Off	scsiread(int, void*, long);
Devsize	scsiseek(int, Devsize);
Off	scsiwrite(int, void*, long);
int	setatapart(int, char*);
int	setscsipart(int, char*);
void	setvec(int, void (*)(Ureg*, void*), void*);
int	tas(Lock*);
void	trapinit(void);
void	uartspecial(int, void (*)(int), int (*)(void), int);
int	uartgetc(void);
void	uartputc(int);
void	uartputs(char*);
void*	vmap(ulong, int);

#define PADDR(a)	((uintptr)(a)-KZERO)
#define PCIWADDR(a)	PADDR(a)
#define	Pciwaddrl(a)	PADDR(a)
#define	Pciwaddrh(a)	0

/* pata */
void	ideinit(Device*);
Devsize	idesize(Device*);
int	ideread(Device*,  Devsize, void*);
int	idewrite(Device*, Devsize, void*);
int	idesecsize(Device*);

/* sata */
void	mvinit(Device*);
Devsize	mvsize(Device*);
int	mvread(Device*,  Devsize, void*);
int	mvwrite(Device*, Devsize, void*);

/* aoe */
void	aoeinit(Device*);
Devsize	aoesize(Device*);
int	aoeread(Device*,  Devsize, void*);
int	aoewrite(Device*, Devsize, void*);

/* iasata */
void	iainit(Device*);
Devsize	iasize(Device*);
int	iaread(Device*,  Devsize, void*);
int	iawrite(Device*, Devsize, void*);
