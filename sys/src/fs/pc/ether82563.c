/*
 * Intel 8256[367], 8257[1-9], 8258[03], i350
 *	Gigabit Ethernet PCI-Express Controllers
 * Coraid EtherDrive® hba
 */
#include "all.h"
#include "io.h"
#include "../ip/ip.h"
#include "etherif.h"
#include "mem.h"

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
	Nstatistics	= 0x124/4,
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

enum {
	/* Eec */
	Nvpres		= 1<<8,		/* nvram present */
	Autord		= 1<<9,		/* autoread complete */
	Sec1val		= 1<<22,		/* sector 1 valid (!sec0) */
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

enum {					/* EEPROM content offsets */
	Ea		= 0x00,		/* Ethernet Address */
};

enum {					/* Mdic */
	MDIdMASK	= 0x0000FFFF,	/* Data */
	MDIdSHIFT	= 0,
	MDIrMASK	= 0x001F0000,	/* PHY Register Address */
	MDIrSHIFT	= 16,
	MDIpMASK	= 0x03E00000,	/* PHY Address */
	MDIpSHIFT	= 21,
	MDIwop		= 0x04000000,	/* Write Operation */
	MDIrop		= 0x08000000,	/* Read Operation */
	MDIready	= 0x10000000,	/* End of Transaction */
	MDIie		= 0x20000000,	/* Interrupt Enable */
	MDIe		= 0x40000000,	/* Error */
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
	uint	base;
	uint	lim;
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
	Nrd		= 256,		/* power of two */
	Ntd		= 64,		/* power of two */
	Nrb		= 512,		/* private receive buffers per Ctlr */
	Nctlr		= 4,
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

typedef void (*Freefn)(Msgbuf*);

typedef struct Ctlr Ctlr;
struct Ctlr {
	ulong	port;
	Pcidev	*pcidev;
	int	active;
	int	type;
	int	pool;
	u16int	eeprom[0x40];

	void	*alloc;			/* receive/transmit descriptors */
	int	nrd;
	int	ntd;
	uint	rbsz;

	u32int	*nic;
	Lock	imlock;
	int	im;			/* interrupt mask */

	uchar	ra[Easize];		/* receive address */
	u32int	mta[128];		/* multicast table array */

	Rendez	rrendez;
	int	rim;
	int	rdfree;
	Rd	*rdba;			/* receive descriptor base address */
	Msgbuf	**rb;			/* receive buffers */
	uint	rdh;			/* receive descriptor head */
	uint	rdt;			/* receive descriptor tail */
	int	rdtr;			/* receive delay timer ring value */
	int	radv;			/* receive interrupt absolute delay timer */

	Rendez	trendez;
	QLock	tlock;
	int	tbusy;
	Td	*tdba;			/* transmit descriptor base address */
	Msgbuf	**tb;			/* transmit buffers */
	int	tdh;			/* transmit descriptor head */
	int	tdt;			/* transmit descriptor tail */

	int	fcrtl;
	int	fcrth;

	uint	pba;			/* packet buffer allocation */

	char	rname[NAMELEN];
	char	tname[NAMELEN];
	Queue	*reply;			/* alias of ifc->reply. */
};

#define Get(c, r)	(*((c)->nic+((r)/4)))
#define Set(c, r, v)	(*((c)->nic+((r)/4)) = (v))

static Ctlr	ports[Nctlr];
static int		nports;

static Lock	i82563rblock[Nctlrtype];		/* free receive Blocks */
static Msgbuf	*i82563rbpool[Nctlrtype];

static char*
cname(Ctlr *c)
{
	return cttab[c->type].name;
}

static Msgbuf*
rballoc(int t)
{
	Msgbuf *m;

	ilock(&i82563rblock[t]);
	if((m = i82563rbpool[t]) != nil){
		i82563rbpool[t]= m->next;
		m->next = nil;
		m->flags &= ~FREE;
		m->count = 0;
		m->data = (uchar*)PGROUND((uintptr)m->xdata);
	}
	iunlock(&i82563rblock[t]);
	return m;
}

static void
rbfree(Msgbuf *m, int t)
{
	m->flags |= FREE;
	ilock(&i82563rblock[t]);
	m->next = i82563rbpool[t];
	i82563rbpool[t] = m;
	iunlock(&i82563rblock[t]);
}

static void
rbfree0(Msgbuf *m)
{
	rbfree(m, 0);
}

static void
rbfree1(Msgbuf *m)
{
	rbfree(m, 1);
}

static void
rbfree2(Msgbuf *m)
{
	rbfree(m, 2);
}

static void
rbfree3(Msgbuf *m)
{
	rbfree(m, 3);
}

static void
rbfree4(Msgbuf *m)
{
	rbfree(m, 4);
}


static void
rbfree5(Msgbuf *m)
{
	rbfree(m, 5);
}

static void
rbfree6(Msgbuf *m)
{
	rbfree(m, 6);
}

static void
rbfree7(Msgbuf *m)
{
	rbfree(m, 7);
}

static Freefn freetab[] = {
	rbfree0,
	rbfree1,
	rbfree2,
	rbfree3,
	rbfree4,
	rbfree5,
	rbfree6,
	rbfree7,
};

static int
newpool(void)
{
	static int seq;

	if(seq == nelem(freetab))
		return -1;
	if(freetab[seq] == nil){
		print("82563: bad freetab\n");
		return -1;
	}
	return seq++;
}

static void
i82563im(Ctlr *c, int im)
{
	ilock(&c->imlock);
	c->im |= im;
	Set(c, Ims, c->im);
	iunlock(&c->imlock);
}

static void
i82563txinit(Ctlr *c)
{
	int i, r;
	Msgbuf *m;

	if(cttab[c->type].flag & F75)
		Set(c, Tctl, 0x0F<<CtSHIFT | Psp);
	else
		Set(c, Tctl, 0x0F<<CtSHIFT | Psp | 66<<ColdSHIFT | Mulr);
	Set(c, Tipg, 6<<20 | 8<<10 | 8);		/* yb sez: 0x702008 */
	Set(c, Tdbal, PCIWADDR(c->tdba));
//	Set(c, Tdbah, Pciwaddrh(c->tdba));
	Set(c, Tdbah, 0);
	Set(c, Tdlen, c->ntd * sizeof(Td));
	c->tdh = PREV(0, c->ntd);
	Set(c, Tdh, 0);
	c->tdt = 0;
	Set(c, Tdt, 0);
	for(i = 0; i < c->ntd; i++){
		if((m = c->tb[i]) != nil){
			c->tb[i] = nil;
			mbfree(m);
		}
		memset(&c->tdba[i], 0, sizeof(Td));
	}
	Set(c, Tidv, 128);
	Set(c, Tadv, 64);
	Set(c, Tctl, Get(c, Tctl) | Ten);
	r = Get(c, Txdctl) & ~WthreshMASK;
	r |= 4<<WthreshSHIFT | 4<<PthreshSHIFT;
	if(cttab[c->type].flag & F75)
		r |= Enable;
	Set(c, Txdctl, r);
}

#define Next(x, m)	(((x)+1) & (m))

static int
i82563cleanup(Ctlr *c)
{
	int tdh, m, n;
	Msgbuf *b;

	tdh = c->tdh;
	m = c->ntd-1;
	while(c->tdba[n = Next(tdh, m)].status & Tdd){
		tdh = n;
		if((b = c->tb[tdh]) != nil){
			c->tb[tdh] = nil;
			mbfree(b);
		}else
			print("82563 tx underrun!\n");
		c->tdba[tdh].status = 0;
	}

	return c->tdh = tdh;
}

static void
i82563transmit(Ether *e)
{
	int tdh, tdt, m;
	Ctlr *c;
	Td *td;
	Msgbuf *b;

	c = e->ctlr;
	qlock(&c->tlock);
	tdh = i82563cleanup(c);

	tdt = c->tdt;
	m = c->ntd-1;
	for(;;){
		if(Next(tdt, m) == tdh){
			i82563im(c, Txdw);
			break;
		}
		if((b = etheroq(e)) == nil)
			break;
		td = &c->tdba[tdt];
		td->addr[0] = PCIWADDR(b->data);
//		td->addr[1] = Pciwaddrh(b->rp);
		td->control = Ide|Rs|Ifcs|Teop|b->count;
		c->tb[tdt] = b;
		tdt = Next(tdt, m);
	}
	if(c->tdt != tdt){
		c->tdt = tdt;
		Set(c, Tdt, tdt);
	}
	qunlock(&c->tlock);
}

static void
i82563replenish(Ctlr *c)
{
	int rdt, m;
	Msgbuf *b;
	Rd *rd;

	rdt = c->rdt;
	m = c->nrd-1;
	while(Next(rdt, m) != c->rdh){
		rd = &c->rdba[rdt];
		if(c->rb[rdt] != nil){
			print("82563 tx overrun\n");
			break;
		}
		if((b = rballoc(c->pool)) == nil){
			print("82563 no available buffers\n");
			break;
		}
		c->rb[rdt] = b;
		rd->addr[0] = PCIWADDR(b->data);
	//	rd->addr[1] = Pciwaddrh(bp->rp);
		rd->status = 0;
		c->rdfree++;
		rdt = Next(rdt, m);
	}
	c->rdt = rdt;
	Set(c, Rdt, rdt);
}

static void
i82563rxinit(Ctlr *c)
{
	int i;
	Msgbuf *m;

	if(c->rbsz <= 2048)
		Set(c, Rctl, Dpf|Bsize2048|Bam|RdtmsHALF);
	else{
		i = c->rbsz / 1024;
		if(c->rbsz % 1024)
			i++;
		if(cttab[c->type].flag & F75){
			Set(c, Rctl, Lpe|Dpf|Bsize2048|Bam|RdtmsHALF|Secrc);
			if(c->type != i82575)
				i |= (c->nrd/2>>4)<<20;		/* RdmsHalf */
			Set(c, Srrctl, i | Dropen);
			Set(c, Rmpl, c->rbsz);
//			Set(c, Drxmxod, 0x7ff);
		}else
			Set(c, Rctl, Lpe|Dpf|BsizeFlex*i|Bam|RdtmsHALF|Secrc);
	}

	if(cttab[c->type].flag & Fert)
		Set(c, Ert, 1024/8);

	if(c->type == i82566)
		Set(c, Pbs, 16);

	Set(c, Rdbal, PCIWADDR(c->rdba));
//	Set(c, Rdbah, Pciwaddrh(c->rdba));
	Set(c, Rdbah, 0);
	Set(c, Rdlen, c->nrd * sizeof(Rd));
	c->rdh = 0;
	Set(c, Rdh, 0);
	c->rdt = 0;
	Set(c, Rdt, 0);
	c->rdtr = 25;
	c->radv = 250;
	Set(c, Rdtr, c->rdtr);
	Set(c, Radv, c->radv);

	for(i = 0; i < c->nrd; i++)
		if((m = c->rb[i]) != nil){
			c->rb[i] = nil;
			mbfree(m);
		}
	if(cttab[c->type].flag & F75)
		Set(c, Rxdctl, 1<<WthreshSHIFT | 8<<PthreshSHIFT | 1<<HthreshSHIFT | Enable);
	else
		Set(c, Rxdctl, 2<<WthreshSHIFT | 2<<PthreshSHIFT);

	/*
	 * Enable checksum offload.
	 */
	Set(c, Rxcsum, Tuofl | Ipofl | Ensize);
}

static int
rim0(void *rim)
{
	return *(int*)rim != 0;
}

static void
i82563rxproc(void)
{
	uint m, rdh, rim, im;
	Ctlr *c;
	Ether *e;
	Msgbuf *b;
	Rd *rd;

	e = u->arg;
	c = e->ctlr;
	i82563rxinit(c);
	Set(c, Rctl, Get(c, Rctl) | Ren);
	if(cttab[c->type].flag & F75){
		Set(c, Rxdctl, Get(c, Rxdctl) | Enable);
		im = Rxt0|Rxo|Rxdmt0|Rxseq|Ack;
	}else
		im = Rxt0|Rxo|Rxdmt0|Rxseq|Ack;
	m = c->nrd-1;

	for(;;){
		i82563im(c, im);
		i82563replenish(c);
		sleep(&c->rrendez, rim0, &c->rim);

		rdh = c->rdh;
		for(;;){
			rd = &c->rdba[rdh];
			rim = c->rim;
			c->rim = 0;
			if(!(rd->status & Rdd))
				break;
	
			if(b = c->rb[rdh]) {
				if((rd->status & Reop) && rd->errors == 0){
					b->count = rd->length;
					b->next = nil;
					etheriq(e, b);
				} else
					mbfree(b);
				c->rb[rdh] = nil;
			}
			rd->status = 0;
			c->rdfree--;
			c->rdh = rdh = Next(rdh, m);
			if(c->nrd-c->rdfree >= 32 || (rim & Rxdmt0))
				i82563replenish(c);
		}
	}
}

static void
i82563tproc(void)
{
	Ctlr *c;
	Ether *e;

	e = u->arg;
	c = e->ctlr;

	for(;;){
		sleep(&c->trendez, no, 0);
		i82563transmit(e);
	}
}

static void
i82563attach(Ether *e)
{
	Ctlr *c;
	Msgbuf *m;

	c = e->ctlr;
	c->nrd = Nrd;
	c->ntd = Ntd;
	c->alloc = ialloc(c->nrd*sizeof(Rd)+c->ntd*sizeof(Td) + 255, 0);
	c->rdba = (Rd*)ROUNDUP((ulong)c->alloc, 256);
	c->tdba = (Td*)(c->rdba+c->nrd);

	c->rb = ialloc(c->nrd*sizeof m, 0);
	c->tb = ialloc(c->ntd*sizeof m, 0);

	mballocpool(Nrb, c->rbsz, BY2PG, Mbeth82563, freetab[c->pool]);
	snprint(c->rname, sizeof c->rname, "r%ld", c-ports);
	userinit(i82563rxproc, e, c->rname);
	snprint(c->tname, sizeof c->tname, "t%ld", c-ports);
	userinit(i82563tproc, e, c->tname);

	i82563txinit(c);
}

static void
i82563interrupt(Ureg*, void *v)
{
	int icr, im;
	Ctlr *c;
	Ether *e;

	e = v;
	c = e->ctlr;

	ilock(&c->imlock);
	Set(c, Imc, ~0);
	im = c->im;

	while(icr = Get(c, Icr) & c->im){
		if(icr & (Rxt0|Rxo|Rxdmt0|Rxseq|Ack)){
			c->rim = icr & (Rxt0|Rxo|Rxdmt0|Rxseq|Ack);
			im &= ~(Rxt0|Rxo|Rxdmt0|Rxseq|Ack);
			wakeup(&c->rrendez);
		}
		if(icr & Txdw){
			im &= ~Txdw;
			wakeup(&c->trendez);
		}
	}

	c->im = im;
	Set(c, Ims, im);
	iunlock(&c->imlock);
}

static int
i82563detach(Ctlr *c)
{
	int r, timeo;

	/* balance rx/tx packet buffer; survives reset */
	if(c->rbsz > 8192 && cttab[c->type].flag & Fpba){
		c->pba = Get(c, Pba);
		r = c->pba >> 16;
		r += c->pba & 0xffff;
		r >>= 1;
		Set(c, Pba, r);
	}else if(c->type == i82573 && c->rbsz > 1514)
		Set(c, Pba, 14);
	c->pba = Get(c, Pba);

	/*
	 * Perform a device reset to get the chip back to the
	 * power-on state, followed by an EEPROM reset to read
	 * the defaults for some internal registers.
	 */
	Set(c, Imc, ~0);
	Set(c, Rctl, 0);
	Set(c, Tctl, Get(c, Tctl) & ~Ten);

	delay(10);

	r = Get(c, Ctrl);
	if(c->type == i82566 || c->type == i82579)
		r |= Phyrst;
	Set(c, Ctrl, Devrst | r);
	delay(1);
	for(timeo = 0;; timeo++){
		if((Get(c, Ctrl) & (Devrst|Phyrst)) == 0)
			break;
		if(timeo >= 1000)
			return -1;
		delay(1);
	}

	r = Get(c, Ctrl);
	Set(c, Ctrl, Slu|r);

	r = Get(c, Ctrlext);
	Set(c, Ctrlext, r|Eerst);
	delay(1);
	for(timeo = 0; timeo < 1000; timeo++){
		if(!(Get(c, Ctrlext) & Eerst))
			break;
		delay(1);
	}
	if(Get(c, Ctrlext) & Eerst)
		return -1;

	Set(c, Imc, ~0);
	delay(1);
	for(timeo = 0; timeo < 1000; timeo++){
		if((Get(c, Icr) & ~Rxcfg) == 0)
			break;
		delay(1);
	}
	if(Get(c, Icr) & ~Rxcfg)
		return -1;

	return 0;
}

static ushort
eeread(Ctlr *c, int adr)
{
	Set(c, Eerd, EEstart | adr << 2);
	while ((Get(c, Eerd) & EEdone) == 0)
		;
	return Get(c, Eerd) >> 16;
}

static int
eeload(Ctlr *c)
{
	u16int sum;
	int data, adr;

	sum = 0;
	for (adr = 0; adr < 0x40; adr++) {
		data = eeread(c, adr);
		c->eeprom[adr] = data;
		sum += data;
	}
	return sum;
}

static int
fcycle(Ctlr *, Flash *f)
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
	uint data, io, r, adr;
	u16int sum;
	Flash f;

	io = c->pcidev->mem[1].bar & ~0x0f;
	f.reg = vmap(io, c->pcidev->mem[1].size);
	if(f.reg == nil)
		return -1;
	f.reg32 = (u32int*)f.reg;
	f.base = f.reg32[Bfpr] & 0x1fff;
	f.lim = f.reg32[Bfpr]>>16 & 0x1fff;
	if(Get(c, Eec) & Sec1val)
		f.base += f.lim+1 - f.base >> 1;
	r = f.base << 12;
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
defaultea(Ctlr *c, uchar *ra)
{
	uint i, r;
	uvlong u;
	static uchar nilea[Easize];

	if(memcmp(ra, nilea, Easize) != 0)
		return;
	if(cttab[c->type].flag & Fflashea){
		/* intel mb bug */
		u = (uvlong)Get(c, Rah)<<32u | (ulong)Get(c, Ral);
		for(i = 0; i < Easize; i++)
			ra[i] = u >> 8*i;
	}
	if(memcmp(ra, nilea, Easize) != 0)
		return;
	for(i = 0; i < Easize/2; i++){
		ra[2*i] = c->eeprom[Ea+i];
		ra[2*i+1] = c->eeprom[Ea+i] >> 8;
	}
	r = (Get(c, Status) & Lanid) >> 2;
	ra[5] += r;				/* ea ctlr[n] = ea ctlr[0]+n */
}

static int
reset(Ctlr *c)
{
	uchar *ra;
	int i, r;

	if(i82563detach(c))
		return -1;
	if(cttab[c->type].flag & Fload)
		r = fload(c);
	else
		r = eeload(c);
	if(r != 0 && r != 0xbaba){
		print("%s: bad eeprom checksum - %#.4ux\n",
			cname(c), r);
		return -1;
	}

	ra = c->ra;
	defaultea(c, ra);
	Set(c, Ral, ra[3]<<24 | ra[2]<<16 | ra[1]<<8 | ra[0]);
	Set(c, Rah, 1<<31 | ra[5]<<8 | ra[4]);
	for(i = 1; i < 16; i++){
		Set(c, Ral+i*8, 0);
		Set(c, Rah+i*8, 0);
	}
	memset(c->mta, 0, sizeof(c->mta));
	for(i = 0; i < 128; i++)
		Set(c, Mta + i*4, 0);
	Set(c, Fcal, 0x00C28001);
	Set(c, Fcah, 0x0100);
	if(c->type != i82579)
		Set(c, Fct, 0x8808);
	Set(c, Fcttv, 0x0100);
	Set(c, Fcrtl, c->fcrtl);
	Set(c, Fcrth, c->fcrth);
	if(cttab[c->type].flag & F75)
		Set(c, Eitr, 128<<2);		/* 128 ¼ microsecond intervals */
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
i82563init(Ether *)
{
	int type, n;
	Ctlr *c;
	Pcidev *p;

	n = Nctlr;
	if(nelem(freetab) < n)
		n = nelem(freetab);
	for(p = nil; p = pcimatch(p, 0x8086, 0); ){
		if(nports >= n)
			break;
 		hbafixup(p);
		if((type = didtype(p->did)) == -1)
			continue;
		c = ports+nports;
		memset(c, 0, sizeof *c);

		c->type = type;
		c->pcidev = p;
		c->rbsz = cttab[type].mtu;
		c->port = p->mem[0].bar & ~0x0F;

		c->pool = newpool();
		c->nic = (u32int*)vmap(c->port, p->mem[0].size);
		if(reset(c))
			continue;
		pcisetbme(p);
		print("%s %d irq %d mtu %d Ea %E\n", cttab[type].name, nports, p->intl, c->rbsz, ports[nports].ra);
		nports++;
	}
}

int
i82563reset(Ether *e)
{
	int i;
	static int once;

	if(once++ == 0)
		i82563init(e);
	for(i = 0;; i++){
		if(i == nports)
			return -1;
		if(ports[i].active)
			continue;
		if(e->port != 0 && e->port != ports[i].port)
			continue;
		break;
	}
	ports[i].active = 1;
	e->ctlr = ports+i;
	e->port = ports[i].port;
	e->irq = ports[i].pcidev->intl;
	e->tbdf = ports[i].pcidev->tbdf;
	e->mbps = 1000;
	e->ifc.maxmtu = ports[i].rbsz;
	memmove(e->ea, ports[i].ra, Easize);
	e->attach = i82563attach;
	e->transmit = i82563transmit;
	e->interrupt = i82563interrupt;

	return 0;
}

