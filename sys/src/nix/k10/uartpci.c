#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/uartp8250.h"

extern	PhysUart	pciphysuart;

static	Uart	*pciuarthead;
static	Uart	*pciuarttail;
static	int	npciuart;

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
	else{
		/* this causes hangs. please debug. */
//		return intrdisable(c->irq, i8250interrupt, u, c->tbdf, u->name);
		return -1;
	}
	return 0;
}

static Ctlr*
pciuartalloc(void *io, uint intl, uint tbdf)
{
	Ctlr *ctlr;

	if((ctlr = malloc(sizeof *ctlr)) == nil)
		return nil;
	ctlr->reg = io;
	ctlr->irq = intl;
	ctlr->tbdf = tbdf;
	ctlr->get = ioget;
	ctlr->set = ioset;
	ctlr->itr = itr;

	return ctlr;
}

void
uartpci(Pcidev* p, int barno, int n, int freq, char* name, int iosize)
{
	int i;
	uintptr io;
	void *ctlr;
	char buf[64];
	Uart *uart;

	io = p->mem[barno].bar & ~0x01;
	snprint(buf, sizeof(buf), "%s%d", pciphysuart.name, npciuart);
	if(ioalloc(io, p->mem[barno].size, 0, buf) < 0){
		print("uartpci: I/O %#p in use\n", io);
		return;
	}

	uart = malloc(sizeof *uart*n);
	for(i = 0; i < n; i++){
		ctlr = pciuartalloc((void*)io, p->intl, p->tbdf);
		io += iosize;
		if(ctlr == nil)
			continue;

		uart->regs = ctlr;
		snprint(buf, sizeof(buf), "%s.%.8ux", name, p->tbdf);
		kstrdup(&uart->name, buf);
		uart->freq = freq;
		uart->phys = &pciphysuart;
		if(pciuarthead != nil)
			pciuarttail->next = uart;
		else
			pciuarthead = uart;
		pciuarttail = uart;
		uart++;
		npciuart++;
	}
}

static void
ultraport16si(Pcidev *p, ulong freq)
{
	int io, i;
	char *name;

	name = "Ultraport16si";		/* 16L788 UARTs */
	io = p->mem[4].bar & ~1;
	if (ioalloc(io, p->mem[4].size, 0, name) < 0) {
		print("uartpci: can't get IO space to set %s to rs-232\n", name);
		return;
	}
	for (i = 0; i < 16; i++) {
		outb(io, i << 4);
		outb(io, (i << 4) + 1);	/* set to RS232 mode  (Don't ask!) */
	}

	uartpci(p, 2, 8, freq, name, 16);
	uartpci(p, 3, 8, freq, name, 16);
}

static Uart*
uartpcipnp(void)
{
	char *name;
	int subid;
	ulong freq;
	Pcidev *p;

	memmove(&pciphysuart, &p8250physuart, sizeof(PhysUart));
	pciphysuart.name = "UartPCI";
	pciphysuart.pnp = uartpcipnp;

	/*
	 * Loop through all PCI devices looking for simple serial
	 * controllers (ccrb == Pcibccomm (7)) and configure the ones which
	 * are familiar. 
	 */
	for(p = nil; p = pcimatch(p, 0, 0);){
		if(p->ccrb != 0x07 || p->ccru > 2)
			continue;

		switch(p->did<<16 | p->vid){
		default:
			continue;
		case 0x9835<<16|0x9710:		/* StarTech PCI2S550 */
			name = "PCI2S550";
			uartpci(p, 0, 1, 1843200, name, 8);
			uartpci(p, 1, 1, 1843200, name, 8);
			break;
		case 0x950A<<16|0x1415:		/* Oxford Semi OX16PCI954 */
		case 0x9501<<16|0x1415:
		case 0x9521<<16|0x1415:
			/*
			 * These are common devices used by 3rd-party
			 * manufacturers.
			 * Must check the subsystem VID and DID for correct
			 * match.
			 */
			subid = pcicfgr16(p, PciSID)<<16 | pcicfgr16(p, PciSVID);
			switch(subid){
			default:
				print("uartpci: %T: ox954 unknown: %.8ux\n", p->tbdf, subid);
				continue;
			case 0<<16|0x1415:
				uartpci(p, 0, 4, 1843200, "starport-pex4s", 8);
				break;
			case 1<<16|0x1415:
				uartpci(p, 0, 2, 14745600, "starport-pex2s", 8);
				break;
			case 0x2000<<16|0x131F:	/* SIIG CyberSerial PCIe */
				uartpci(p, 0, 1, 18432000, "CyberSerial-1S", 8);
				break;
			}
			break;
		case 0x9505<<16|0x1415:		/* Oxford Semi OXuPCI952 */
			name = "SATAGear-IOI-102";  /* PciSVID=1415, PciSID=0 */
			uartpci(p, 0, 1, 14745600, name, 8);
			uartpci(p, 1, 1, 14745600, name, 8);
			break;
		case 0x9050<<16|0x10B5:		/* Perle PCI-Fast4 series */
		case 0x9030<<16|0x10B5:		/* Perle Ultraport series */
			/*
			 * These devices consists of a PLX bridge (the above
			 * PCI VID+DID) behind which are some 16C654 UARTs.
			 * Must check the subsystem VID and DID for correct
			 * match.
			 */
			freq = 7372800;
			subid = pcicfgr16(p, PciSID)<<16 | pcicfgr16(p, PciSVID);
			switch(subid){
			default:
				print("uartpci: %T: perle unknown: %.8ux\n", p->tbdf, subid);
				continue;
			case 0x0011<<16|0x12E0:		/* Perle PCI-Fast16 */
				uartpci(p, 2, 16, freq, "PCI-Fast16", 8);
				break;
			case 0x0021<<16|0x12E0:		/* Perle PCI-Fast8 */
				uartpci(p, 2, 8, freq, "PCI-Fast8", 8);
				break;
			case 0x0031<<16|0x12E0:		/* Perle PCI-Fast4 */
				uartpci(p, 2, 4, freq, "PCI-Fast4", 8);
				break;
			case 0x0021<<16|0x155F:		/* Perle Ultraport8 */
				uartpci(p, 2, 8, freq, "Ultraport8", 8);
				break;
			case 0x0041<<16|0x155F:		/* Perle Ultraport16 */
				uartpci(p, 2, 16, 2 * freq, "Ultraport16", 8);
				break;
			case 0x0241<<16|0x155F:		/* Perle Ultraport16 */
				ultraport16si(p, 4 * freq);
				break;
			}
			break;
		}
	}
	return pciuarthead;
}

/* hook used only at pnp time */
PhysUart pciphysuart = {
	.name		= "UartPCI",
	.pnp		= uartpcipnp,
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
