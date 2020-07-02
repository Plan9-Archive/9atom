/*
 * broadcom bcm57xx bootstrap driver
  */

#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "etherif.h"
#include "ethermii.h"

#define debug		bcmdebug
#define iallocb(x)		allocb(x)
#define	etheriq(e, b, frn)	toringbuf(e, (b)->rp, BLEN(b))
#define	qget(blah)	fromringbuf(edev)

#define dprint(...)	do{ if(debug)print(__VA_ARGS__); }while(0)
#define Rbsz		ROUNDUP(sizeof(Etherpkt)+4, 4)
#define	Pciwaddrl(x)	PCIWADDR(x)
#define Pciwaddrh(x)	(sizeof(uintptr)>4? (uvlong)PCIWADDR(x)>>32: 0)

typedef struct Ctlr Ctlr;
struct Ctlr {
	Lock	txlock, imlock;
	Ether	*ether;
	Ctlr	*next;
	Pcidev	*pdev;
	ulong	*nic, *status;

	ulong	*recvret, *recvprod, *sendr;
	ulong	port;
	ulong	recvreti, recvprodi, sendri, sendcleani;
	Block	**sends;
	Block	**rxs;
	int	active, duplex;

	uint	nobuf;
	uint	partial;
	uint	rxerr;
	uint	qfull;
	uint	dmaerr;
};

enum {
	RecvRetRingLen = 0x200,
	RecvProdRingLen = 0x200,
	SendRingLen = 0x200,

	Reset = 1<<0,
	Enable = 1<<1,
	Attn = 1<<2,
	
	PowerControlStatus = 0x4C,

	MiscHostCtl = 0x68,
	TaggedStatus		= 1<<9,
	IndirectAccessEnable	= 1<<7,
	EnableClockControl	= 1<<5,
	EnablePCIStateRegister	= 1<<4,
	WordSwap		= 1<<3,
	ByteSwap		= 1<<2,
	MaskPCIInt		= 1<<1,
	ClearIntA		= 1<<0,
	
	Fwmbox		= 0x0b50,	/* magic value exchange */
	Fwmagic		= 0x4b657654,

	DMARWControl = 0x6C,
	DMAWatermarkMask = ~(7<<19),
	DMAWatermarkValue = 3<<19,

	MemoryWindow = 0x7C,
	MemoryWindowData = 0x84,
	
	SendRCB = 0x100,
	RecvRetRCB = 0x200,
	
	InterruptMailbox = 0x204,
	
	RecvProdBDRingIndex = 0x26c,
	RecvBDRetRingIndex = 0x284,
	SendBDRingHostIndex = 0x304,
	
	MACMode = 0x400,
	MACPortMask = ~((1<<3)|(1<<2)),
	MACPortGMII = 1<<3,
	MACPortMII = 1<<2,
	MACEnable = (1<<23) | (1<<22) | (1<<21) | (1 << 15) | (1 << 14) | (1<<12) | (1<<11),
	MACHalfDuplex = 1<<1,
	
	MACEventStatus = 0x404,
	MACEventEnable = 0x408,
	MACAddress = 0x410,
	EthernetRandomBackoff = 0x438,
	ReceiveMTU = 0x43C,
	MIComm = 0x44C,
	MIStatus = 0x450,
	MIMode = 0x454,
	ReceiveMACMode = 0x468,
	TransmitMACMode = 0x45C,
	TransmitMACLengths = 0x464,
	MACHash = 0x470,
	ReceiveRules = 0x480,
	
	ReceiveRulesConfiguration = 0x500,
	LowWatermarkMaximum = 0x504,
	LowWatermarkMaxMask = ~0xFFFF,
	LowWatermarkMaxValue = 2,

	SendDataInitiatorMode = 0xC00,
	SendInitiatorConfiguration = 0x0C08,
	SendStats = 1<<0,
	SendInitiatorMask = 0x0C0C,
	
	SendDataCompletionMode = 0x1000,
	SendBDSelectorMode = 0x1400,
	SendBDInitiatorMode = 0x1800,
	SendBDCompletionMode = 0x1C00,
	
	ReceiveListPlacementMode = 0x2000,
	ReceiveListPlacement = 0x2010,
	ReceiveListPlacementConfiguration = 0x2014,
	ReceiveStats = 1<<0,
	ReceiveListPlacementMask = 0x2018,
	
	ReceiveDataBDInitiatorMode = 0x2400,
	ReceiveBDHostAddr = 0x2450,
	ReceiveBDFlags = 0x2458,
	ReceiveBDNIC = 0x245C,
	ReceiveDataCompletionMode = 0x2800,
	ReceiveBDInitiatorMode = 0x2C00,
	ReceiveBDRepl = 0x2C18,
	
	ReceiveBDCompletionMode = 0x3000,
	HostCoalescingMode = 0x3C00,
	HostCoalescingRecvTicks = 0x3C08,
	HostCoalescingSendTicks = 0x3C0C,
	RecvMaxCoalescedFrames = 0x3C10,
	SendMaxCoalescedFrames = 0x3C14,
	RecvMaxCoalescedFramesInt = 0x3C20,
	SendMaxCoalescedFramesInt = 0x3C24,
	StatusBlockHostAddr = 0x3C38,
	FlowAttention = 0x3C48,

	MemArbiterMode = 0x4000,
	
	BufferManMode = 0x4400,
	
	MBUFLowWatermark = 0x4414,
	MBUFHighWatermark = 0x4418,
	
	ReadDMAMode = 0x4800,
	ReadDMAStatus = 0x4804,
	WriteDMAMode = 0x4C00,
	WriteDMAStatus = 0x4C04,
	
	RISCState = 0x5004,
	FTQReset = 0x5C00,
	MSIMode = 0x6000,
	
	ModeControl = 0x6800,
	ByteWordSwap = (1<<4)|(1<<5)|(1<<2),//|(1<<1),
	HostStackUp = 1<<16,
	HostSendBDs = 1<<17,
	InterruptOnMAC = 1<<26,
	
	MiscConfiguration = 0x6804,
	CoreClockBlocksReset = 1<<0,
	GPHYPowerDownOverride = 1<<26,
	DisableGRCResetOnPCIE = 1<<29,
	TimerMask = ~0xFF,
	TimerValue = 65<<1,
	MiscLocalControl = 0x6808,
	InterruptOnAttn = 1<<3,
	AutoSEEPROM = 1<<24,
	
	SwArbitration = 0x7020,
	SwArbitSet1 = 1<<1,
	SwArbitWon1 = 1<<9,
	Pcitlplpl		= 0x7C00,	/* "lower 1k of the pcie pl regs" ?? */

	PhyAuxControl = 0x18,
	PhyIntStatus = 0x1A,
	PhyIntMask = 0x1B,
	
	Updated = 1<<0,
	LinkStateChange = 1<<1,
	Error = 1<<2,
	
	PacketEnd = 1<<2,
	FrameError = 1<<10,
};

#define csr32(c, r)	((c)->nic[(r)/4])

static Ctlr *bcmhead;
static int debug=1;

enum {
	Phybusy		= 1<<29,
	Phyrdfail		= 1<<28,
	Phyrd		= 1<<27,
	Phywr		= 1<<26,
};
Lock miilock;

static uint
miiwait(Ctlr *ctlr)
{
	uint i, v;

	for(i = 0; i < 100; i += 5){
		microdelay(10);
		v = csr32(ctlr, MIComm);
		if((v & Phybusy) == 0){
			microdelay(5);
			return csr32(ctlr, MIComm);
		}
		microdelay(5);
	}
	print("#l%d: bcm: miiwait: timeout\n", ctlr->ether->ctlrno);
	return ~0;
}

static int
miir(Ctlr *ctlr, int r)
{
	uint v, phyno;

	phyno = 1;
	lock(&miilock);
	csr32(ctlr, MIComm) = r<<16 | phyno<<21 | Phyrd | Phybusy;
	v = miiwait(ctlr);
	unlock(&miilock);
	if(v == ~0)
		return -1;
	if(v & Phyrdfail){
		print("#l%d: bcm: miir: fail\n", ctlr->ether->ctlrno);
		return -1;
	}
	return v & 0xffff;
}

static int
miiw(Ctlr *ctlr, int r, int v)
{
	uint phyno, w;

	phyno = 1;
	lock(&miilock);
	csr32(ctlr, MIComm) = r<<16 | v&0xffff | phyno<<21 | Phywr | Phybusy;
	w = miiwait(ctlr);
	unlock(&miilock);
	if(w == ~0)
		return -1;
	return 0;
}

static ulong*
currentrecvret(Ctlr *ctlr)
{
	if(ctlr->recvreti == (ctlr->status[4] & 0xFFFF))
		return 0;
	return ctlr->recvret + ctlr->recvreti * 8;
}

static void
consumerecvret(Ctlr *ctlr)
{
	ctlr->recvreti = ctlr->recvreti+1 & RecvRetRingLen-1;
	csr32(ctlr, RecvBDRetRingIndex) = ctlr->recvreti;
}

static int
replenish(Ctlr *ctlr)
{
	ulong *next, incr;
	Block *bp;
	
	incr = (ctlr->recvprodi + 1) & (RecvProdRingLen - 1);
	if(incr == (ctlr->status[2] >> 16))
		return -1;
	bp = iallocb(Rbsz);
	if(bp == nil) {
		/* iallocb never fails.  this code is unnecessary */
		dprint("bcm: out of memory for receive buffers\n");
		ctlr->nobuf++;
		return -1;
	}
	next = ctlr->recvprod + ctlr->recvprodi * 8;
	memset(next, 0, 32);
	next[0] = Pciwaddrh(bp->rp);
	next[1] = Pciwaddrl(bp->rp);
	next[2] = Rbsz;
	next[7] = ctlr->recvprodi;
	ctlr->rxs[ctlr->recvprodi] = bp;
	coherence();
	csr32(ctlr, RecvProdBDRingIndex) = ctlr->recvprodi = incr;
	return 0;
}

static void
bcmreceive(Ether *edev)
{
	ulong *pkt, len;
	Ctlr *ctlr;
	Block *bp;
	
	ctlr = edev->ctlr;
	for(; pkt = currentrecvret(ctlr); replenish(ctlr), consumerecvret(ctlr)) {
		bp = ctlr->rxs[pkt[7]];
		len = pkt[2] & 0xFFFF;
		bp->wp = bp->rp + len;
		if((pkt[3] & PacketEnd) == 0){
			dprint("bcm: partial frame received -- shouldn't happen\n");
			ctlr->partial++;
			freeb(bp);
			continue;
		}
		if(pkt[3] & FrameError){
			ctlr->rxerr++;
			freeb(bp);
			continue;
		}
		etheriq(edev, bp, 1);
	}
}

static void
bcmtransclean(Ether *edev)
{
	Ctlr *ctlr;
	
	ctlr = edev->ctlr;
	ilock(&ctlr->txlock);
	while(ctlr->sendcleani != (ctlr->status[4] >> 16)) {
		freeb(ctlr->sends[ctlr->sendcleani]);
		ctlr->sends[ctlr->sendcleani] = nil;
		ctlr->sendcleani = (ctlr->sendcleani + 1) & (SendRingLen - 1);
	}
	iunlock(&ctlr->txlock);
}

static void
bcmtransmit(Ether *edev)
{
	ulong *next, incr;
	Ctlr *ctlr;
	Block *bp;
	
	ctlr = edev->ctlr;
	ilock(&ctlr->txlock);
	for(;;){
		incr = (ctlr->sendri + 1) & (SendRingLen - 1);
		if(incr == ctlr->sendcleani) {
			dprint("bcm: send queue full %ld %ld\n", incr, ctlr->sendcleani);
			ctlr->qfull++;
			break;
		}
		bp = qget(edev->oq);
		if(bp == nil)
			break;
		next = ctlr->sendr + ctlr->sendri * 4;
		next[0] = Pciwaddrh(bp->rp);
		next[1] = Pciwaddrl(bp->rp);
		next[2] = (BLEN(bp) << 16) | PacketEnd;
		next[3] = 0;
		ctlr->sends[ctlr->sendri] = bp;
		coherence();
		csr32(ctlr, SendBDRingHostIndex) = ctlr->sendri = incr;
	}
	iunlock(&ctlr->txlock);
}

static void
bcmerror(Ether *edev)
{
	Ctlr *ctlr;
	
	ctlr = edev->ctlr;
	if(csr32(ctlr, FlowAttention)) {
		if(csr32(ctlr, FlowAttention) & 0xf8ff8080)
			print("bcm: fatal error %#.8lux", csr32(ctlr, FlowAttention));
		csr32(ctlr, FlowAttention) = 0;
	}
	csr32(ctlr, MACEventStatus) = 0; /* worth ignoring */
	if(csr32(ctlr, ReadDMAStatus) || csr32(ctlr, WriteDMAStatus)) {
		dprint("bcm: DMA error\n");
		ctlr->dmaerr++;
		csr32(ctlr, ReadDMAStatus) = 0;
		csr32(ctlr, WriteDMAStatus) = 0;
	}
	if(csr32(ctlr, RISCState)) {
		if(csr32(ctlr, RISCState) & 0x78000403)
			print("bcm: RISC halted %#.8lux", csr32(ctlr, RISCState));
		csr32(ctlr, RISCState) = 0;
	}
}

static void
bcminterrupt(Ureg*, void *arg)
{
	ulong status, tag, dummy;
	Ether *edev;
	Ctlr *ctlr;
	
	edev = arg;
	ctlr = edev->ctlr;
	ilock(&ctlr->imlock);
	dummy = csr32(ctlr, InterruptMailbox);
	USED(dummy);
	csr32(ctlr, InterruptMailbox) = 1;
	status = ctlr->status[0];
	tag = ctlr->status[1];
	ctlr->status[0] = 0;
	if(status & Error)
		bcmerror(edev);
	if(status & LinkStateChange)
		{}//checklink(edev);
	bcmreceive(edev);
	bcmtransclean(edev);
	bcmtransmit(edev);
	csr32(ctlr, InterruptMailbox) = tag << 24;
	iunlock(&ctlr->imlock);
}

static void
mem32w(Ctlr *c, uint r, uint v)
{
	pcicfgw32(c->pdev, MemoryWindow, r);
	pcicfgw32(c->pdev, MemoryWindowData, v);
}

static ulong
mem32r(Ctlr *c, uint r)
{
	ulong v;

	pcicfgw32(c->pdev, MemoryWindow, r);
	v = pcicfgr32(c->pdev, MemoryWindowData);
	pcicfgw32(c->pdev, MemoryWindow, 0);
	return v;
}

static int
bcmµwait(Ctlr *ctlr, uint to, uint r, uint m, uint v)
{
	int i;

	for(i = 0;; i += 100){
		if((csr32(ctlr, r) & m) == v)
			return 0;
		if(i == to /* µs */)
			return -1;
		microdelay(100);
	}
}

static int
bcminit(Ether *edev)
{
	ulong i, j;
	Ctlr *ctlr;
	
	ctlr = edev->ctlr;
	dprint("bcm: reset\n");
	/* initialization procedure according to the datasheet */
	csr32(ctlr, MiscHostCtl) |= MaskPCIInt | ClearIntA;
	csr32(ctlr, SwArbitration) |= SwArbitSet1;
	if(bcmµwait(ctlr, 2000, SwArbitration, SwArbitWon1, SwArbitWon1) == -1){
		print("bcm: arbiter failed to respond\n");
		return -1;
	}
	csr32(ctlr, MemArbiterMode) |= Enable;
	csr32(ctlr, MiscHostCtl) = WordSwap | IndirectAccessEnable | EnablePCIStateRegister | EnableClockControl
		| MaskPCIInt | ClearIntA;
	csr32(ctlr, MemoryWindow) = 0;
	mem32w(ctlr, Fwmbox, Fwmagic);
	csr32(ctlr, MiscConfiguration) |= GPHYPowerDownOverride | DisableGRCResetOnPCIE | CoreClockBlocksReset;
	delay(100);
	pcicfgw32(ctlr->pdev, PciPCR, ctlr->pdev->pcr);	/* restore pci bits lost */
	csr32(ctlr, MiscHostCtl) |= MaskPCIInt | ClearIntA;
	csr32(ctlr, MemArbiterMode) |= Enable;
	csr32(ctlr, MiscHostCtl) |= WordSwap | IndirectAccessEnable | EnablePCIStateRegister | EnableClockControl | TaggedStatus;
	csr32(ctlr, ModeControl) |= ByteWordSwap;
	csr32(ctlr, MACMode) = (csr32(ctlr, MACMode) & MACPortMask) | MACPortGMII;
	delay(40);
	for(i = 0;; i += 100){
		if(mem32r(ctlr, Fwmbox) == ~Fwmagic)
			break;
		if(i == 20*10000 /* µs */){
			print("bcm: fw failed to respond %#.8lux\n", mem32r(ctlr, Fwmbox));
			break; //return -1;
		}
		microdelay(100);
	}
	/*
	 * there appears to be no justification for setting these bits in any driver
	 * i can find.  nor to i have a datasheet that recommends this.  - quanstro
	 * csr32(ctlr, Pcitlplpl) |= (1<<25) | (1<<29);
	 */
	memset(ctlr->status, 0, 20);
	csr32(ctlr, DMARWControl) = (csr32(ctlr, DMARWControl) & DMAWatermarkMask) | DMAWatermarkValue;
	csr32(ctlr, ModeControl) |= HostSendBDs | HostStackUp | InterruptOnMAC;
	csr32(ctlr, MiscConfiguration) = (csr32(ctlr, MiscConfiguration) & TimerMask) | TimerValue;
	csr32(ctlr, MBUFLowWatermark) = 0x20;
	csr32(ctlr, MBUFHighWatermark) = 0x60;
	csr32(ctlr, LowWatermarkMaximum) = (csr32(ctlr, LowWatermarkMaximum) & LowWatermarkMaxMask) | LowWatermarkMaxValue;
	csr32(ctlr, BufferManMode) |= Enable | Attn;
	if(bcmµwait(ctlr, 2000, BufferManMode, Enable, Enable) == -1){
		print("bcm: failed to enable buffers\n");
		return -1;
	}
	csr32(ctlr, FTQReset) = ~0;
	csr32(ctlr, FTQReset) = 0;
	if(bcmµwait(ctlr, 2000, FTQReset, ~0, 0) == -1){
		print("bcm: failed to bring ftq out of reset\n");
		return -1;
	}
	csr32(ctlr, ReceiveBDHostAddr) = Pciwaddrh(ctlr->recvprod);
	csr32(ctlr, ReceiveBDHostAddr + 4) = Pciwaddrl(ctlr->recvprod);
	csr32(ctlr, ReceiveBDFlags) = RecvProdRingLen << 16;
	csr32(ctlr, ReceiveBDNIC) = 0x6000;
	csr32(ctlr, ReceiveBDRepl) = 25;
	csr32(ctlr, SendBDRingHostIndex) = 0;
	csr32(ctlr, SendBDRingHostIndex+4) = 0;
	mem32w(ctlr, SendRCB, Pciwaddrh(ctlr->sendr));
	mem32w(ctlr, SendRCB + 4, Pciwaddrl(ctlr->sendr));
	mem32w(ctlr, SendRCB + 8, SendRingLen << 16);
	mem32w(ctlr, SendRCB + 12, 0x4000);
	for(i=1;i<4;i++)
		mem32w(ctlr, RecvRetRCB + i * 0x10 + 8, 2);
	mem32w(ctlr, RecvRetRCB, Pciwaddrh(ctlr->recvret));
	mem32w(ctlr, RecvRetRCB + 4, Pciwaddrl(ctlr->recvret));
	mem32w(ctlr, RecvRetRCB + 8, RecvRetRingLen << 16);
	csr32(ctlr, RecvProdBDRingIndex) = 0;
	csr32(ctlr, RecvProdBDRingIndex+4) = 0;
	/* this delay is not in the datasheet, but necessary */
	delay(1);
	i = csr32(ctlr, MACAddress);
	j = edev->ea[0] = i >> 8;
	j += edev->ea[1] = i;
	i = csr32(ctlr, MACAddress + 4);
	j += edev->ea[2] = i >> 24;
	j += edev->ea[3] = i >> 16;
	j += edev->ea[4] = i >> 8;
	j += edev->ea[5] = i;
	csr32(ctlr, EthernetRandomBackoff) = j & 0x3FF;
	csr32(ctlr, ReceiveMTU) = Rbsz;
	csr32(ctlr, TransmitMACLengths) = 0x2620;
	csr32(ctlr, ReceiveListPlacement) = 1<<3; /* one list */
	csr32(ctlr, ReceiveListPlacementMask) = 0xFFFFFF;
	csr32(ctlr, ReceiveListPlacementConfiguration) |= ReceiveStats;
	csr32(ctlr, SendInitiatorMask) = 0xFFFFFF;
	csr32(ctlr, SendInitiatorConfiguration) |= SendStats;
	csr32(ctlr, HostCoalescingMode) = 0;
	while(csr32(ctlr, HostCoalescingMode) != 0)
		;
	if(bcmµwait(ctlr, 2000, HostCoalescingMode, ~0, 0) == -1){
		print("bcm: failed to unset coalescing\n");
		return -1;
	}
	csr32(ctlr, HostCoalescingRecvTicks) = 150;
	csr32(ctlr, HostCoalescingSendTicks) = 150;
	csr32(ctlr, RecvMaxCoalescedFrames) = 10;
	csr32(ctlr, SendMaxCoalescedFrames) = 10;
	csr32(ctlr, RecvMaxCoalescedFramesInt) = 0;
	csr32(ctlr, SendMaxCoalescedFramesInt) = 0;
	csr32(ctlr, StatusBlockHostAddr) = Pciwaddrh(ctlr->status);
	csr32(ctlr, StatusBlockHostAddr + 4) = Pciwaddrl(ctlr->status);
	csr32(ctlr, HostCoalescingMode) |= Enable;
	csr32(ctlr, ReceiveBDCompletionMode) |= Enable | Attn;
	csr32(ctlr, ReceiveListPlacementMode) |= Enable;
	csr32(ctlr, MACMode) |= MACEnable;
	csr32(ctlr, MiscLocalControl) |= InterruptOnAttn | AutoSEEPROM;
	csr32(ctlr, InterruptMailbox) = 0;
	csr32(ctlr, WriteDMAMode) |= 0x200003fe; /* pulled out of my nose */
	csr32(ctlr, ReadDMAMode) |= 0x3fe;
	csr32(ctlr, ReceiveDataCompletionMode) |= Enable | Attn;
	csr32(ctlr, SendDataCompletionMode) |= Enable;
	csr32(ctlr, SendBDCompletionMode) |= Enable | Attn;
	csr32(ctlr, ReceiveBDInitiatorMode) |= Enable | Attn;
	csr32(ctlr, ReceiveDataBDInitiatorMode) |= Enable | (1<<4);
	csr32(ctlr, SendDataInitiatorMode) |= Enable;
	csr32(ctlr, SendBDInitiatorMode) |= Enable | Attn;
	csr32(ctlr, SendBDSelectorMode) |= Enable | Attn;
	ctlr->recvprodi = 0;
	while(replenish(ctlr) >= 0)
		;
	csr32(ctlr, TransmitMACMode) |= Enable;
	csr32(ctlr, ReceiveMACMode) |= Enable;
	csr32(ctlr, PowerControlStatus) &= ~3;
	csr32(ctlr, MIStatus) |= 1<<0;
	csr32(ctlr, MACEventEnable) = 0;
	csr32(ctlr, MACEventStatus) |= (1<<12);
	csr32(ctlr, MIMode) = 0xC0000;		/* set base mii clock */
	microdelay(40);

	if(0){
		/* bug (ours): can't reset phy without dropping into 100mbit mode */
		miiw(ctlr, Bmcr, BmcrR);
		for(i = 0;; i += 100){
			if((miir(ctlr, Bmcr) & BmcrR) == 0)
				break;
			if(i == 10000 /* µs */){
				print("bcm: phy reset failure\n");
				return -1;
			}
			microdelay(100);
		}
	}
	miiw(ctlr, Bmcr, BmcrAne | BmcrRan);

	miiw(ctlr, PhyAuxControl, 2);
	miir(ctlr, PhyIntStatus);
	miir(ctlr, PhyIntStatus);
	miiw(ctlr, PhyIntMask, ~(1<<1));
	csr32(ctlr, MACEventEnable) |= 1<<12;
	for(i = 0; i < 4; i++)
		csr32(ctlr, MACHash + 4*i) = ~0;
	for(i = 0; i < 8; i++)
		csr32(ctlr, ReceiveRules + 8 * i) = 0;
	csr32(ctlr, ReceiveRulesConfiguration) = 1 << 3;
	csr32(ctlr, MSIMode) |= Enable;
	csr32(ctlr, MiscHostCtl) &= ~(MaskPCIInt | ClearIntA);
	dprint("bcm: reset: fin\n");
	return 0;
}

static void
bcmpci(void)
{
	ulong mem;
	Ctlr *ctlr, **xx;
	Pcidev *p;

	xx = &bcmhead;
	for(p = nil; p = pcimatch(p, 0, 0); ) {
		if(p->ccrb != 2 || p->ccru != 0)
			continue;
		
		switch(p->vid<<16 | p->did){
		default:
			continue;
		case 0x14e4165a:
		case 0x14e4167d:
		case 0x14e41670:
		case 0x14e41672:
		case 0x14e41673:
		case 0x14e41674:
		case 0x14e41677:
		case 0x14e4167a:
		case 0x14e4167b:
		case 0x14e41693:
		case 0x14e41696:		/* BCM4313 BCM5782; steve */
		case 0x14e4169b:
		case 0x14e41712:
		case 0x14e41713:
			break;
		}
		pcisetbme(p);
		pcisetpms(p, 0);
		ctlr = malloc(sizeof(Ctlr));
		if(ctlr == nil)
			continue;
		ctlr->port = p->mem[0].bar & ~0x0F;
		mem = upamalloc(ctlr->port, p->mem[0].size, 0);
		if(mem == 0) {
			print("bcm: can't map %#p\n", ctlr->port);
			free(ctlr);
			continue;
		}
		ctlr->pdev = p;
		ctlr->nic = KADDR(mem);
		ctlr->status = xspanalloc(20, 16, 0);
		ctlr->recvprod = xspanalloc(32 * RecvProdRingLen, 16, 0);
		ctlr->recvret = xspanalloc(32 * RecvRetRingLen, 16, 0);
		ctlr->sendr = xspanalloc(16 * SendRingLen, 16, 0);
		ctlr->sends = malloc(sizeof *ctlr->sends * SendRingLen);
		ctlr->rxs = malloc(sizeof *ctlr->sends * SendRingLen);
		*xx = ctlr;
		xx = &ctlr->next;
	}
}

int
bcmpnp(Ether* edev)
{
	Ctlr *ctlr;
	static int done;

	if(done == 0){
		bcmpci();
		done = 1;
	}
	
redux:
	for(ctlr = bcmhead; ; ctlr = ctlr->next) {
		if(ctlr == nil)
			return -1;
		if(ctlr->active)
			continue;
		if(edev->port == 0 || edev->port == ctlr->port) {
			ctlr->active = 1;
			break;
		}
	}

	ctlr->ether = edev;
	edev->ctlr = ctlr;
	edev->port = ctlr->port;
	edev->irq = ctlr->pdev->intl;
	edev->tbdf = ctlr->pdev->tbdf;
	edev->interrupt = bcminterrupt;
	edev->transmit = bcmtransmit;
	edev->mbps = 1000;

	if(bcminit(edev) == -1)
		goto redux;
	return 0;
}
