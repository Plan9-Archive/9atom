#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/uartp8250.h"

/*
 * 8250 UART and compatibles.
 */
enum {
	Uart0		= 0x3F8,	/* COM1 */
	Uart0IRQ	= 4,
	Uart1		= 0x2F8,	/* COM2 */
	Uart1IRQ	= 3,

	UartFREQ	= 1843200,
};

extern PhysUart i8250physuart;	/* BOTCH; should use p8250 directly */

static uint
ioget(void *v, int i)
{
	uintptr io;

	io = (uintptr)v;
	return inb(io+i);
}

static void
ioset(void *v, int i, uint x)
{
	uintptr io;

	io = (uintptr)v;
	outb(io+i, x);
}

static int
itr(Uart *u, int on)
{
	Ctlr *c;

	c = u->regs;
	if(on)
		intrenable(c->irq, i8250interrupt, u, c->tbdf, u->name);
	else
		/* this causes hangs. please debug. */
//		return intrdisable(c->irq, i8250interrupt, u, c->tbdf, u->name);
		return -1;
	return 0;
}

PhysUart i8250physuart;

static Ctlr i8250ctlr[2] = {
{	.reg	= (void*)Uart0,
	.get	= ioget,
	.set	= ioset,
	.itr	= itr,
	.irq	= Uart0IRQ,
	.tbdf	= BUSUNKNOWN, },

{	.reg	= (void*)Uart1,
	.get	= ioget,
	.set	= ioset,
	.itr	= itr,
	.irq	= Uart1IRQ,
	.tbdf	= BUSUNKNOWN, },
};

static Uart i8250uart[2] = {
{	.regs	= &i8250ctlr[0],
	.name	= "COM1",
	.freq	= UartFREQ,
	.phys	= &i8250physuart,
	.special	= 0,
	.next	= &i8250uart[1], },

{	.regs	= &i8250ctlr[1],
	.name	= "COM2",
	.freq	= UartFREQ,
	.phys	= &i8250physuart,
	.special	= 0,
	.next	= nil, },
};

static Uart*
pc8250pnp(void)
{
	return i8250uart;
}

void*
i8250alloc(int io, int irq, int tbdf)
{
	Ctlr *ctlr;

	if((ctlr = malloc(sizeof(Ctlr))) != nil){
		ctlr->reg = (void*)io;
		ctlr->irq = irq;
		ctlr->tbdf = tbdf;
		ctlr->get = ioget;
		ctlr->set = ioset;
		ctlr->itr = itr;
	}
	return ctlr;
}

void
i8250console(void)
{
	int n;
	char *cmd;
	static int once;

	if(once == 0){
		once = 1;
		memmove(&i8250physuart, &p8250physuart, sizeof(PhysUart));
		i8250physuart.name = "i8250";
		i8250physuart.pnp = pc8250pnp;
	}

	n = uartconsconf(&cmd);
	if(n == 0 || n == 1){
		qlock(i8250uart + n);
		uartctl(i8250uart + n, "z");
		uartctl(i8250uart + n, cmd);
		qunlock(i8250uart + n);
	}
}

void
i8250mouse(char* which, int (*putc)(Queue*, int), int setb1200)
{
	char *p;
	int port;

	port = strtol(which, &p, 0);
	if(p == which || port < 0 || port > 1)
		error(Ebadarg);
	uartmouse(&i8250uart[port], putc, setb1200);
}

void
i8250setmouseputc(char* which, int (*putc)(Queue*, int))
{
	char *p;
	int port;

	port = strtol(which, &p, 0);
	if(p == which || port < 0 || port > 1)
		error(Ebadarg);
	uartsetmouseputc(&i8250uart[port], putc);
}
