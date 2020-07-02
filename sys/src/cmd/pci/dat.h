typedef	struct	Pcidev	Pcidev;
typedef	struct	Bar	Bar;

enum {
	ApicFIXED	= 0x00000000,	/* [10:8] Delivery Mode */
	ApicLOWEST	= 0x00000100,	/* Lowest priority */
	ApicSMI		= 0x00000200,	/* System Management Interrupt */
	ApicRR		= 0x00000300,	/* Remote Read */
	ApicNMI		= 0x00000400,
	ApicINIT	= 0x00000500,	/* INIT/RESET */
	ApicSTARTUP	= 0x00000600,	/* Startup IPI */
	ApicExtINT	= 0x00000700,

	ApicPHYSICAL	= 0x00000000,	/* [11] Destination Mode (RW) */
	ApicLOGICAL	= 0x00000800,

	ApicDELIVS	= 0x00001000,	/* [12] Delivery Status (RO) */
	ApicHIGH	= 0x00000000,	/* [13] Interrupt Input Pin Polarity (RW) */
	ApicLOW		= 0x00002000,
	ApicRemoteIRR	= 0x00004000,	/* [14] Remote IRR (RO) */
	ApicEDGE	= 0x00000000,	/* [15] Trigger Mode (RW) */
	ApicLEVEL	= 0x00008000,
	ApicIMASK	= 0x00010000,	/* [16] Interrupt Mask */
};

enum {
	BUSUNKNOWN		= -1,

	BusCBUS		= 0,		/* Corollary CBUS */
	BusCBUSII,			/* Corollary CBUS II */
	BusEISA,			/* Extended ISA */
	BusFUTURE,			/* IEEE Futurebus */
	BusINTERN,			/* Internal bus */
	BusISA,				/* Industry Standard Architecture */
	BusMBI,				/* Multibus I */
	BusMBII,			/* Multibus II */
	BusMCA,				/* Micro Channel Architecture */
	BusMPI,				/* MPI */
	BusMPSA,			/* MPSA */
	BusNUBUS,			/* Apple Macintosh NuBus */
	BusPCI,				/* Peripheral Component Interconnect */
	BusPCMCIA,			/* PC Memory Card International Association */
	BusTC,				/* DEC TurboChannel */
	BusVL,				/* VESA Local bus */
	BusVME,				/* VMEbus */
	BusXPRESS,			/* Express System Bus */
};

struct Bar {
	uintptr	bar;
	uint	size;
};

struct Pcidev {
	Pcidev	*next;
	uint	tbdf;
	ushort	vid;
	ushort	did;
	ushort	pcr;
	ushort	ccrb;
	ushort	ccru;
	ushort	ccrp;
	ushort	svid;
	ushort	sdid;
	uint	intl;
	Bar	mem[6];
	uchar	cfg[4096];
	int	ncfg;
};

#define	BUSTYPE(tbdf)	((tbdf)>>24)
#define	BUSBNO(tbdf)	(((tbdf)>>16)&0xFF)
#define	BUSDNO(tbdf)	(((tbdf)>>11)&0x1F)
#define	BUSFNO(tbdf)	(((tbdf)>>8)&0x07)

enum {
	Dpcicap		= 1<<0,
	Dmsicap		= 1<<1,
	Dvec		= 1<<2,
	Dpciwrite	= 1<<3,
	Debug		= Dvec | Dpciwrite,
};

enum {
	/* address */
	Msiabase		= 0xfee00000,
	Msiadest		= 1<<12,		/* same as 63:56 of apic vector */
	Msiaedest	= 1<<4,		/* same as 55:48 of apic vector */
	Msialowpri	= 1<<3,		/* redirection hint */
	Msialogical	= 1<<2,

	/* data */
	Msidlevel	= 1<<15,
	Msidassert	= 1<<14,
	Msidlogical	= 1<<11,
	Msidmode	= 1<<8,		/* 3 bits; delivery mode */
	Msidvector	= 0xff<<0,
};

enum {
	PciCapPMG	= 0x01,		/* power management */
	PciCapAGP	= 0x02,
	PciCapVPD	= 0x03,		/* vital product data */
	PciCapSID	= 0x04,		/* slot id */
	PciCapMSI	= 0x05,
	PciCapCHS	= 0x06,		/* compact pci hot swap */
	PciCapPCIX	= 0x07,
	PciCapHTC	= 0x08,		/* hypertransport irq conf */
	PciCapVND	= 0x09,		/* vendor specific information */
	PciCapPCIe	= 0x10,
	PciCapMSIx	= 0x11,
	PciCapSATA	= 0x12,
	PciCapHSW	= 0x0C,		/* hot swap */
};

enum {					/* type 0 & type 1 pre-defined header */
	PciVID		= 0x00,		/* vendor ID */
	PciDID		= 0x02,		/* device ID */
	PciPCR		= 0x04,		/* command */
	PciPSR		= 0x06,		/* status */
	PciRID		= 0x08,		/* revision ID */
	PciCCRp		= 0x09,		/* programming interface class code */
	PciCCRu		= 0x0A,		/* sub-class code */
	PciCCRb		= 0x0B,		/* base class code */
	PciCLS		= 0x0C,		/* cache line size */
	PciLTR		= 0x0D,		/* latency timer */
	PciHDT		= 0x0E,		/* header type */
	PciBST		= 0x0F,		/* BIST */

	PciBAR0		= 0x10,		/* base address */
	PciBAR1		= 0x14,

	PciINTL		= 0x3C,		/* interrupt line */
	PciINTP		= 0x3D,		/* interrupt pin */
};

enum {					/* type 0 pre-defined header */
	PciCIS		= 0x28,		/* cardbus CIS pointer */
	PciSVID		= 0x2C,		/* subsystem vendor ID */
	PciSID		= 0x2E,		/* cardbus CIS pointer */
	PciEBAR0	= 0x30,		/* expansion ROM base address */
	PciMGNT		= 0x3E,		/* burst period length */
	PciMLT		= 0x3F,		/* maximum latency between bursts */
};

enum {					/* type 1 pre-defined header */
	PciPBN		= 0x18,		/* primary bus number */
	PciSBN		= 0x19,		/* secondary bus number */
	PciUBN		= 0x1A,		/* subordinate bus number */
	PciSLTR		= 0x1B,		/* secondary latency timer */
	PciIBR		= 0x1C,		/* I/O base */
	PciILR		= 0x1D,		/* I/O limit */
	PciSPSR		= 0x1E,		/* secondary status */
	PciMBR		= 0x20,		/* memory base */
	PciMLR		= 0x22,		/* memory limit */
	PciPMBR		= 0x24,		/* prefetchable memory base */
	PciPMLR		= 0x26,		/* prefetchable memory limit */
	PciPUBR		= 0x28,		/* prefetchable base upper 32 bits */
	PciPULR		= 0x2C,		/* prefetchable limit upper 32 bits */
	PciIUBR		= 0x30,		/* I/O base upper 16 bits */
	PciIULR		= 0x32,		/* I/O limit upper 16 bits */
	PciEBAR1	= 0x28,		/* expansion ROM base address */
	PciBCR		= 0x3E,		/* bridge control register */
};

enum {					/* type 2 pre-defined header */
	PciCBExCA	= 0x10,
	PciCBSPSR	= 0x16,
	PciCBPBN	= 0x18,		/* primary bus number */
	PciCBSBN	= 0x19,		/* secondary bus number */
	PciCBUBN	= 0x1A,		/* subordinate bus number */
	PciCBSLTR	= 0x1B,		/* secondary latency timer */
	PciCBMBR0	= 0x1C,
	PciCBMLR0	= 0x20,
	PciCBMBR1	= 0x24,
	PciCBMLR1	= 0x28,
	PciCBIBR0	= 0x2C,		/* I/O base */
	PciCBILR0	= 0x30,		/* I/O limit */
	PciCBIBR1	= 0x34,		/* I/O base */
	PciCBILR1	= 0x38,		/* I/O limit */
	PciCBSVID	= 0x40,		/* subsystem vendor ID */
	PciCBSID	= 0x42,		/* subsystem ID */
	PciCBLMBAR	= 0x44,		/* legacy mode base address */
};

enum
{					/* command register */
	IOen		= (1<<0),
	MEMen		= (1<<1),
	MASen		= (1<<2),
	MemWrInv	= (1<<4),
	PErrEn		= (1<<6),
	SErrEn		= (1<<8),
};

enum {
	PciePCP		= 0x02,		/* pcie capabilities */
	PcieCAP		= 0x04,		/* device capabilities */
	PcieDCR	= 0x08,		/* device control */
	PcieDSR		= 0x0a,		/* device status */
	PcieLCA		= 0x0c,		/* link capabilties */
	PcieLCR		= 0x10,		/* link control */
	PcieLSR		= 0x12,		/* link status */
	PcieSCA		= 0x14,		/* slot capabilties */
	PcieSCR		= 0x18,		/* slot control */
	PcieSSR		= 0x1a,		/* slot status */
};

enum{
	/* msi capabilities */
	Vmask		= 1<<8,
	Cap64		= 1<<7,
	Mmesgmsk	= 7<<4,
	Msienable	= 1<<0,
};

#pragma varargck	type	"T"	int
#pragma varargck	type	"T"	uint

uint	getn(Pcidev*, uint, uint);
uint	pcicfgr32(Pcidev*, uint);
uint	pcicfgr16(Pcidev*, uint);
uint	pcicfgr8(Pcidev *, uint);

void	putn(Pcidev*, uint, uint, uint);
void	pcicfgw32(Pcidev*, uint, uint);
void	pcicfgw16(Pcidev*, uint, uint);
void	pcicfgw8(Pcidev*, uint, uint);

Pcidev*	pcimatch(Pcidev*, int, int);
Pcidev*	pcimatchtbdf(int);

void	pcisetbme(Pcidev*);
Pcidev*	getpcidev(char*, int);
uint	strtotbdf(char*);
int	pcicap(Pcidev*, int, int);

void	pciinit(void);

/* cap.c */
void	prcap(Pcidev*, int);

#define pcicapdbg(...)	do if(Debug&Dpcicap)print(__VA_ARGS__);while(0)
#define dprint(c, ...)	do if(Debug&c)print(__VA_ARGS__);while(0)

