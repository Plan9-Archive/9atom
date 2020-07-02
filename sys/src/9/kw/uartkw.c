#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/uartp8250.h"

#define UART0REG	((uint*)AddrUart0)

extern PhysUart kwphysuart;

static uint
kwget(void *v, int i)
{
	uint *regs;

	regs = v;
	return regs[i];
}

static void
kwset(void *v, int i, uint x)
{
	uint *regs;

	regs = v;
	regs[i] = x;
}

static void
kwirq(Ureg *ur, void *v)
{
	Ctlr *c;
	Uart *u;

	u = v;
	c = u->regs;
	i8250interrupt(ur, u);
	intrclear(Irqhi, c->irq);
}

static int
kwint(Uart *u, int on)
{
	Ctlr *c;

	c = u->regs;
	if(on)
		intrenable(Irqhi, c->irq, kwirq, u, u->name);
	else
		intrdisable(Irqhi, c->irq, kwirq, u, u->name);
	return 0;
}

PhysUart kwphysuart;

static Ctlr kirkwoodctlr[] = {
	.reg	= UART0REG,
	.get	= kwget,
	.set	= kwset,
	.itr	= kwint,
	.irq	= IRQ1uart0,
};

static Uart kirkwooduart[] = {
	.regs	= &kirkwoodctlr[0],
	.name	= "kw console",
	.freq	= CLOCKFREQ,			/* TCLK; may be 166 or 200Mhz */
	.phys	= &kwphysuart,
	.next	= nil,
};

static Uart*
kwpnp(void)
{
	return kirkwooduart;
}

void
uartkirkwoodconsole(void)
{
	memmove(&kwphysuart, &p8250physuart, sizeof(PhysUart));
	kwphysuart.name = "kw console";
	kwphysuart.pnp = kwpnp;

	uartctl(kirkwooduart, "z b115200 l8 pn s1 i1");
}

/* boot crap */
void
wave(int c)
{
	int cnt, s;
	ulong *p;

	s = splhi();
	p = (ulong*)UART0REG;
	cnt = m->cpuhz;
	if (cnt <= 0)			/* cpuhz not set yet? */
		cnt = 1000000;
	coherence();
	while((p[Lsr] & Thre) == 0 && --cnt > 0)
		;
	p[Thr] = c;
	coherence();
	splx(s);
}
