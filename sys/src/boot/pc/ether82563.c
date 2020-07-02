/*
 * bootstrap driver for
 * Intel 8256[367], 8257[1-9], 8258[03], i350
 *	Gigabit Ethernet PCI-Express Controllers
 * Coraid EtherDrive® hba
 */
#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "etherif.h"

/* compatibility with cpu kernels */
#define iallocb allocb

/*
 * note: the 82575, 82576 and 82580 are operated using registers aliased
 * to the 82563-style architecture.  many features seen in the 82598
 * are also seen in the 82575 part.
 */

enum {
	/* General */

	Ctrl		= 0x0000,	/* Device Control */
	Status		= 0x0008,	/* Device Status */
	Eec		= 0x0010,	/* EEPROM/Flash Control/Data */
	Eerd		= 0x0014,	/* EEPROM Read */
	Ctrlext		= 0x0018,	/* Extended Device Control */
	Fla		= 0x001c,	/* Flash Access */
	Mdic		= 0x0020,	/* MDI Control */
	Fcal		= 0x0028,	/* Flow Control Address Low */
	Fcah		= 0x002C,	/* Flow Control Address High */
	Fct		= 0x0030,	/* Flow Control Type */
	Kumctrlsta	= 0x0034,	/* Kumeran Control and Status Register */
	Connsw		= 0x0034,	/* copper / fiber switch control; 82575/82576 */
	Vet		= 0x0038,	/* VLAN EtherType */
	Fcttv		= 0x0170,	/* Flow Control Transmit Timer Value */
	Txcw		= 0x0178,	/* Transmit Configuration Word */
	Rxcw		= 0x0180,	/* Receive Configuration Word */
	Ledctl		= 0x0E00,	/* LED control */
	Pba		= 0x1000,	/* Packet Buffer Allocation */
	Pbs		= 0x1008,	/* Packet Buffer Size */

	/* Interrupt */

	Icr		= 0x00C0,	/* Interrupt Cause Read */
	Itr		= 0x00c4,	/* Interrupt Throttling Rate */
	Ics		= 0x00C8,	/* Interrupt Cause Set */
	Ims		= 0x00D0,	/* Interrupt Mask Set/Read */
	Imc		= 0x00D8,	/* Interrupt mask Clear */
	Iam		= 0x00E0,	/* Interrupt acknowledge Auto Mask */
	Eitr		= 0x1680,	/* Extended itr; 82575/6 80 only */

	/* Receive */

	Rctl		= 0x0100,	/* Control */
	Ert		= 0x2008,	/* Early Receive Threshold (573[EVL], 82578 only) */
	Fcrtl		= 0x2160,	/* Flow Control RX Threshold Low */
	Fcrth		= 0x2168,	/* Flow Control Rx Threshold High */
	Psrctl		= 0x2170,	/* Packet Split Receive Control */
	Drxmxod		= 0x2540,	/* dma max outstanding bytes (82575) */
	Rdbal		= 0x2800,	/* Rdesc Base Address Low Queue 0 */
	Rdbah		= 0x2804,	/* Rdesc Base Address High Queue 0 */
	Rdlen		= 0x2808,	/* Descriptor Length Queue 0 */
	Srrctl		= 0x280c,	/* split and replication rx control (82575) */
	Rdh		= 0x2810,	/* Descriptor Head Queue 0 */
	Rdt		= 0x2818,	/* Descriptor Tail Queue 0 */
	Rdtr		= 0x2820,	/* Descriptor Timer Ring */
	Rxdctl		= 0x2828,	/* Descriptor Control */
	Radv		= 0x282C,	/* Interrupt Absolute Delay Timer */
	Rsrpd		= 0x2c00,	/* Small Packet Detect */
	Raid		= 0x2c08,	/* ACK interrupt delay */
	Cpuvec		= 0x2c10,	/* CPU Vector */
	Rxcsum		= 0x5000,	/* Checksum Control */
	Rmpl		= 0x5004,	/* rx maximum packet length (82575) */
	Rfctl		= 0x5008,	/* Filter Control */
	Mta		= 0x5200,	/* Multicast Table Array */
	Ral		= 0x5400,	/* Receive Address Low */
	Rah		= 0x5404,	/* Receive Address High */
	Vfta		= 0x5600,	/* VLAN Filter Table Array */
	Mrqc		= 0x5818,	/* Multiple Receive Queues Command */

	/* Transmit */

	Tctl		= 0x0400,	/* Transmit Control */
	Tipg		= 0x0410,	/* Transmit IPG */
	Tkabgtxd	= 0x3004,	/* glci afe band gap transmit ref data, or something */
	Tdbal		= 0x3800,	/* Tdesc Base Address Low */
	Tdbah		= 0x3804,	/* Tdesc Base Address High */
	Tdlen		= 0x3808,	/* Descriptor Length */
	Tdh		= 0x3810,	/* Descriptor Head */
	Tdt		= 0x3818,	/* Descriptor Tail */
	Tidv		= 0x3820,	/* Interrupt Delay Value */
	Txdctl		= 0x3828,	/* Descriptor Control */
	Tadv		= 0x382C,	/* Interrupt Absolute Delay Timer */
	Tarc0		= 0x3840,	/* Arbitration Counter Queue 0 */

	/* Statistics */

	Statistics	= 0x4000,	/* Start of Statistics Area */
	Gorcl		= 0x88/4,	/* Good Octets Received Count */
	Gotcl		= 0x90/4,	/* Good Octets Transmitted Count */
	Torl		= 0xC0/4,	/* Total Octets Received */
	Totl		= 0xC8/4,	/* Total Octets Transmitted */
};

enum {					/* Ctrl */
	Lrst		= 1<<3,		/* link reset */
	Slu		= 1<<6,		/* Set Link Up */
	Devrst		= 1<<26,	/* Device Reset */
	Rfce		= 1<<27,	/* Receive Flow Control Enable */
	Tfce		= 1<<28,	/* Transmit Flow Control Enable */
	Phyrst		= 1<<31,	/* Phy Reset */
};

enum {					/* Status */
	Lu		= 1<<1,		/* Link Up */
	Lanid		= 3<<2,		/* mask for Lan ID. */
	Txoff		= 1<<4,		/* Transmission Paused */
	Tbimode		= 1<<5,		/* TBI Mode Indication */
	Phyra		= 1<<10,	/* PHY Reset Asserted */
	GIOme		= 1<<19,	/* GIO Master Enable Status */
};

enum {					/* Eerd */
	EEstart		= 1<<0,		/* Start Read */
	EEdone		= 1<<1,		/* Read done */
};

enum {					/* Ctrlext */
	Eerst		= 1<<13,	/* EEPROM Reset */
	Linkmode	= 3<<22,	/* linkmode */
	Internalphy	= 0<<22,	/* " internal phy (copper) */
	Sgmii		= 2<<22,	/* " sgmii */
	Serdes		= 3<<22,	/* " serdes */
};

enum {
	/* Connsw */
	Enrgirq		= 1<<2,	/* interrupt on power detect (enrgsrc) */
};

enum {					/* EEPROM content offsets */
	Ea		= 0x00,		/* Ethernet Address */
};

enum {					/* phy interface */
	Phyctl		= 0,		/* phy ctl register */
	Phyisr		= 19,		/* 82563 phy interrupt status register */
	Phylhr		= 19,		/* 8257[12] link health register */
	Physsr		= 17,		/* phy secondary status register */
	Phyprst		= 193<<8 | 17,	/* 8256[34] phy port reset */
	Phyier		= 18,		/* 82573 phy interrupt enable register */
	Phypage		= 22,		/* 8256[34] page register */
	Phystat		= 26,		/* 82580 phy status */
	Phyapage	= 29,
	Rtlink		= 1<<10,	/* realtime link status */
	Phyan		= 1<<11,	/* phy has autonegotiated */

	/* Phyctl bits */
	Ran		= 1<<9,	/* restart auto negotiation */
	Ean		= 1<<12,	/* enable auto negotiation */

	/* Phyprst bits */
	Prst		= 1<<0,	/* reset the port */

	/* 82573 Phyier bits */
	Lscie		= 1<<10,	/* link status changed ie */
	Ancie		= 1<<11,	/* auto negotiation complete ie */
	Spdie		= 1<<14,	/* speed changed ie */
	Panie		= 1<<15,	/* phy auto negotiation error ie */

	/* Phylhr/Phyisr bits */
	Anf		= 1<<6,	/* lhr: auto negotiation fault */
	Ane		= 1<<15,	/* isr: auto negotiation error */

	/* 82580 Phystat bits */
	Ans	= 1<<14 | 1<<15,	/* 82580 autoneg. status */
	Link	= 1<<6,		/* 82580 Link */

	/* Rxcw builtin serdes */
	Anc		= 1<<31,
	Rxsynch		= 1<<30,
	Rxcfg		= 1<<29,
	Rxcfgch		= 1<<28,
	Rxcfgbad	= 1<<27,
	Rxnc		= 1<<26,

	/* Txcw */
	Txane		= 1<<31,
	Txcfg		= 1<<30,
};

enum {					/* fiber (pcs) interface */
	Pcsctl	= 0x4208,		/* pcs control */
	Pcsstat	= 0x420c,		/* pcs status */

	/* Pcsctl bits */
	Pan	= 1<<16,		/* autoegotiate */
	Prestart	= 1<<17,		/* restart an (self clearing) */

	/* Pcsstat bits */
	Linkok	= 1<<0,		/* link is okay */
	Andone	= 1<<16,		/* an phase is done see below for success */
	Anbad	= 1<<19 | 1<<20,	/* Anerror | Anremfault */
};

enum {					/* Icr, Ics, Ims, Imc */
	Txdw		= 0x00000001,	/* Transmit Descriptor Written Back */
	Txqe		= 0x00000002,	/* Transmit Queue Empty */
	Lsc		= 0x00000004,	/* Link Status Change */
	Rxseq		= 0x00000008,	/* Receive Sequence Error */
	Rxdmt0		= 0x00000010,	/* Rdesc Minimum Threshold Reached */
	Rxo		= 0x00000040,	/* Receiver Overrun */
	Rxt0		= 0x00000080,	/* Receiver Timer Interrupt; !82575/6/80 only */
	Rxdw		= 0x00000080,	/* Rdesc write back; 82575/6/80 only */
	Mdac		= 0x00000200,	/* MDIO Access Completed */
	Rxcfgset		= 0x00000400,	/* Receiving /C/ ordered sets */
	Ack		= 0x00020000,	/* Receive ACK frame */
	Omed		= 1<<20,	/* media change; pcs interface */
};

enum {					/* Txcw */
	TxcwFd		= 0x00000020,	/* Full Duplex */
	TxcwHd		= 0x00000040,	/* Half Duplex */
	TxcwPauseMASK	= 0x00000180,	/* Pause */
	TxcwPauseSHIFT	= 7,
	TxcwPs		= 1<<TxcwPauseSHIFT,	/* Pause Supported */
	TxcwAs		= 2<<TxcwPauseSHIFT,	/* Asymmetric FC desired */
	TxcwRfiMASK	= 0x00003000,	/* Remote Fault Indication */
	TxcwRfiSHIFT	= 12,
	TxcwNpr		= 0x00008000,	/* Next Page Request */
	TxcwConfig	= 0x40000000,	/* Transmit COnfig Control */
	TxcwAne		= 0x80000000,	/* Auto-Negotiation Enable */
};

enum {					/* Rctl */
	Rrst		= 0x00000001,	/* Receiver Software Reset */
	Ren		= 0x00000002,	/* Receiver Enable */
	Sbp		= 0x00000004,	/* Store Bad Packets */
	Upe		= 0x00000008,	/* Unicast Promiscuous Enable */
	Mpe		= 0x00000010,	/* Multicast Promiscuous Enable */
	Lpe		= 0x00000020,	/* Long Packet Reception Enable */
	RdtmsMASK	= 0x00000300,	/* Rdesc Minimum Threshold Size */
	RdtmsHALF	= 0x00000000,	/* Threshold is 1/2 Rdlen */
	RdtmsQUARTER	= 0x00000100,	/* Threshold is 1/4 Rdlen */
	RdtmsEIGHTH	= 0x00000200,	/* Threshold is 1/8 Rdlen */
	MoMASK		= 0x00003000,	/* Multicast Offset */
	Bam		= 0x00008000,	/* Broadcast Accept Mode */
	BsizeMASK	= 0x00030000,	/* Receive Buffer Size */
	Bsize16384	= 0x00010000,	/* Bsex = 1 */
	Bsize8192	= 0x00020000, 	/* Bsex = 1 */
	Bsize2048	= 0x00000000,
	Bsize1024	= 0x00010000,
	Bsize512	= 0x00020000,
	Bsize256	= 0x00030000,
	BsizeFlex	= 0x08000000,	/* Flexable Bsize in 1kb increments */
	Vfe		= 0x00040000,	/* VLAN Filter Enable */
	Cfien		= 0x00080000,	/* Canonical Form Indicator Enable */
	Cfi		= 0x00100000,	/* Canonical Form Indicator value */
	Dpf		= 0x00400000,	/* Discard Pause Frames */
	Pmcf		= 0x00800000,	/* Pass MAC Control Frames */
	Bsex		= 0x02000000,	/* Buffer Size Extension */
	Secrc		= 0x04000000,	/* Strip CRC from incoming packet */
};

enum {					/* Srrctl */
	Dropen		= 1<<31,
};

enum {					/* Tctl */
	Trst		= 0x00000001,	/* Transmitter Software Reset */
	Ten		= 0x00000002,	/* Transmit Enable */
	Psp		= 0x00000008,	/* Pad Short Packets */
	Mulr		= 0x10000000,	/* Allow multiple concurrent requests */
	CtMASK		= 0x00000FF0,	/* Collision Threshold */
	CtSHIFT		= 4,
	ColdMASK	= 0x003FF000,	/* Collision Distance */
	ColdSHIFT	= 12,
	Swxoff		= 0x00400000,	/* Sofware XOFF Transmission */
	Pbe		= 0x00800000,	/* Packet Burst Enable */
	Rtlc		= 0x01000000,	/* Re-transmit on Late Collision */
	Nrtu		= 0x02000000,	/* No Re-transmit on Underrrun */
};

enum {					/* [RT]xdctl */
	PthreshMASK	= 0x0000003F,	/* Prefetch Threshold */
	PthreshSHIFT	= 0,
	HthreshMASK	= 0x00003F00,	/* Host Threshold */
	HthreshSHIFT	= 8,
	WthreshMASK	= 0x003F0000,	/* Writeback Threshold */
	WthreshSHIFT	= 16,
	Gran		= 0x01000000,	/* Granularity; not 82575 */
	Enable		= 0x02000000,
};

enum {					/* Rxcsum */
	Ipofl		= 0x0100,	/* IP Checksum Off-load Enable */
	Tuofl		= 0x0200,	/* TCP/UDP Checksum Off-load Enable */
};

typedef struct Rd {			/* Receive Descriptor */
	u32int	addr[2];
	u16int	length;
	u16int	checksum;
	uchar	status;
	uchar	errors;
	u16int	special;
} Rd;

enum {					/* Rd status */
	Rdd		= 0x01,		/* Descriptor Done */
	Reop		= 0x02,		/* End of Packet */
	Ixsm		= 0x04,		/* Ignore Checksum Indication */
	Vp		= 0x08,		/* Packet is 802.1Q (matched VET) */
	Tcpcs		= 0x20,		/* TCP Checksum Calculated on Packet */
	Ipcs		= 0x40,		/* IP Checksum Calculated on Packet */
	Pif		= 0x80,		/* Passed in-exact filter */
};

enum {					/* Rd errors */
	Ce		= 0x01,		/* CRC Error or Alignment Error */
	Se		= 0x02,		/* Symbol Error */
	Seq		= 0x04,		/* Sequence Error */
	Cxe		= 0x10,		/* Carrier Extension Error */
	Tcpe		= 0x20,		/* TCP/UDP Checksum Error */
	Ipe		= 0x40,		/* IP Checksum Error */
	Rxe		= 0x80,		/* RX Data Error */
};

typedef struct {			/* Transmit Descriptor */
	u32int	addr[2];		/* Data */
	u32int	control;
	u32int	status;
} Td;

enum {					/* Tdesc control */
	LenMASK		= 0x000FFFFF,	/* Data/Packet Length Field */
	LenSHIFT	= 0,
	DtypeCD		= 0x00000000,	/* Data Type 'Context Descriptor' */
	DtypeDD		= 0x00100000,	/* Data Type 'Data Descriptor' */
	PtypeTCP	= 0x01000000,	/* TCP/UDP Packet Type (CD) */
	Teop		= 0x01000000,	/* End of Packet (DD) */
	PtypeIP		= 0x02000000,	/* IP Packet Type (CD) */
	Ifcs		= 0x02000000,	/* Insert FCS (DD) */
	Tse		= 0x04000000,	/* TCP Segmentation Enable */
	Rs		= 0x08000000,	/* Report Status */
	Rps		= 0x10000000,	/* Report Status Sent */
	Dext		= 0x20000000,	/* Descriptor Extension */
	Vle		= 0x40000000,	/* VLAN Packet Enable */
	Ide		= 0x80000000,	/* Interrupt Delay Enable */
};

enum {					/* Tdesc status */
	Tdd		= 0x0001,	/* Descriptor Done */
	Ec		= 0x0002,	/* Excess Collisions */
	Lc		= 0x0004,	/* Late Collision */
	Tu		= 0x0008,	/* Transmit Underrun */
	CssMASK		= 0xFF00,	/* Checksum Start Field */
	CssSHIFT	= 8,
};

typedef struct {
	u16int	*reg;
	u32int	*reg32;
	int	sz;
} Flash;

enum {
	/* 16 and 32-bit flash registers for ich flash parts */
	Bfpr	= 0x00/4,		/* flash base 0:12; lim 16:28 */
	Fsts	= 0x04/2,		/* flash status; Hsfsts */
	Fctl	= 0x06/2,		/* flash control; Hsfctl */
	Faddr	= 0x08/4,		/* flash address to r/w */
	Fdata	= 0x10/4,		/* data @ address */

	/* status register */
	Fdone	= 1<<0,			/* flash cycle done */
	Fcerr	= 1<<1,			/* cycle error; write 1 to clear */
	Ael	= 1<<2,			/* direct access error log; 1 to clear */
	Scip	= 1<<5,			/* spi cycle in progress */
	Fvalid	= 1<<14,		/* flash descriptor valid */

	/* control register */
	Fgo	= 1<<0,			/* start cycle */
	Flcycle	= 1<<1,			/* two bits: r=0; w=2 */
	Fdbc	= 1<<8,			/* bytes to read; 5 bits */
};

enum {
	Nrd		= 64,		/* multiple of 8 */
	Ntd		= 64,		/* multiple of 8 */
};

/*
 * cavet emptor: 82577/78 have been entered speculatitively.
 * awating datasheet from intel.
 */
enum {
	i82563,
	i82566,
	i82567,
	i82567m,
	i82571,
	i82572,
	i82573,
	i82574,
	i82575,
	i82576,
	i82577,
	i82577m,	
	i82578,
	i82578m,
	i82579,
	i82580,
	i82583,
 	i350,
	Nctlrtype,
};

enum {
	Fload	= 1<<0,
	Fert	= 1<<1,
	F75	= 1<<2,
	Fpba	= 1<<3,
	Fflashea	= 1<<4,
	F79phy	= 1<<5,
};

typedef struct Ctlrtype Ctlrtype;
struct Ctlrtype {
	int	type;
	int	mtu;
	int	flag;
	char	*name;
};

static Ctlrtype cttab[Nctlrtype] = {
	i82563,		9014,	Fpba,		"i82563",
	i82566,		1514,	Fload,		"i82566",
	i82567,		9234,	Fload,		"i82567",
	i82567m,		1514,	0,		"i82567m",
	i82571,		9234,	Fpba,		"i82571",
	i82572,		9234,	Fpba,		"i82572",
	i82573,		8192,	Fert,		"i82573",		/* terrible perf above 8k */
	i82574,		9018,	0,		"i82574",
	i82575,		9728,	F75|Fflashea,	"i82575",
	i82576,		9728,	F75,		"i82576",
	i82577,		4096,	Fload|Fert,	"i82577",
	i82577m,		1514,	Fload|Fert,	"i82577",
	i82578,		4096,	Fload|Fert,	"i82578",
	i82578m,		1514,	Fload|Fert,	"i82578",
	i82579,		9018,	Fload|Fert|F79phy,	"i82579",
	i82580,		9728,	F75|F79phy,	"i82580",
	i82583,		1514,	0,		"i82583",
	i350,		9728,	F75|F79phy,	"i350",
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	ulong	port;
	Pcidev	*pcidev;
	Ctlr	*next;
	int	active;
	int	type;
	u16int	eeprom[0x40];
	uchar	ra[Eaddrlen];

	u32int	*nic;
	Lock	imlock;
	int	im;			/* interrupt mask */

	Lock	slock;

	Rd	*rdba;			/* receive descriptor base address */
	Block	**rb;			/* receive buffers */
	int	rdh;			/* receive descriptor head */
	int	rdt;			/* receive descriptor tail */

	Td	*tdba;			/* transmit descriptor base address */
	Lock	tdlock;
	Block	**tb;			/* transmit buffers */
	int	tdh;			/* transmit descriptor head */
	int	tdt;			/* transmit descriptor tail */

	int	fcrtl;
	int	fcrth;

	/* bootstrap goo */
	Block	*bqhead;	/* transmission queue */
	Block	*bqtail;
};

static Ctlr	*ctlrhead;
static Ctlr	*ctlrtail;

#define csr32r(c, r)	(*((c)->nic+((r)/4)))
#define csr32w(c, r, v)	(*((c)->nic+((r)/4)) = (v))

static char*
cname(Ctlr *c)
{
	return cttab[c->type].name;
}

static void
i82563im(Ctlr* ctlr, int im)
{
	ilock(&ctlr->imlock);
	ctlr->im |= im;
	csr32w(ctlr, Ims, ctlr->im);
	iunlock(&ctlr->imlock);
}

static void
txstart(Ether *edev)
{
	int tdh, tdt;
	Ctlr *ctlr;
	Block *bp;
	Td *td;

	/*
	 * Try to fill the ring back up, moving buffers from the transmit q.
	 */
	ctlr = edev->ctlr;
	tdh = PREV(ctlr->tdh, Ntd);
	for(tdt = ctlr->tdt; tdt != tdh; tdt = NEXT(tdt, Ntd)){
		/* pull off the head of the transmission queue */
		if((bp = ctlr->bqhead) == nil)
			break;
		ctlr->bqhead = bp->next;
		if (ctlr->bqtail == bp)
			ctlr->bqtail = nil;

		/* set up a descriptor for it */
		td = ctlr->tdba + tdt;
		td->addr[0] = PCIWADDR(bp->rp);
		td->addr[1] = 0;
		td->control = Ide|Rs|Ifcs|Teop|BLEN(bp);

		ctlr->tb[tdt] = bp;
	}
	ctlr->tdt = tdt;
	csr32w(ctlr, Tdt, tdt);
	i82563im(ctlr, Txdw);
}

static void
i82563transmit(Ether* edev)
{
	Block *bp;
	Ctlr *ctlr;
	Td *td;
	RingBuf *tb;
	int tdh;

	ctlr = edev->ctlr;
	ilock(&ctlr->tdlock);

	/*
	 * Free any completed packets
	 * - try to get the soft tdh to catch the tdt;
	 * - if the packet had an underrun bump the threshold
	 *   - the Tu bit doesn't seem to ever be set, perhaps
	 *     because Rs mode is used?
	 */
	tdh = ctlr->tdh;
	for(;;){
		td = &ctlr->tdba[tdh];
		if(!(td->status & Tdd))
			break;
		if(ctlr->tb[tdh] != nil){
			freeb(ctlr->tb[tdh]);
			ctlr->tb[tdh] = nil;
		}
		td->status = 0;
		tdh = NEXT(tdh, Ntd);
	}
	ctlr->tdh = tdh;

	/* copy packets from the software RingBuf to the transmission q */
	while((tb = &edev->tb[edev->ti])->owner == Interface){
		bp = fromringbuf(edev);
//print("%d: tx %d %E %E\n", edev->ctlrno, edev->ti, bp->rp, bp->rp+6);

		if(ctlr->bqhead)
			ctlr->bqtail->next = bp;
		else
			ctlr->bqhead = bp;
		ctlr->bqtail = bp;

		txstart(edev);		/* kick transmitter */
		tb->owner = Host;	/* give descriptor back */
		edev->ti = NEXT(edev->ti, edev->ntb);
	}
	iunlock(&ctlr->tdlock);
}

static void
i82563replenish(Ctlr* ctlr)
{
	int rdt;
	Block *bp;
	Rd *rd;

	rdt = ctlr->rdt;
	while(NEXT(rdt, Nrd) != ctlr->rdh){
		rd = &ctlr->rdba[rdt];
		if(ctlr->rb[rdt] != nil){
			/* nothing to do */
		}
		else if((bp = iallocb(2048)) != nil){
			ctlr->rb[rdt] = bp;
			rd->addr[0] = PCIWADDR(bp->rp);
			rd->addr[1] = 0;
		}
		else
			break;
		rd->status = 0;
		rdt = NEXT(rdt, Nrd);
	}
	ctlr->rdt = rdt;
	csr32w(ctlr, Rdt, rdt);
}

static void
i82563interrupt(Ureg*, void* arg)
{
	Block *bp;
	Ctlr *ctlr;
	Ether *edev;
	Rd *rd;
	int icr, im, rdh, txdw = 0;
	static int i;

	edev = arg;
	ctlr = edev->ctlr;

	if(edev->state < 1 || ctlr->active != 1)
		return;

	ilock(&ctlr->imlock);
	csr32w(ctlr, Imc, ~0);
	im = ctlr->im;

	while(icr = csr32r(ctlr, Icr) & ctlr->im){
		if(icr & (Rxseq|Lsc)){
		}

		rdh = ctlr->rdh;
		for(;;){
			rd = &ctlr->rdba[rdh];
			if(!(rd->status & Rdd))
				break;
			if ((rd->status & Reop) && rd->errors == 0) {
				bp = ctlr->rb[rdh];
				ctlr->rb[rdh] = nil;
				bp->wp += rd->length;
				toringbuf(edev, bp->rp, BLEN(bp));
				freeb(bp);
			} else if ((rd->status & Reop) && rd->errors&~0x40)
				/* ignore checksum errors; gre multicast generates this */
				print("%s: rx error %#ux\n",
					cname(ctlr), rd->errors);
			rd->status = 0;
			rdh = NEXT(rdh, Nrd);
			if(i++ > Nrd - 16){
				print("replenish rx %d\n", i);
				i82563replenish(ctlr);
				i = 0;
			}
		}
		ctlr->rdh = rdh;
		if(icr & Rxdmt0){
			i82563replenish(ctlr);
			i = 0;
		}
		if(icr & Txdw){
			im &= ~Txdw;
			txdw++;
		}
	}
	ctlr->im = im;
	csr32w(ctlr, Ims, im);
	iunlock(&ctlr->imlock);
	if(txdw)
		i82563transmit(edev);
}

static void
i82563init(Ether* edev)
{
	int i;
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	ctlr->rdba = xspanalloc(Nrd*sizeof(Rd), 256, 0);
	csr32w(ctlr, Rdbal, PCIWADDR(ctlr->rdba));
	csr32w(ctlr, Rdbah, 0);
	csr32w(ctlr, Rdlen, Nrd*sizeof(Rd));
	ctlr->rdh = 0;
	csr32w(ctlr, Rdh, ctlr->rdh);
	ctlr->rdt = 0;
	csr32w(ctlr, Rdt, ctlr->rdt);
	ctlr->rb = malloc(sizeof(Block*)*Nrd);
	i82563replenish(ctlr);
	csr32w(ctlr, Rdtr, 0);
	if(cttab[ctlr->type].flag & F75){
		csr32w(ctlr, Rctl, Dpf|Bsize2048|Bam|RdtmsHALF|Secrc);
		i = 2;					/* 2kb */
		if(ctlr->type != i82575)
			i |= (Nrd/2>>4)<<20;		/* RdmsHalf */
		csr32w(ctlr, Srrctl, i | Dropen);
		csr32w(ctlr, Rmpl, 2048);
//		csr32w(ctlr, Drxmxod, 0x7ff);
	}
	else
		csr32w(ctlr, Rctl, Dpf|Bsize2048|Bam|RdtmsHALF|Secrc);
	i82563im(ctlr, Rxt0|Rxo|Rxdmt0|Rxseq|Ack);
	
	if(cttab[ctlr->type].flag & F75)
		csr32w(ctlr, Tctl, 0x0F<<CtSHIFT | Psp);
	else
		csr32w(ctlr, Tctl, 0x0F<<CtSHIFT | Psp | 66<<ColdSHIFT | Mulr);
	csr32w(ctlr, Tipg, 6<<20 | 8<<10 | 8);		/* yb sez: 0x702008 */
	csr32w(ctlr, Tidv, 1);

	ctlr->tdba = xspanalloc(Ntd*sizeof(Td), 256, 0);
	memset(ctlr->tdba, 0, Ntd*sizeof(Td));
	csr32w(ctlr, Tdbal, PCIWADDR(ctlr->tdba));
	csr32w(ctlr, Tdbah, 0);
	csr32w(ctlr, Tdlen, Ntd*sizeof(Td));
	ctlr->tdh = 0;
	csr32w(ctlr, Tdh, ctlr->tdh);
	ctlr->tdt = 0;
	csr32w(ctlr, Tdt, ctlr->tdt);
	ctlr->tb = malloc(sizeof(Block*)*Ntd);

//	csr32w(ctlr, Rxcsum, Tuofl | Ipofl | ETHERHDRSIZE);
	csr32w(ctlr, Tctl, csr32r(ctlr, Tctl) | Ten);
	if(cttab[ctlr->type].flag & F75)
		csr32w(ctlr, Txdctl, csr32r(ctlr, Txdctl) | Enable);
}

static void
i82563attach(Ether* edev)
{
	Ctlr *ctlr;

	ctlr = edev->ctlr;
	i82563im(ctlr, 0);
	csr32w(ctlr, Rctl, csr32r(ctlr, Rctl) | Ren);
	if(cttab[ctlr->type].flag & F75)
		csr32w(ctlr, Rxdctl, csr32r(ctlr, Rxdctl) | Enable);
	csr32w(ctlr, Tctl, csr32r(ctlr, Tctl)|Ten);
	i82563im(ctlr, Rxt0|Rxo|Rxdmt0|Rxseq|Ack);
	i82563replenish(ctlr);
}

static int
detach(Ctlr *ctlr)
{
	int r, timeo;

	/*
	 * Perform a device reset to get the chip back to the
	 * power-on state, followed by an EEPROM reset to read
	 * the defaults for some internal registers.
	 */
	csr32w(ctlr, Imc, ~0);
	csr32w(ctlr, Rctl, 0);
	csr32w(ctlr, Tctl, csr32r(ctlr, Tctl) & ~Ten);

	delay(10);

	r = csr32r(ctlr, Ctrl);
	if(ctlr->type == i82566 || ctlr->type == i82579)
		r |= Phyrst;
	csr32w(ctlr, Ctrl, Devrst | r);
	delay(1);
	for(timeo = 0;; timeo++){
		if((csr32r(ctlr, Ctrl) & (Devrst|Phyrst)) == 0)
			break;
		if(timeo >= 1000)
			return -1;
		delay(1);
	}

	r = csr32r(ctlr, Ctrl);
	csr32w(ctlr, Ctrl, Slu|r);

	r = csr32r(ctlr, Ctrlext);
	csr32w(ctlr, Ctrlext, r|Eerst);
	delay(1);
	for(timeo = 0; timeo < 1000; timeo++){
		if(!(csr32r(ctlr, Ctrlext) & Eerst))
			break;
		delay(1);
	}
	if(csr32r(ctlr, Ctrlext) & Eerst)
		return -1;

	csr32w(ctlr, Imc, ~0);
	delay(1);
	for(timeo = 0; timeo < 1000; timeo++){
		if((csr32r(ctlr, Icr) & ~Rxcfg) == 0)
			break;
		delay(1);
	}
	if(csr32r(ctlr, Icr) & ~Rxcfg)
		return -1;

	return 0;
}

static void
i82563detach(Ether *edev)
{
	detach(edev->ctlr);
}

static void
i82563shutdown(Ether* ether)
{
	i82563detach(ether);
}

static ushort
eeread(Ctlr *ctlr, int adr)
{
	csr32w(ctlr, Eerd, EEstart | adr << 2);
	while ((csr32r(ctlr, Eerd) & EEdone) == 0)
		;
	return csr32r(ctlr, Eerd) >> 16;
}

static int
eeload(Ctlr *ctlr)
{
	u16int sum;
	int data, adr;

	sum = 0;
	for (adr = 0; adr < 0x40; adr++) {
		data = eeread(ctlr, adr);
		ctlr->eeprom[adr] = data;
		sum += data;
	}
	return sum;
}

static int
fcycle(Ctlr*, Flash *f)
{
	u16int s, i;

	s = f->reg[Fsts];
	if((s&Fvalid) == 0)
		return -1;
	f->reg[Fsts] |= Fcerr | Ael;
	for(i = 0; i < 10; i++){
		if((s&Scip) == 0)
			return 0;
		delay(1);
		s = f->reg[Fsts];
	}
	return -1;
}

static int
fread(Ctlr *c, Flash *f, int ladr)
{
	u16int s;

	delay(1);
	if(fcycle(c, f) == -1)
		return -1;
	f->reg[Fsts] |= Fdone;
	f->reg32[Faddr] = ladr;

	/* setup flash control register */
	s = f->reg[Fctl] & ~0x3ff;
	f->reg[Fctl] = s | 1<<8 | Fgo;	/* 2 byte read */

	while((f->reg[Fsts] & Fdone) == 0)
		;
	if(f->reg[Fsts] & (Fcerr|Ael))
		return -1;
	return f->reg32[Fdata] & 0xffff;
}

static int
fload(Ctlr *c)
{
	ulong data, io, r, adr;
	u16int sum;
	Flash f;

	io = c->pcidev->mem[1].bar & ~0x0f;
	f.reg = (u16int*)upamalloc(io, c->pcidev->mem[1].size, 0);
	if(f.reg == nil)
		return -1;
	f.reg = KADDR(f.reg);
	f.reg32 = (u32int*)f.reg;
	f.sz = f.reg32[Bfpr];
	if(csr32r(c, Eec) & 1<<22){
		if(c->type == i82579)
			f.sz  += 16;		/* sector size: 64k */
		else
			f.sz  += 1;		/* sector size: 4k */
	}
	r = (f.sz & 0x1fff) << 12;
	sum = 0;
	for(adr = 0; adr < 0x40; adr++) {
		data = fread(c, &f, r + adr*2);
		if(data == -1)
			return -1;
		c->eeprom[adr] = data;
		sum += data;
	}
//	vunmap(f.reg, c->pcidev->mem[1].size);
	return sum;
}

static void
defaultea(Ctlr *ctlr, uchar *ra)
{
	uint i, r;
	uvlong u;
	static uchar nilea[Eaddrlen];

	if(memcmp(ra, nilea, Eaddrlen) != 0)
		return;
	if(cttab[ctlr->type].flag & Fflashea){
		/* intel mb bug */
		u = (uvlong)csr32r(ctlr, Rah)<<32u | (ulong)csr32r(ctlr, Ral);
		for(i = 0; i < Eaddrlen; i++)
			ra[i] = u >> 8*i;
	}
	if(memcmp(ra, nilea, Eaddrlen) != 0)
		return;
	for(i = 0; i < Eaddrlen/2; i++){
		ra[2*i] = ctlr->eeprom[Ea+i];
		ra[2*i+1] = ctlr->eeprom[Ea+i] >> 8;
	}
	r = (csr32r(ctlr, Status) & Lanid) >> 2;
	ra[5] += r;				/* ea ctlr[n] = ea ctlr[0]+n */
}

static int
i82563reset(Ctlr *ctlr)
{
	uchar *ra;
	int i, r;

	if(detach(ctlr) == -1)
		return -1;
	if(cttab[ctlr->type].flag & Fload)
		r = fload(ctlr);
	else
		r = eeload(ctlr);
	if(r != 0 && r != 0xbaba){
		print("%s: bad eeprom checksum - %#.4ux\n",
			cname(ctlr), r);
		return -1;
	}

	ra = ctlr->ra;
	defaultea(ctlr, ra);
	csr32w(ctlr, Ral, ra[3]<<24 | ra[2]<<16 | ra[1]<<8 | ra[0]);
	csr32w(ctlr, Rah, 1<<31 | ra[5]<<8 | ra[4]);
	for(i = 1; i < 16; i++){
		csr32w(ctlr, Ral+i*8, 0);
		csr32w(ctlr, Rah+i*8, 0);
	}
	for(i = 0; i < 128; i++)
		csr32w(ctlr, Mta + i*4, 0);
	csr32w(ctlr, Fcal, 0x00C28001);
	csr32w(ctlr, Fcah, 0x0100);
	if(ctlr->type != i82579)
		csr32w(ctlr, Fct, 0x8808);
	csr32w(ctlr, Fcttv, 0x0100);
	csr32w(ctlr, Fcrtl, ctlr->fcrtl);
	csr32w(ctlr, Fcrth, ctlr->fcrth);
	if(cttab[ctlr->type].flag & F75)
		csr32w(ctlr, Eitr, 128<<2);		/* 128 ¼ microsecond intervals */

	ilock(&ctlr->imlock);
	csr32w(ctlr, Imc, ~0);
	ctlr->im = 0; //Lsc;
	csr32w(ctlr, Ims, ctlr->im);
	iunlock(&ctlr->imlock);
	return 0;
}

static int
didtype(int d)
{
	switch(d){
	case 0x1096:
	case 0x10ba:		/* “gilgal” */
	case 0x1098:		/* serdes; not seen */
	case 0x10bb:		/* serdes */
		return i82563;
	case 0x1049:		/* mm */
	case 0x104a:		/* dm */
	case 0x104b:		/* dc */
	case 0x104d:		/* v “ninevah” */
	case 0x10bd:		/* dm-2 */
	case 0x294c:		/* ich 9 */
		return i82566;
	case 0x10de:		/* lm ich10d */
	case 0x10df:		/* lf ich10 */
	case 0x10e5:		/* lm ich9 */
	case 0x10f5:		/* lm ich9m; “boazman” */
		return i82567;
	case 0x10bf:		/* lf ich9m */
	case 0x10cb:		/* v ich9m */
	case 0x10cd:		/* lf ich10 */
	case 0x10ce:		/* v ich10 */
	case 0x10cc:		/* lm ich10 */
		return i82567m;
	case 0x105e:		/* eb */
	case 0x105f:		/* eb */
	case 0x1060:		/* eb */
	case 0x10a4:		/* eb */
	case 0x10a5:		/* eb  fiber */
	case 0x10bc:		/* eb */
	case 0x10d9:		/* eb serdes */
	case 0x10da:		/* eb serdes “ophir” */
		return i82571;
	case 0x107d:		/* eb copper */
	case 0x107e:		/* ei fiber */
	case 0x107f:		/* ei */
	case 0x10b9:		/* ei “rimon” */
		return i82572;
	case 0x108b:		/*  e “vidalia” */
	case 0x108c:		/*  e (iamt) */
	case 0x109a:		/*  l “tekoa” */
		return i82573;
	case 0x10d3:		/* l or it; “hartwell” */
		return i82574;
	case 0x10a7:
	case 0x10a9:		/* fiber/serdes */
		return i82575;
	case 0x10c9:		/* copper */
	case 0x10e6:		/* fiber */
	case 0x10e7:		/* serdes; “kawela” */
	case 0x150d:		/* backplane */
		return i82576;
	case 0x10ea:		/* lc “calpella”; aka pch lan */
		return i82577;
	case 0x10eb:		/* lm “calpella” */
		return i82577m;
	case 0x10ef:		/* dc “piketon” */
		return i82578;
	case 0x1502:		/* lm */
	case 0x1503:		/* v “lewisville” */
		return i82579;
	case 0x10f0:		/* dm “king's creek” */
		return i82578m;
	case 0x150e:		/* “barton hills” */
	case 0x150f:		/* fiber */
	case 0x1510:		/* backplane */
	case 0x1511:		/* sfp */
	case 0x1516:		
		return i82580;
	case 0x1506:		/* v */
		return i82583;
	case 0x151f:		/* “powerville” eeprom-less */
	case 0x1521:		/* copper */
	case 0x1522:		/* fiber */
	case 0x1523:		/* serdes */
	case 0x1524:		/* sgmii */
		return i350;
	}
	return -1;
}

static void
hbafixup(Pcidev *p)
{
	uint i;

	i = pcicfgr32(p, PciSVID);
	if((i & 0xffff) == 0x1b52 && p->did == 1)
		p->did = i>>16;
}

static void
i82563pci(void)
{
	int type;
	Ctlr *ctlr;
	Pcidev *p;

	for(p = nil; p = pcimatch(p, 0x8086, 0);){
		hbafixup(p);
		if((type = didtype(p->did)) == -1)
			continue;
		ctlr = malloc(sizeof(Ctlr));
		ctlr->type = type;
		ctlr->pcidev = p;
	//	ctlr->rbsz = cttab[type].mtu;
		ctlr->port = p->mem[0].bar & ~0x0F;
		if(ctlrhead != nil)
			ctlrtail->next = ctlr;
		else
			ctlrhead = ctlr;
		ctlrtail = ctlr;
	}
}

static int
setup(Ctlr *ctlr)
{
	Pcidev *p;
	ulong nic;

	p = ctlr->pcidev;
	nic = upamalloc(ctlr->port, p->mem[0].size, 0);
	if(nic == 0){
		print("%s: can't map %#p\n", cname(ctlr), (uintptr)ctlr->port);
		return -1;
	}
	ctlr->nic = KADDR(nic);
	if(i82563reset(ctlr)){
//		vunmap(ctlr->nic, p->mem[0].size);
		return -1;
	}
	pcisetbme(ctlr->pcidev);
	return 0;
}

static uchar nilea[Eaddrlen];

int
pnp(Ether* edev, int type)
{
	Ctlr *ctlr;
	static int done;

	if(!done) {
		i82563pci();
		done = 1;
	}

	/*
	 * Any adapter matches if no edev->port is supplied,
	 * otherwise the ports must match.
	 */
	for(ctlr = ctlrhead; ; ctlr = ctlr->next){
		if(ctlr == nil)
			return -1;
		if(ctlr->active)
			continue;
		if(type != -1 && ctlr->type != type)
			continue;
		if(edev->port == 0 || edev->port == ctlr->port){
			ctlr->active = -1;
			memmove(ctlr->ra, edev->ea, Eaddrlen);
			if(setup(ctlr) == 0)
				break;
		}
	}

	/*
	 *  with the current structure, there is no right place for this.
	 *  ideally, we recognize the interface, note it's down and move on.
	 *  currently either we can skip the interface or note it is down,
	 *  but not both.
	 */
	if(ctlr->type != i82579)	/* could use bit 6 phy reg 26 */
	if((csr32r(ctlr, Status)&Lu) == 0){
		print("ether#%d: %s: link down\n", edev->ctlrno, cname(ctlr));
		detach(ctlr);
		return -1;
	}
	ctlr->active = 1;

	edev->ctlr = ctlr;
	edev->port = ctlr->port;
	edev->irq = ctlr->pcidev->intl;
	edev->tbdf = ctlr->pcidev->tbdf;
//	edev->mbps = 1000;

	if(memcmp(edev->ea, nilea, Eaddrlen) == 0)
		memmove(edev->ea, ctlr->ra, Eaddrlen);
	i82563init(edev);

	/*
	 * Linkage to the generic ethernet driver.
	 */
	edev->attach = i82563attach;
	edev->transmit = i82563transmit;
	edev->interrupt = i82563interrupt;
	edev->detach = i82563detach;

	return 0;
}

int
i82563pnp(Ether *edev)
{
	int i;

	/* important to get onboard nics first */
	for(i = 0; i < nelem(cttab); i++)
		if(pnp(edev, cttab[i].type) == 0)
			return 0;
	return 1;
}
