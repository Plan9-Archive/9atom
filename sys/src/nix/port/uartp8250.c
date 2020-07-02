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

static Uart *i8250uarts;

static uint
csr8r(Ctlr *c, uint r)
{
	return c->get(c->reg, r);
}

static void
csr8w(Ctlr *c, int r, uint v)
{
	c->set(c->reg, r, c->sticky[r]|v);
}

static void
putb(Ctlr *c, int r, uint v)
{
	c->set(c->reg, r, v);
}

static long
i8250status(Uart* uart, void* buf, long n, long offset)
{
	char *p;
	Ctlr *ctlr;
	uchar ier, lcr, mcr, msr;

	ctlr = uart->regs;
	p = malloc(READSTR);
	mcr = ctlr->sticky[Mcr];
	msr = csr8r(ctlr, Msr);
	ier = ctlr->sticky[Ier];
	lcr = ctlr->sticky[Lcr];
	snprint(p, READSTR,
		"b%d c%d d%d e%d l%d m%d p%c r%d s%d i%d\n"
		"dev(%d%s) framing(%d) overruns(%d) "
		"berr(%d) serr(%d)%s%s%s%s\n",

		uart->baud,
		uart->hup_dcd, 
		(msr & Dsr) != 0,
		uart->hup_dsr,
		(lcr & WlsMASK) + 5,
		(ier & Ems) != 0, 
		(lcr & Pen) ? ((lcr & Eps) ? 'e': 'o'): 'n',
		(mcr & Rts) != 0,
		(lcr & Stb) ? 2: 1,
		ctlr->fena,

		uart->dev,
		uart->console? " con": "",
		uart->ferr,
		uart->oerr,
		uart->berr,
		uart->serr,
		(msr & Cts) ? " cts": "",
		(msr & Dsr) ? " dsr": "",
		(msr & Dcd) ? " dcd": "",
		(msr & Ri) ? " ring": ""
	);
	n = readstr(offset, buf, n, p);
	free(p);

	return n;
}

static void
i8250fifo(Uart* uart, int level)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	if(ctlr->hasfifo == 0)
		return;

	/*
	 * Changing the FIFOena bit in Fcr flushes data
	 * from both receive and transmit FIFOs; there's
	 * no easy way to guarantee not losing data on
	 * the receive side, but it's possible to wait until
	 * the transmitter is really empty.
	 */
	ilock(ctlr);
	while(!(csr8r(ctlr, Lsr) & Temt))
		;

	/*
	 * Set the trigger level, default is the max.
	 * value.
	 * Some UARTs require FIFOena to be set before
	 * other bits can take effect, so set it twice.
	 */
	ctlr->fena = level;
	switch(level){
	case 0:
		break;
	case 1:
		level = FIFO1|FIFOena;
		break;
	case 4:
		level = FIFO4|FIFOena;
		break;
	case 8:
		level = FIFO8|FIFOena;
		break;
	default:
		level = FIFO14|FIFOena;
		break;
	}
	csr8w(ctlr, Fcr, level);
	csr8w(ctlr, Fcr, level);
	iunlock(ctlr);
}

static void
i8250dtr(Uart* uart, int on)
{
	Ctlr *ctlr;

	/*
	 * Toggle DTR.
	 */
	ctlr = uart->regs;
	if(on)
		ctlr->sticky[Mcr] |= Dtr;
	else
		ctlr->sticky[Mcr] &= ~Dtr;
	csr8w(ctlr, Mcr, 0);
}

static void
i8250rts(Uart* uart, int on)
{
	Ctlr *ctlr;

	/*
	 * Toggle RTS.
	 */
	ctlr = uart->regs;
	if(on)
		ctlr->sticky[Mcr] |= Rts;
	else
		ctlr->sticky[Mcr] &= ~Rts;
	csr8w(ctlr, Mcr, 0);
}

static void
i8250modemctl(Uart* uart, int on)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	ilock(&uart->tlock);
	if(on){
		ctlr->sticky[Ier] |= Ems;
		csr8w(ctlr, Ier, ctlr->sticky[Ier]);
		uart->modem = 1;
		uart->cts = csr8r(ctlr, Msr) & Cts;
	}
	else{
		ctlr->sticky[Ier] &= ~Ems;
		csr8w(ctlr, Ier, ctlr->sticky[Ier]);
		uart->modem = 0;
		uart->cts = 1;
	}
	iunlock(&uart->tlock);

	/* modem needs fifo */
	(*uart->phys->fifo)(uart, on);
}

static int
i8250parity(Uart* uart, int parity)
{
	int lcr;
	Ctlr *ctlr;

	ctlr = uart->regs;
	lcr = ctlr->sticky[Lcr] & ~(Eps|Pen);

	switch(parity){
	case 'e':
		lcr |= Eps|Pen;
		break;
	case 'o':
		lcr |= Pen;
		break;
	case 'n':
		break;
	default:
		return -1;
	}
	ctlr->sticky[Lcr] = lcr;
	csr8w(ctlr, Lcr, 0);

	uart->parity = parity;

	return 0;
}

static int
i8250stop(Uart* uart, int stop)
{
	int lcr;
	Ctlr *ctlr;

	ctlr = uart->regs;
	lcr = ctlr->sticky[Lcr] & ~Stb;

	switch(stop){
	case 1:
		break;
	case 2:
		lcr |= Stb;
		break;
	default:
		return -1;
	}
	ctlr->sticky[Lcr] = lcr;
	csr8w(ctlr, Lcr, 0);

	uart->stop = stop;

	return 0;
}

static int
i8250bits(Uart* uart, int bits)
{
	int lcr;
	Ctlr *ctlr;

	ctlr = uart->regs;
	lcr = ctlr->sticky[Lcr] & ~WlsMASK;

	switch(bits){
	case 5:
		lcr |= Wls5;
		break;
	case 6:
		lcr |= Wls6;
		break;
	case 7:
		lcr |= Wls7;
		break;
	case 8:
		lcr |= Wls8;
		break;
	default:
		return -1;
	}
	ctlr->sticky[Lcr] = lcr;
	csr8w(ctlr, Lcr, 0);

	uart->bits = bits;
	return 0;
}

static int
i8250baud(Uart* uart, int baud)
{
	uint bgc;
	Ctlr *ctlr;

	uart->baud = baud;
	/*
	 * Set the Baud rate by calculating and setting the Baud rate
	 * Generator Constant. This will work with fairly non-standard
	 * Baud rates.
	 */
	if(uart->freq == 0 || baud <= 0)
		return -1;
	bgc = (uart->freq+8*baud-1)/(16*baud);

	ctlr = uart->regs;
	csr8w(ctlr, Lcr, Dlab);
	putb(ctlr, Dlm, bgc>>8);
	putb(ctlr, Dll, bgc);
	csr8w(ctlr, Lcr, 0);

	uart->baud = baud;

	return 0;
}

static void
i8250break(Uart* uart, int ms)
{
	Ctlr *ctlr;

	/*
	 * Send a break.
	 */
	if(ms <= 0)
		ms = 200;

	ctlr = uart->regs;
	csr8w(ctlr, Lcr, Brk);
	tsleep(&up->sleep, return0, 0, ms);
	csr8w(ctlr, Lcr, 0);
}

static void
i8250kick(Uart* uart)
{
	int i;
	Ctlr *ctlr;

	if(uart->cts == 0 || uart->blocked)
		return;

	/*
	 *  128 here is an arbitrary limit to make sure
	 *  we don't stay in this loop too long.  If the
	 *  chip's output queue is longer than 128, too
	 *  bad -- presotto
	 */
	ctlr = uart->regs;
	for(i = 0; i < 128; i++){
		if(!(csr8r(ctlr, Lsr) & Thre))
			break;
		if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
			break;
		putb(ctlr, Thr, *(uart->op++));
	}
}

void
i8250interrupt(Ureg*, void *v)
{
	int iir, lsr, old, r;
	Ctlr *ctlr;
	Uart *uart;

	uart = v;
	ctlr = uart->regs;

	for(iir = csr8r(ctlr, Iir); !(iir & Ip); iir = csr8r(ctlr, Iir)){
		switch(iir & IirMASK){
		case Ims:		/* Ms interrupt */
			r = csr8r(ctlr, Msr);
			if(r & Dcts){
				ilock(&uart->tlock);
				old = uart->cts;
				uart->cts = r & Cts;
				if(old == 0 && uart->cts)
					uart->ctsbackoff = 2;
				iunlock(&uart->tlock);
			}
		 	if(r & Ddsr){
				old = r & Dsr;
				if(uart->hup_dsr && uart->dsr && !old)
					uart->dohup = 1;
				uart->dsr = old;
			}
		 	if(r & Ddcd){
				old = r & Dcd;
				if(uart->hup_dcd && uart->dcd && !old)
					uart->dohup = 1;
				uart->dcd = old;
			}
			break;
		case Ithre:		/* Thr Empty */
			uartkick(uart);
			break;
		case Irda:		/* Received Data Available */
		case Irls:		/* Receiver Line Status */
		case Ictoi:		/* Character Time-out Indication */
			/*
			 * Consume any received data.
			 * If the received byte came in with a break,
			 * parity or framing error, throw it away;
			 * overrun is an indication that something has
			 * already been tossed.
			 */
			while((lsr = csr8r(ctlr, Lsr)) & Dr){
				if(lsr & (FIFOerr|Oe))
					uart->oerr++;
				if(lsr & Pe)
					uart->perr++;
				if(lsr & Fe)
					uart->ferr++;
				r = csr8r(ctlr, Rbr);
				if(!(lsr & (Bi|Fe|Pe)))
					uartrecv(uart, r);
			}
			break;

		default:
			iprint("weird uart interrupt %#.2ux\n", iir);
			break;
		}
	}
}

static void
i8250disable(Uart* uart)
{
	Ctlr *ctlr;

	/*
 	 * Turn off DTR and RTS, disable interrupts and fifos.
	 */
	(*uart->phys->dtr)(uart, 0);
	(*uart->phys->rts)(uart, 0);
	(*uart->phys->fifo)(uart, 0);

	ctlr = uart->regs;
	ctlr->sticky[Ier] = 0;
	csr8w(ctlr, Ier, ctlr->sticky[Ier]);

	if(ctlr->iena != 0){
		if(ctlr->itr(uart, 0) == 0)
			ctlr->iena = 0;
	}
}

static void
i8250enable(Uart* uart, int ie)
{
	int i;
	Ctlr *ctlr;

	ctlr = uart->regs;

	/*
	 * Check if there is a FIFO.
	 * Changing the FIFOena bit in Fcr flushes data
	 * from both receive and transmit FIFOs; there's
	 * no easy way to guarantee not losing data on
	 * the receive side, but it's possible to wait until
	 * the transmitter is really empty.
	 * Also, reading the Iir without i8250interrupt()
	 * can be dangerous, but this should only happen
	 * once, before interrupts are enabled.
	 */
	ilock(ctlr);
	if(!ctlr->checkfifo){
		/*
		 * Wait until the transmitter is really empty.
		 */
		for(i = 5000; i>0; i--)
			if(csr8r(ctlr, Lsr) & Temt)
				break;
		csr8w(ctlr, Fcr, FIFOena);
		if(csr8r(ctlr, Iir) & Ifena)
			ctlr->hasfifo = 1;
		csr8w(ctlr, Fcr, 0);
		ctlr->checkfifo = 1;
	}
	iunlock(ctlr);

	/*
 	 * Enable interrupts and turn on DTR and RTS.
	 * Be careful if this is called to set up a polled serial line
	 * early on not to try to enable interrupts as interrupt-
	 * -enabling mechanisms might not be set up yet.
	 */
	if(ie){
		if(ctlr->iena == 0){
			ctlr->itr(uart, 1);
			ctlr->iena = 1;
		}
		ctlr->sticky[Ier] = Ethre|Erda;
		ctlr->sticky[Mcr] |= Ie;
	}
	else{
		ctlr->sticky[Ier] = 0;
		ctlr->sticky[Mcr] = 0;
	}
	csr8w(ctlr, Ier, ctlr->sticky[Ier]);
	csr8w(ctlr, Mcr, ctlr->sticky[Mcr]);

	(*uart->phys->dtr)(uart, 1);
	(*uart->phys->rts)(uart, 1);

	/*
	 * During startup, the i8259 interrupt controller is reset.
	 * This may result in a lost interrupt from the i8250 uart.
	 * The i8250 thinks the interrupt is still outstanding and does not
	 * generate any further interrupts. The workaround is to call the
	 * interrupt handler to clear any pending interrupt events.
	 * Note: this must be done after setting Ier.
	 */
	if(ie)
		i8250interrupt(nil, uart);
}

Uart*
i8250pnp(void)
{
	return i8250uarts;
}

static int
i8250getc(Uart *uart)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	while(!(csr8r(ctlr, Lsr)&Dr))
		microdelay(1);
	return csr8r(ctlr, Rbr);
}

static void
i8250putc(Uart *uart, int c)
{
	int i;
	Ctlr *ctlr;

	ctlr = uart->regs;
	for(i = 0; !(csr8r(ctlr, Lsr)&Thre) && i < 128000; i++)
		microdelay(1);
	putb(ctlr, Thr, c);
	for(i = 0; !(csr8r(ctlr, Lsr)&Thre) && i < 128000; i++)
		microdelay(1);
}

static void
i8250poll(Uart* uart)
{
	Ctlr *ctlr;

	/*
	 * If PhysUart has a non-nil .poll member, this
	 * routine will be called from the uartclock timer.
	 * If the Ctlr .poll member is non-zero, when the
	 * Uart is enabled interrupts will not be enabled
	 * and the result is polled input and output.
	 * Not very useful here, but ports to new hardware
	 * or simulators can use this to get serial I/O
	 * without setting up the interrupt mechanism.
	 */
	ctlr = uart->regs;
	if(ctlr->iena || !ctlr->poll)
		return;
	i8250interrupt(nil, uart);
}

PhysUart p8250physuart = {
	.name		= "p8250",
	.pnp		= i8250pnp,
	.enable		= i8250enable,
	.disable	= i8250disable,
	.kick		= i8250kick,
	.dobreak	= i8250break,
	.baud		= i8250baud,
	.bits		= i8250bits,
	.stop		= i8250stop,
	.parity		= i8250parity,
	.modemctl	= i8250modemctl,
	.rts		= i8250rts,
	.dtr		= i8250dtr,
	.status		= i8250status,
	.fifo		= i8250fifo,
	.getc		= i8250getc,
	.putc		= i8250putc,
	.poll		= i8250poll,
};
