enum {					/* I/O ports */
	Rbr		= 0,		/* Receiver Buffer (RO) */
	Thr		= 0,		/* Transmitter Holding (WO) */
	Ier		= 1,		/* Interrupt Enable */
	Iir		= 2,		/* Interrupt Identification (RO) */
	Fcr		= 2,		/* FIFO Control (WO) */
	Lcr		= 3,		/* Line Control */
	Mcr		= 4,		/* Modem Control */
	Lsr		= 5,		/* Line Status */
	Msr		= 6,		/* Modem Status */
	Scr		= 7,		/* Scratch Pad */
	Dll		= 0,		/* Divisor Latch LSB */
	Dlm		= 1,		/* Divisor Latch MSB */
};

enum {					/* Ier */
	Erda		= 0x01,		/* Enable Received Data Available */
	Ethre		= 0x02,		/* Enable Thr Empty */
	Erls		= 0x04,		/* Enable Receiver Line Status */
	Ems		= 0x08,		/* Enable Modem Status */
};

enum {					/* Iir */
	Ims		= 0x00,		/* Ms interrupt */
	Ip		= 0x01,		/* Interrupt Pending (not) */
	Ithre		= 0x02,		/* Thr Empty */
	Irda		= 0x04,		/* Received Data Available */
	Irls		= 0x06,		/* Receiver Line Status */
	Ictoi		= 0x0C,		/* Character Time-out Indication */
	IirMASK		= 0x3F,
	Ifena		= 0xC0,		/* FIFOs enabled */
};

enum {					/* Fcr */
	FIFOena		= 0x01,		/* FIFO enable */
	FIFOrclr	= 0x02,		/* clear Rx FIFO */
	FIFOtclr	= 0x04,		/* clear Tx FIFO */
	FIFO1		= 0x00,		/* Rx FIFO trigger level 1 byte */
	FIFO4		= 0x40,		/*	4 bytes */
	FIFO8		= 0x80,		/*	8 bytes */
	FIFO14		= 0xC0,		/*	14 bytes */
};

enum {					/* Lcr */
	Wls5		= 0x00,		/* Word Length Select 5 bits/byte */
	Wls6		= 0x01,		/*	6 bits/byte */
	Wls7		= 0x02,		/*	7 bits/byte */
	Wls8		= 0x03,		/*	8 bits/byte */
	WlsMASK		= 0x03,
	Stb		= 0x04,		/* 2 stop bits */
	Pen		= 0x08,		/* Parity Enable */
	Eps		= 0x10,		/* Even Parity Select */
	Stp		= 0x20,		/* Stick Parity */
	Brk		= 0x40,		/* Break */
	Dlab		= 0x80,		/* Divisor Latch Access Bit */
};

enum {					/* Mcr */
	Dtr		= 0x01,		/* Data Terminal Ready */
	Rts		= 0x02,		/* Ready To Send */
	Out1		= 0x04,		/* no longer in use */
	Ie		= 0x08,		/* IRQ Enable */
	Dm		= 0x10,		/* Diagnostic Mode loopback */
};

enum {					/* Lsr */
	Dr		= 0x01,		/* Data Ready */
	Oe		= 0x02,		/* Overrun Error */
	Pe		= 0x04,		/* Parity Error */
	Fe		= 0x08,		/* Framing Error */
	Bi		= 0x10,		/* Break Interrupt */
	Thre		= 0x20,		/* Thr Empty */
	Temt		= 0x40,		/* Tramsmitter Empty */
	FIFOerr		= 0x80,		/* error in receiver FIFO */
};

enum {					/* Msr */
	Dcts		= 0x01,		/* Delta Cts */
	Ddsr		= 0x02,		/* Delta Dsr */
	Teri		= 0x04,		/* Trailing Edge of Ri */
	Ddcd		= 0x08,		/* Delta Dcd */
	Cts		= 0x10,		/* Clear To Send */
	Dsr		= 0x20,		/* Data Set Ready */
	Ri		= 0x40,		/* Ring Indicator */
	Dcd		= 0x80,		/* Data Set Ready */
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	void	*reg;
	uint	(*get)(void*, int);
	void	(*set)(void*, int, uint);
	int	(*itr)(Uart*, int);

	int	irq;
	int	tbdf;
	int	iena;

	uchar	sticky[8];

	Lock;
	int	hasfifo;
	int	checkfifo;
	int	fena;
};

extern	PhysUart		p8250physuart;

void	i8250interrupt(Ureg*, void*);
