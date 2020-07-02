/*
 * Oxford Semiconductor OXPCIe958 UART driver.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/uartp8250.h"

extern PhysUart ox958physuart;

static uint
ox958get(void *reg, int r)
{
	uchar *p;

	p = reg;
	return p[r];
}

static void
ox958set(void *reg, int r, uint v)
{
	uchar *p;

	p = reg;
	p[r] = v;
}

static int
itr(Uart *u, int on)
{
	Ctlr *c;

	c = u->regs;
	if(on)
		intrenable(c->irq, i8250interrupt, u, c->tbdf, u->name);
	else{
		/* this causes hangs. please debug. */
//		return intrdisable(c->irq, i8250interrupt, u, c->tbdf, u->name);
		return -1;
	}
	return 0;
}

static Ctlr*
ox958alloc(void *io, Pcidev *p)
{
	Ctlr *ctlr;

	if((ctlr = malloc(sizeof *ctlr)) == nil)
		return nil;
	ctlr->reg = io;
	ctlr->irq = p->intl;
	ctlr->tbdf = p->tbdf;
	ctlr->get = ox958get;
	ctlr->set = ox958set;
	ctlr->itr = itr;

	return ctlr;
}

static Uart*
ox958pnp(void)
{
	char buf[64];
	uchar *mem;
	int i, n;
	Ctlr *ctlr;
	Pcidev *p;
	Uart **xx, *head, *uart;

	memmove(&ox958physuart, &p8250physuart, sizeof(PhysUart));
	ox958physuart.name = "OXPCIe958";
	ox958physuart.pnp = ox958pnp;

	head = nil;
	xx = &head;
	for(p = nil; p = pcimatch(p, 0x1415, 0xc308);){
		mem = vmap(p->mem[0].bar & ~1, p->mem[0].size);
		if (mem == nil) {
			print("uartox958: %T: can't vmap\n", p->tbdf);
			continue;
		}
		n = mem[4] & 0x1f;
		print("%s: %d ports\n", ox958physuart.name, n);
		mem += 0x1000;
		for (i = 0; i < n; ++i) {
			ctlr = ox958alloc(mem, p);
			mem += 0x200;
			if (ctlr == nil) {
				print("uartox958: %T: can't i8250alloc %d\n", p->tbdf, n);
				continue;
			}

			uart = malloc(sizeof *uart);
			if (uart == nil) {
				print("uartox958: %T: uart %d: can't malloc\n", p->tbdf, n);
				free(ctlr);
				continue;
			}
			uart->regs = ctlr;

			snprint(buf, sizeof buf, "%s.%.8ux", "OXPCIe958", p->tbdf);
			kstrdup(&uart->name, buf);
			uart->freq = 62284801;
			uart->phys = &ox958physuart;
			*xx = uart;
			xx = &uart->next;
		}
	}
	return head;
}

/* hook used only at pnp time */
PhysUart ox958physuart = {
	.name		= "OXPCIe958",
	.pnp		= ox958pnp,
	.enable		= nil,
	.disable	= nil,
	.kick		= nil,
	.dobreak	= nil,
	.baud		= nil,
	.bits		= nil,
	.stop		= nil,
	.parity		= nil,
	.modemctl	= nil,
	.rts		= nil,
	.dtr		= nil,
	.status		= nil,
	.fifo		= nil,
};
