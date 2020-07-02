/*
 * ata disk driver for file server.
 * derived from /sys/src/boot/pc/sdata.c and /sys/src/9/pc/sdata.c
 *
 * we can't write message into a ctl file on the file server, so
 * enable dma and rwm as advertised by the drive & controller.
 * if that doesn't work, fix the hardware or turn it off in the source
 * (set conf.idedma = 0).
 *
 */
#include "all.h"
#include "io.h"
#include "mem.h"

#define	dprint(...)	// print(__VA_ARGS__)
#define idprint(...)	// print(__VA_ARGS__)


#define	HOWMANY(x, y)	(((x)+((y)-1))/(y))
#define ROUNDUP(x, y)	(HOWMANY((x), (y))*(y))

enum{
	IrqATA0	= 14,
	IrqATA1	= 15,
};

enum {
	NCtlr=		8,
	NCtlrdrv=	2,		/* fixed by hardware */
	NDrive=		NCtlr*NCtlrdrv,
	Maxxfer=	16*1024,	/* maximum transfer size/cmd */

	Read = 0,
	Write,

	/* I/O ports */
	Ctlr0cmd =	0x1f0,
	Ctlr0ctl =	0x3f4,
	Ctlr1cmd =	0x170,
	Ctlr1ctl =	0x374,
	Ctl2cmd = Ctlr0ctl - Ctlr0cmd,

	Ok	= 0,
	Check	= 1,
	Retry	= 2,
	Timeout	= 3,
};

enum {
	DbgCONFIG	= 0x0001,	/* detected drive config info */
	DbgIDENTIFY	= 0x0002,	/* detected drive identify info */
	DbgSTATE	= 0x0004,	/* dump state on panic */
	DbgPROBE	= 0x0008,	/* trace device probing */
	DbgDEBUG	= 0x0080,	/* the current problem... */
	DbgINL		= 0x0100,	/* That Inil20+ message we hate */
	Dbg48BIT	= 0x0200,	/* 48-bit LBA */
	DbgBsy		= 0x0400,	/* interrupt but Bsy (shared IRQ) */
};

/* adjust to taste */
#define DEBUG		(DbgDEBUG|DbgCONFIG)

enum {					/* I/O ports */
	Data		= 0,
	Error		= 1,		/* (read) */
	Features	= 1,		/* (write) */
	Count		= 2,		/* sector count<7-0>, sector count<15-8> */
	Ir		= 2,		/* interrupt reason (PACKET) */
	Sector		= 3,		/* sector number */
	Lbalo		= 3,		/* LBA<7-0>, LBA<31-24> */
	Cyllo		= 4,		/* cylinder low */
	Bytelo		= 4,		/* byte count low (PACKET) */
	Lbamid		= 4,		/* LBA<15-8>, LBA<39-32> */
	Cylhi		= 5,		/* cylinder high */
	Bytehi		= 5,		/* byte count hi (PACKET) */
	Lbahi		= 5,		/* LBA<23-16>, LBA<47-40> */
	Dh		= 6,		/* Device/Head, LBA<32-14> */
	Status		= 7,		/* (read) */
	Cmd		= 7,		/* (write) */

	As		= 2,		/* Alternate Status (read) */
	Dc		= 2,		/* Device Control (write) */
};

enum {					/* Error */
	Med		= 0x01,		/* Media error */
	Ili		= 0x01,		/* command set specific (PACKET) */
	Nm		= 0x02,		/* No Media */
	Eom		= 0x02,		/* command set specific (PACKET) */
	Abrt		= 0x04,		/* Aborted command */
	Mcr		= 0x08,		/* Media Change Request */
	Idnf		= 0x10,		/* no user-accessible address */
	Mc		= 0x20,		/* Media Change */
	Unc		= 0x40,		/* Uncorrectable data error */
	Wp		= 0x40,		/* Write Protect */
	Icrc		= 0x80,		/* Interface CRC error */
};

enum {					/* Features */
	Dma		= 0x01,		/* data transfer via DMA (PACKET) */
	Ovl		= 0x02,		/* command overlapped (PACKET) */
};

enum {					/* Interrupt Reason */
	Cd		= 0x01,		/* Cmd/Data */
	Io		= 0x02,		/* I/O direction */
	Rel		= 0x04,		/* Bus Release */
};

enum {					/* Device/Head */
	Dev0		= 0xA0,		/* Master */
	Dev1		= 0xB0,		/* Slave */
	Lba		= 0x40,		/* LBA mode */
};

enum {					/* internal flags */
	Dllba		= 0x1,		/* LBA48 mode */
};

enum {					/* Status, Alternate Status */
	Err		= 0x01,		/* Error */
	Chk		= 0x01,		/* Check error (PACKET) */
	Drq		= 0x08,		/* Data Request */
	Dsc		= 0x10,		/* Device Seek Complete */
	Serv		= 0x10,		/* Service */
	Df		= 0x20,		/* Device Fault */
	Dmrd		= 0x20,		/* DMA ready (PACKET) */
	Drdy		= 0x40,		/* Device Ready */
	Bsy		= 0x80,		/* Busy */
};

enum {					/* Cmd */
	Cnop		= 0x00,		/* NOP */
	Cdr		= 0x08,		/* Device Reset */
	Crs		= 0x20,		/* Read Sectors */
	Crs48		= 0x24,		/* Read Sectors Ext */
	Crd48		= 0x25,		/* Read w/ DMA Ext */
	Crdq48		= 0x26,		/* Read w/ DMA Queued Ext */
	Crsm48		= 0x29,		/* Read Multiple Ext */
	Cws		= 0x30,		/* Write Sectors */
	Cws48		= 0x34,		/* Write Sectors Ext */
	Cwd48		= 0x35,		/* Write w/ DMA Ext */
	Cwdq48		= 0x36,		/* Write w/ DMA Queued Ext */
	Cwsm48		= 0x39,		/* Write Multiple Ext */
	Cedd		= 0x90,		/* Execute Device Diagnostics */
	Cpkt		= 0xA0,		/* Packet */
	Cidpkt		= 0xA1,		/* Identify Packet Device */
	Crsm		= 0xC4,		/* Read Multiple */
	Cwsm		= 0xC5,		/* Write Multiple */
	Csm		= 0xC6,		/* Set Multiple */
	Crdq		= 0xC7,		/* Read DMA queued */
	Crd		= 0xC8,		/* Read DMA */
	Cwd		= 0xCA,		/* Write DMA */
	Cwdq		= 0xCC,		/* Write DMA queued */
	Cstandby	= 0xE2,		/* Standby */
	Cid		= 0xEC,		/* Identify Device */
	Csf		= 0xEF,		/* Set Features */
};

enum {					/* Device Control */
	Nien		= 0x02,		/* (not) Interrupt Enable */
	Srst		= 0x04,		/* Software Reset */
	Hob		= 0x80,		/* High Order Bit [sic] */
};

enum {					/* PCI Configuration Registers */
	Bmiba		= 0x20,		/* Bus Master Interface Base Address */
	Idetim		= 0x40,		/* IE Timing */
	Sidetim		= 0x44,		/* Slave IE Timing */
	Udmactl		= 0x48,		/* Ultra DMA/33 Control */
	Udmatim		= 0x4A,		/* Ultra DMA/33 Timing */
};

enum {					/* Bus Master IDE I/O Ports */
	Bmicx		= 0,		/* Cmd */
	Bmisx		= 2,		/* Status */
	Bmidtpx		= 4,		/* Descriptor Table Pointer */
};

enum {					/* Bmicx */
	Ssbm		= 0x01,		/* Start/Stop Bus Master */
	Rwcon		= 0x08,		/* Read/Write Control */
};

enum {					/* Bmisx */
	Bmidea		= 0x01,		/* Bus Master IDE Active */
	Idedmae		= 0x02,		/* IDE DMA Error  (R/WC) */
	Ideints		= 0x04,		/* IDE Interrupt Status (R/WC) */
	Dma0cap		= 0x20,		/* Drive 0 DMA Capable */
	Dma1cap		= 0x40,		/* Drive 0 DMA Capable */
};
enum {					/* Physical Region Descriptor */
	PrdEOT		= 0x80000000,	/* Bus Master IDE Active */
};

enum {					/* offsets into the identify info. */
	Iconfig		= 0,		/* general configuration */
	Ilcyl		= 1,		/* logical cylinders */
	Ilhead		= 3,		/* logical heads */
	Ilsec		= 6,		/* logical sectors per logical track */
	Iserial		= 10,		/* serial number */
	Ifirmware	= 23,		/* firmware revision */
	Imodel		= 27,		/* model number */
	Imaxrwm		= 47,		/* max. read/write multiple sectors */
	Icapabilities	= 49,		/* capabilities */
	Istandby	= 50,		/* device specific standby timer */
	Ipiomode	= 51,		/* PIO data transfer mode number */
	Ivalid		= 53,
	Iccyl		= 54,		/* cylinders if (valid&0x01) */
	Ichead		= 55,		/* heads if (valid&0x01) */
	Icsec		= 56,		/* sectors if (valid&0x01) */
	Iccap		= 57,		/* capacity if (valid&0x01) */
	Irwm		= 59,		/* read/write multiple */
	Ilba		= 60,		/* LBA size */
	Imwdma		= 63,		/* multiword DMA mode */
	Iapiomode	= 64,		/* advanced PIO modes supported */
	Iminmwdma	= 65,		/* min. multiword DMA cycle time */
	Irecmwdma	= 66,		/* rec. multiword DMA cycle time */
	Iminpio		= 67,		/* min. PIO cycle w/o flow control */
	Iminiordy	= 68,		/* min. PIO cycle with IORDY */
	Ipcktbr		= 71,		/* time from PACKET to bus release */
	Iserbsy		= 72,		/* time from SERVICE to !Bsy */
	Iqdepth		= 75,		/* max. queue depth */
	Imajor		= 80,		/* major version number */
	Iminor		= 81,		/* minor version number */
	Icsfs		= 82,		/* command set/feature supported */
	Icsfe		= 85,		/* command set/feature enabled */
	Iudma		= 88,		/* ultra DMA mode */
	Ierase		= 89,		/* time for security erase */
	Ieerase		= 90,		/* time for enhanced security erase */
	Ipower		= 91,		/* current advanced power management */
	Ilba48		= 100,		/* 48-bit LBA size (64 bits in 100-103) */
	Irmsn		= 127,		/* removable status notification */
	Isecstat	= 128,		/* security status */
	Icfapwr		= 160,		/* CFA power mode */
	Imediaserial	= 176,		/* current media serial number */
	Icksum		= 255,		/* checksum */
};

enum {					/* bit masks for config identify info */
	Mpktsz		= 0x0003,	/* packet command size */
	Mincomplete	= 0x0004,	/* incomplete information */
	Mdrq		= 0x0060,	/* DRQ type */
	Mrmdev		= 0x0080,	/* device is removable */
	Mtype		= 0x1F00,	/* device type */
	Mproto		= 0x8000,	/* command protocol */
};

enum {					/* bit masks for capabilities identify info */
	Mdma		= 0x0100,	/* DMA supported */
	Mlba		= 0x0200,	/* LBA supported */
	Mnoiordy	= 0x0400,	/* IORDY may be disabled */
	Miordy		= 0x0800,	/* IORDY supported */
	Msoftrst	= 0x1000,	/* needs soft reset when Bsy */
	Mstdby		= 0x2000,	/* standby supported */
	Mqueueing	= 0x4000,	/* queueing overlap supported */
	Midma		= 0x8000,	/* interleaved DMA supported */
};

enum {					/* bit masks for supported/enabled features */
	Msmart		= 0x0001,
	Msecurity	= 0x0002,
	Mrmmedia	= 0x0004,
	Mpwrmgmt	= 0x0008,
	Mpkt		= 0x0010,
	Mwcache		= 0x0020,
	Mlookahead	= 0x0040,
	Mrelirq		= 0x0080,
	Msvcirq		= 0x0100,
	Mreset		= 0x0200,
	Mprotected	= 0x0400,
	Mwbuf		= 0x1000,
	Mrbuf		= 0x2000,
	Mnop		= 0x4000,
	Mmicrocode	= 0x0001,
	Mqueued		= 0x0002,
	Mcfa		= 0x0004,
	Mapm		= 0x0008,
	Mnotify		= 0x0010,
	Mstandby	= 0x0020,
	Mspinup		= 0x0040,
	Mmaxsec		= 0x0100,
	Mautoacoustic	= 0x0200,
	Maddr48		= 0x0400,
	Mdevconfov	= 0x0800,
	Mflush		= 0x1000,
	Mflush48	= 0x2000,
	Msmarterror	= 0x0001,
	Msmartselftest	= 0x0002,
	Mmserial	= 0x0004,
	Mmpassthru	= 0x0008,
	Mlogging	= 0x0020,
};

typedef struct Ctlr Ctlr;
typedef struct Drive Drive;

typedef struct Prd {
	ulong	pa;			/* Physical Base Address */
	int	count;
} Prd;

enum {
	BMspan		= 64*1024,	/* must be power of 2 <= 64*1024 */
	Nprd		= 512*1024/BMspan+2,
};

typedef struct Ctlr {
	int	cmdport;
	int	ctlport;
	int	irq;
	int	tbdf;
	int	bmiba;			/* bus master interface base address */
	int	maxio;			/* sector count transfer maximum */
	int	span;			/* don't span this boundary with dma */

	Pcidev*	pcidev;
	void	(*ienable)(Ctlr*);

	Drive*	drive[NCtlrdrv];

	Prd*	prdt;			/* physical region descriptor table */
	void*	prdtbase;

	QLock;				/* current command */
	Drive*	curdrive;
	int	command;		/* last command issued (debugging) */
	Rendez;
	int	done;
	char	name[10];
	Lock;				/* register access */
} Ctlr;

typedef struct Drive {
	Ctlr*	ctlr;

	int	dev;
	ushort	info[256];
	int	c;			/* cylinder */
	int	h;			/* head */
	int	s;			/* sector */
	uvlong	sectors;		/* total sectors */
	int	secsize;		/* sector size */

	int	dma;			/* DMA R/W possible */
	int	dmactl;
	int	rwm;			/* read/write multiple possible */
	int	rwmctl;

	int	pkt;			/* PACKET device, length of pktcmd */
	uchar	pktcmd[16];
	int	pktdma;			/* this PACKET command using dma */

	QLock;				/* drive access */
	int	command;		/* current command */
	int	write;
	uchar*	data;
	int	dlen;
	uchar*	limit;
	int	count;			/* sectors */
	int	block;			/* R/W bytes per block */
	int	status;
	int	error;
	int	flags;			/* internal flags */

	int	driveno;		/* ctlr*NCtlrdrv + unit */
	uchar	lba;		/* true if drive has logical block addressing */
	uchar	online;

	Filter	rate[2];
	int	fflag;
} Drive;

/* file-server-specific data */

static Ctlr *atactlr[NCtlr];
static int	natactlr;
static Drive *atadrive[NDrive];
static Drive	*atadriveprobe(int driveno);

void
presleep(Rendez *r, int (*fn)(void*), void *v)
{
	int x;

	if (u != nil) {
		sleep(r, fn, v);
		return;
	}
	/* else we're in predawn with no u */
	x = spllo();
	while (!fn(v))
		continue;
	splx(x);
}

void
pretsleep(Rendez *r, int (*fn)(void*), void *v, int msec)
{
	int x;
	ulong start;

	if (u != nil) {
		tsleep(r, fn, v, msec);
		return;
	}
	/* else we're in predawn with no u */
	x = spllo();
	for (start = m->ticks; TK2MS(Ticks - start) < msec && !fn(v); )
		continue;
	splx(x);
}

#define sleep	presleep
#define tsleep	pretsleep

static void
pc87415ienable(Ctlr* ctlr)
{
	Pcidev *p;
	int x;

	p = ctlr->pcidev;
	if(p == nil)
		return;

	x = pcicfgr32(p, 0x40);
	if(ctlr->cmdport == p->mem[0].bar)
		x &= ~0x00000100;
	else
		x &= ~0x00000200;
	pcicfgw32(p, 0x40, x);
}

static void
atadumpstate(Drive* drive, Devsize lba, int count)
{
	Prd *prd;
	Pcidev *p;
	Ctlr *ctlr;
	int i, bmiba;

	if(!(DEBUG & DbgSTATE)){
		USED(drive, lba, count);
		return;
	}

	ctlr = drive->ctlr;
	print("command %2.2uX\n", ctlr->command);
	print("data %8.8p limit %8.8p dlen %d status %uX error %uX\n",
		drive->data, drive->limit, drive->dlen,
		drive->status, drive->error);
	print("lba %lld, count %d -> %d\n",
			(Wideoff)lba, count, drive->count);
	if(!(inb(ctlr->ctlport+As) & Bsy)){
		for(i = 1; i < 7; i++)
			print(" 0x%2.2uX", inb(ctlr->cmdport+i));
		print(" 0x%2.2uX\n", inb(ctlr->ctlport+As));
	}
	if(drive->command == Cwd || drive->command == Crd){
		bmiba = ctlr->bmiba;
		prd = ctlr->prdt;
		print("bmicx %2.2uX bmisx %2.2uX prdt %8.8p\n",
			inb(bmiba+Bmicx), inb(bmiba+Bmisx), prd);
		for(;;){
			print("pa 0x%8.8luX count %8.8uX\n",
				prd->pa, prd->count);
			if(prd->count & PrdEOT)
				break;
			prd++;
		}
	}
	if(ctlr->pcidev && ctlr->pcidev->vid == 0x8086){
		p = ctlr->pcidev;
		print("0x40: %4.4uX 0x42: %4.4uX",
			pcicfgr16(p, 0x40), pcicfgr16(p, 0x42));
		print("0x48: %2.2uX\n", pcicfgr8(p, 0x48));
		print("0x4A: %4.4uX\n", pcicfgr16(p, 0x4A));
	}
}

static int
atadebug(int cmdport, int ctlport, char* fmt, ...)
{
	int i, n;
	va_list arg;
	char buf[PRINTSIZE];

	if(!(DEBUG & DbgPROBE)){
		USED(cmdport, ctlport, fmt);
		return 0;
	}

	va_start(arg, fmt);
	n = vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);

	if(cmdport){
		if(buf[n-1] == '\n')
			n--;
		n += snprint(buf+n, PRINTSIZE-n, " ataregs 0x%uX:",
			cmdport);
		for(i = Features; i < Cmd; i++)
			n += snprint(buf+n, PRINTSIZE-n, " 0x%2.2uX",
				inb(cmdport+i));
		if(ctlport)
			n += snprint(buf+n, PRINTSIZE-n, " 0x%2.2uX",
				inb(ctlport+As));
		n += snprint(buf+n, PRINTSIZE-n, "\n");
	}
	putstrn(buf, n);

	return n;
}

static int
ataready(int cmdport, int ctlport, int dev, int reset, int ready, int micro)
{
	int as;

	atadebug(cmdport, ctlport, "ataready: dev %uX reset %uX ready %uX",
		dev, reset, ready);

	for(;;){
		/*
		 * Wait for the controller to become not busy and
		 * possibly for a status bit to become true (usually
		 * Drdy). Must change to the appropriate device
		 * register set if necessary before testing for ready.
		 * Always run through the loop at least once so it
		 * can be used as a test for !Bsy.
		 */
		as = inb(ctlport+As);
		if(as & reset){
			/* nothing to do */
		}
		else if(dev){
			outb(cmdport+Dh, dev);
			dev = 0;
		}
		else if(ready == 0 || (as & ready)){
			atadebug(0, 0, "ataready: %d 0x%2.2uX\n", micro, as);
			return as;
		}

		if(micro-- <= 0){
			atadebug(0, 0, "ataready: %d 0x%2.2uX\n", micro, as);
			break;
		}
		microdelay(1);
	}
	atadebug(cmdport, ctlport, "ataready: timeout");

	return -1;
}

static int
atadone(void* arg)
{
	return ((Ctlr*)arg)->done;
}

static int
atarwmmode(Drive* drive, int cmdport, int ctlport, int dev)
{
	int as, maxrwm, rwm;

	maxrwm = (drive->info[Imaxrwm] & 0xFF);
	if(maxrwm == 0)
		return 0;

	/*
	 * Sometimes drives come up with the current count set
	 * to 0; if so, set a suitable value, otherwise believe
	 * the value in Irwm if the 0x100 bit is set.
	 */
	if(drive->info[Irwm] & 0x100)
		rwm = (drive->info[Irwm] & 0xFF);
	else
		rwm = 0;
	if(rwm == 0)
		rwm = maxrwm;
	if(rwm > 16)
		rwm = 16;
	if(ataready(cmdport, ctlport, dev, Bsy|Drq, Drdy, 102*1000) < 0)
		return 0;
	outb(cmdport+Count, rwm);
	outb(cmdport+Cmd, Csm);
	microdelay(1);
	as = ataready(cmdport, ctlport, 0, Bsy, Drdy|Df|Err, 1000);
	inb(cmdport+Status);
	if(as < 0 || (as & (Df|Err)))
		return 0;

	drive->rwm = rwm;
	if (conf.idedma)
		drive->rwmctl = drive->rwm;	/* FS special */
	else
		drive->rwm = 0;
	return rwm;
}

static int
atadmamode(Drive* drive)
{
	int dma;

	/*
	 * Check if any DMA mode enabled.
	 * Assumes the BIOS has picked and enabled the best.
	 * This is completely passive at the moment, no attempt is
	 * made to ensure the hardware is correctly set up.
	 */
	dma = drive->info[Imwdma] & 0x0707;
	drive->dma = (dma>>8) & dma;
	if(drive->dma == 0 && (drive->info[Ivalid] & 0x04)){
		dma = drive->info[Iudma] & 0x7F7F;
		drive->dma = (dma>>8) & dma;
		if(drive->dma)
			drive->dma |= 'U'<<16;
	}
	if (conf.idedma)
		drive->dmactl = drive->dma;	/* FS special */
	else
		drive->dma = 0;
	return dma;
}

static int
ataidentify(int cmdport, int ctlport, int dev, int pkt, void* info)
{
	int as, command, drdy;

	if(pkt){
		command = Cidpkt;
		drdy = 0;
	}
	else{
		command = Cid;
		drdy = Drdy;
	}
	as = ataready(cmdport, ctlport, dev, Bsy|Drq, drdy, 103*1000);
	if(as < 0)
		return as;
	outb(cmdport+Cmd, command);
	microdelay(1);

	as = ataready(cmdport, ctlport, 0, Bsy, Drq|Err, 400*1000);
	if(as < 0)
		return -1;
	if(as & Err)
		return as;

	memset(info, 0, 512);
	inss(cmdport+Data, info, 256);
	inb(cmdport+Status);

	return 0;
}

static Drive*
atagetdrive(int cmdport, int ctlport, int dev)
{
	Drive *drive;
	int as, pkt, driveno;
	uchar buf[512];
	ushort iconfig;

	driveno = (cmdport == Ctlr0cmd? 0:
		cmdport == Ctlr1cmd? NCtlrdrv: 2*NCtlrdrv);
	if (dev == Dev1)
		driveno++;

	atadebug(0, 0, "identify: port 0x%uX dev 0x%2.2uX\n", cmdport, dev);
	pkt = 1;
retry:
	as = ataidentify(cmdport, ctlport, dev, pkt, buf);
	if(as < 0)
		return nil;
	if(as & Err){
		if(pkt == 0)
			return nil;
		pkt = 0;
		goto retry;
	}

	if((drive = ialloc(sizeof(Drive), 0)) == nil)
		return nil;
	drive->dev = dev;
	drive->driveno = -1;				/* unset */
	memmove(drive->info, buf, sizeof drive->info);

	drive->secsize = 512;

	/*
	 * Beware the CompactFlash Association feature set.
	 * Now, why this value in Iconfig just walks all over the bit
	 * definitions used in the other parts of the ATA/ATAPI standards
	 * is a mystery and a sign of true stupidity on someone's part.
	 * Anyway, the standard says if this value is 0x848A then it's
	 * CompactFlash and it's NOT a packet device.
	 */
	iconfig = drive->info[Iconfig];
	if(iconfig != 0x848A && (iconfig & 0xC000) == 0x8000){
		print("atagetdrive: port 0x%uX dev 0x%2.2uX: packet device\n",
			cmdport, dev);
		if(iconfig & 0x01)
			drive->pkt = 16;
		else
			drive->pkt = 12;
	}
	else{
		if (iconfig == 0x848A)
			print("atagetdrive: port 0x%uX dev 0x%2.2uX: non-packet CF device\n",
				cmdport, dev);
		if(drive->info[Ivalid] & 0x0001){
			drive->c = drive->info[Iccyl];
			drive->h = drive->info[Ichead];
			drive->s = drive->info[Icsec];
		}else{
			drive->c = drive->info[Ilcyl];
			drive->h = drive->info[Ilhead];
			drive->s = drive->info[Ilsec];
		}
		if(drive->info[Icapabilities] & Mlba){
			if(drive->info[Icsfs+1] & Maddr48){
				drive->sectors = drive->info[Ilba48]
					| (drive->info[Ilba48+1]<<16)
					| ((Devsize)drive->info[Ilba48+2]<<32);
				drive->flags |= Dllba;
				if(DEBUG & Dbg48BIT)
					print("LLBA48 drive @%.2x:%.3x\n", dev, cmdport);
			}else
				drive->sectors = (drive->info[Ilba+1]<<16)
					 |drive->info[Ilba];
			drive->dev |= Lba;
			drive->lba = 1;
		}else
			drive->sectors = drive->c * drive->h * drive->s;
		atarwmmode(drive, cmdport, ctlport, dev);
	}
	atadmamode(drive);

	if(DEBUG & DbgCONFIG){
		print("ata h%d: dev %2.2uX port %uX config %4.4uX capabilities %4.4uX",
			driveno, dev, cmdport, iconfig,
			drive->info[Icapabilities]);
		print(" mwdma %4.4uX", drive->info[Imwdma]);
		if(drive->info[Ivalid] & 0x04)
			print(" udma %4.4uX", drive->info[Iudma]);
		print(" dma %8.8uX rwm %ud\n", drive->dma, drive->rwm);
		if(drive->flags&Dllba)
			print("\tLLBA sectors %lld\n", (Wideoff)drive->sectors);
	}
	return drive;
}

static void
atasrst(int ctlport)
{
	/*
	 * Srst is a big stick and may cause problems if further
	 * commands are tried before the drives become ready again.
	 * Also, there will be problems here if overlapped commands
	 * are ever supported.
	 */
	microdelay(5);
	outb(ctlport+Dc, Srst);
	microdelay(5);
	outb(ctlport+Dc, 0);
	microdelay(2*1000);
}

static int drivenum = 0;	/* hope that we probe in order */

static void
updprobe(int cmdport)
{
	if(cmdport == Ctlr0cmd)
		drivenum = NCtlrdrv;
	else if (cmdport == Ctlr1cmd)
		drivenum = 2*NCtlrdrv;
}

static Ctlr *
ataprobe(int cmdport, int ctlport, int irq)
{
	Ctlr* ctlr;
	Drive *drive;
	int dev, error, rhi, rlo;

	if(cmdport == Ctlr0cmd)
		drivenum = 0;
	else if (cmdport == Ctlr1cmd)
		drivenum = NCtlrdrv;

	/*
	 * Try to detect a floating bus.
	 * Bsy should be cleared. If not, see if the cylinder registers
	 * are read/write capable.
	 * If the master fails, try the slave to catch slave-only
	 * configurations.
	 * There's no need to restore the tested registers as they will
	 * be reset on any detected drives by the Cedd command.
	 * All this indicates is that there is at least one drive on the
	 * controller; when the non-existent drive is selected in a
	 * single-drive configuration the registers of the existing drive
	 * are often seen, only command execution fails.
	 */
	dev = Dev0;
	if(inb(ctlport+As) & Bsy){
		outb(cmdport+Dh, dev);
		microdelay(1);
trydev1:
		atadebug(cmdport, ctlport, "ataprobe bsy");
		outb(cmdport+Cyllo, 0xAA);
		outb(cmdport+Cylhi, 0x55);
		outb(cmdport+Sector, 0xFF);
		rlo = inb(cmdport+Cyllo);
		rhi = inb(cmdport+Cylhi);
		if(rlo != 0xAA && (rlo == 0xFF || rhi != 0x55)){
			if(dev == Dev1){
release:
//				iofree(cmdport);
//				iofree(ctlport+As);
				updprobe(cmdport);
				return nil;
			}
			dev = Dev1;
			if(ataready(cmdport, ctlport, dev, Bsy, 0, 20*1000) < 0)
				goto trydev1;
		}
	}

	/*
	 * Disable interrupts on any detected controllers.
	 */
	outb(ctlport+Dc, Nien);
tryedd1:
	if(ataready(cmdport, ctlport, dev, Bsy|Drq, 0, 105*1000) < 0){
		/*
		 * There's something there, but it didn't come up clean,
		 * so try hitting it with a big stick. The timing here is
		 * wrong but this is a last-ditch effort and it sometimes
		 * gets some marginal hardware back online.
		 */
		atasrst(ctlport);
		if(ataready(cmdport, ctlport, dev, Bsy|Drq, 0, 106*1000) < 0)
			goto release;
	}

	/*
	 * Can only get here if controller is not busy.
	 * If there are drives Bsy will be set within 400nS,
	 * must wait 2mS before testing Status.
	 * Wait for the command to complete (6 seconds max).
	 */
	outb(cmdport+Cmd, Cedd);
	delay(2);
	if(ataready(cmdport, ctlport, dev, Bsy|Drq, 0, 6*1000*1000) < 0)
		goto release;

	/*
	 * If bit 0 of the error register is set then the selected drive
	 * exists. This is enough to detect single-drive configurations.
	 * However, if the master exists there is no way short of executing
	 * a command to determine if a slave is present.
	 * It appears possible to get here testing Dev0 although it doesn't
	 * exist and the EDD won't take, so try again with Dev1.
	 */
	error = inb(cmdport+Error);
	atadebug(cmdport, ctlport, "ataprobe: dev %uX", dev);
	if((error & ~0x80) != 0x01){
		if(dev == Dev1)
			goto release;
		dev = Dev1;
		goto tryedd1;
	}

	/*
	 * At least one drive is known to exist, try to
	 * identify it. If that fails, don't bother checking
	 * any further.
	 * If the one drive found is Dev0 and the EDD command
	 * didn't indicate Dev1 doesn't exist, check for it.
	 */
	if((drive = atagetdrive(cmdport, ctlport, dev)) == nil)
		goto release;
	if((ctlr = ialloc(sizeof(Ctlr), 0)) == nil){
//		free(drive);
		goto release;
	}
	drive->ctlr = ctlr;

	atactlr[drivenum/NCtlrdrv] = ctlr;
	natactlr++;
	atadrive[drivenum++] = drive;

	if(dev == Dev0){
		ctlr->drive[0] = drive;
		if(!(error & 0x80)){
			/*
			 * Always leave Dh pointing to a valid drive,
			 * otherwise a subsequent call to ataready on
			 * this controller may try to test a bogus Status.
			 * Ataprobe is the only place possibly invalid
			 * drives should be selected.
			 */
			drive = atagetdrive(cmdport, ctlport, Dev1);
			if(drive != nil){
				drive->ctlr = ctlr;
				ctlr->drive[1] = drive;
			}
			else{
				outb(cmdport+Dh, Dev0);
				microdelay(1);
			}
			atadrive[drivenum] = drive;
		}
	}
	else
		ctlr->drive[1] = drive;
	drivenum++;

	print("ata%d: cmd 0x%ux ctl 0x%ux irq %d\n",
		(drivenum-1)/NCtlrdrv, cmdport, ctlport, irq);
	ctlr->cmdport = cmdport;
	ctlr->ctlport = ctlport;
	ctlr->irq = irq;
	ctlr->tbdf = BUSUNKNOWN;
	ctlr->command = Cedd;		/* debugging */

	updprobe(cmdport);
	return ctlr;
}

static char *
atastat(Ctlr *ctlr, char *p, char *e)
{
	return seprint(p, e, "ata port %X ctl %X irq %d\n",
			ctlr->cmdport, ctlr->ctlport, ctlr->irq);
}

static void
atanop(Drive* drive, int subcommand)
{
	Ctlr* ctlr;
	int as, cmdport, ctlport, timeo;

	/*
	 * Attempt to abort a command by using NOP.
	 * In response, the drive is supposed to set Abrt
	 * in the Error register, set (Drdy|Err) in Status
	 * and clear Bsy when done. However, some drives
	 * (e.g. ATAPI Zip) just go Bsy then clear Status
	 * when done, hence the timeout loop only on Bsy
	 * and the forced setting of drive->error.
	 */
	ctlr = drive->ctlr;
	cmdport = ctlr->cmdport;
	outb(cmdport+Features, subcommand);
	outb(cmdport+Dh, drive->dev);
	ctlr->command = Cnop;		/* debugging */
	outb(cmdport+Cmd, Cnop);

	microdelay(1);
	ctlport = ctlr->ctlport;
	for(timeo = 0; timeo < 1000; timeo++){
		as = inb(ctlport+As);
		if(!(as & Bsy))
			break;
		microdelay(1);
	}
	drive->error |= Abrt;
}

static void
ataabort(Drive* drive, int dolock)
{
	/*
	 * If NOP is available (packet commands) use it otherwise
	 * must try a software reset.
	 */
	if(dolock)
		ilock(drive->ctlr);
	if(drive->info[Icsfs] & Mnop)
		atanop(drive, 0);
	else{
		atasrst(drive->ctlr->ctlport);
		drive->error |= Abrt;
	}
	if(dolock)
		iunlock(drive->ctlr);
}

static int
atadmasetup(Drive* drive, int len)
{
	Prd *prd;
	ulong pa;
	Ctlr *ctlr;
	int bmiba, bmisx, count, i, span;

	ctlr = drive->ctlr;
	pa = PCIWADDR(drive->data);
	if(pa & 0x03)
		return -1;

	/*
	 * Sometimes drives identify themselves as being DMA capable
	 * although they are not on a busmastering controller.
	 */
	prd = ctlr->prdt;
	if(prd == nil){
		drive->dmactl = 0;
		print("h%d: disabling dma: not on a busmastering controller\n",
			drive->driveno);
		return -1;
	}

	for(i = 0; len && i < Nprd; i++){
		prd->pa = pa;
		span = ROUNDUP(pa, ctlr->span);
		if(span == pa)
			span += ctlr->span;
		count = span - pa;
		if(count >= len){
			prd->count = PrdEOT|len;
			break;
		}
		prd->count = count;
		len -= count;
		pa += count;
		prd++;
	}
	if(i == Nprd)
		(prd-1)->count |= PrdEOT;

	bmiba = ctlr->bmiba;
	outl(bmiba+Bmidtpx, PCIWADDR(ctlr->prdt));
	if(drive->write)
		outb(ctlr->bmiba+Bmicx, 0);
	else
		outb(ctlr->bmiba+Bmicx, Rwcon);
	bmisx = inb(bmiba+Bmisx);
	outb(bmiba+Bmisx, bmisx|Ideints|Idedmae);

	return 0;
}

static void
atadmastart(Ctlr* ctlr, int write)
{
	if(write)
		outb(ctlr->bmiba+Bmicx, Ssbm);
	else
		outb(ctlr->bmiba+Bmicx, Rwcon|Ssbm);
}

static int
atadmastop(Ctlr* ctlr)
{
	int bmiba;

	bmiba = ctlr->bmiba;
	outb(bmiba+Bmicx, inb(bmiba+Bmicx) & ~Ssbm);

	return inb(bmiba+Bmisx);
}

static void
atadmainterrupt(Drive* drive, int count)
{
	Ctlr* ctlr;
	int bmiba, bmisx;

	ctlr = drive->ctlr;
	bmiba = ctlr->bmiba;
	bmisx = inb(bmiba+Bmisx);
	switch(bmisx & (Ideints|Idedmae|Bmidea)){
	case Bmidea:
		/*
		 * Data transfer still in progress, nothing to do
		 * (this should never happen).
		 */
		return;

	case Ideints:
	case Ideints|Bmidea:
		/*
		 * Normal termination, tidy up.
		 */
		drive->data += count;
		break;

	default:
		/*
		 * What's left are error conditions (memory transfer
		 * problem) and the device is not done but the PRD is
		 * exhausted. For both cases must somehow tell the
		 * drive to abort.
		 */
		ataabort(drive, 0);
		break;
	}
	atadmastop(ctlr);
	ctlr->done = 1;
}

static void
atapktinterrupt(Drive* drive)
{
	Ctlr* ctlr;
	int cmdport, len;

	ctlr = drive->ctlr;
	cmdport = ctlr->cmdport;
	switch(inb(cmdport+Ir) & (/*Rel|*/Io|Cd)){
	case Cd:
		outss(cmdport+Data, drive->pktcmd, drive->pkt/2);
		break;

	case 0:
		len = (inb(cmdport+Bytehi)<<8)|inb(cmdport+Bytelo);
		if(drive->data+len > drive->limit){
			atanop(drive, 0);
			break;
		}
		outss(cmdport+Data, drive->data, len/2);
		drive->data += len;
		break;

	case Io:
		len = (inb(cmdport+Bytehi)<<8)|inb(cmdport+Bytelo);
		if(drive->data+len > drive->limit){
			atanop(drive, 0);
			break;
		}
		inss(cmdport+Data, drive->data, len/2);
		drive->data += len;
		break;

	case Io|Cd:
		if(drive->pktdma)
			atadmainterrupt(drive, drive->dlen);
		else
			ctlr->done = 1;
		break;
	}
}

static int
atapktio(Drive* drive, uchar* cmd, int clen)
{
	Ctlr *ctlr;
	int as, cmdport, ctlport, len, r, timeo;

	r = Ok;

	drive->command = Cpkt;
	memmove(drive->pktcmd, cmd, clen);
	memset(drive->pktcmd+clen, 0, drive->pkt-clen);
	drive->limit = drive->data+drive->dlen;

	ctlr = drive->ctlr;
	cmdport = ctlr->cmdport;
	ctlport = ctlr->ctlport;

	qlock(ctlr);

	if(ataready(cmdport, ctlport, drive->dev, Bsy|Drq, 0, 107*1000) < 0){
		qunlock(ctlr);
		return -1;
	}

	ilock(ctlr);
	if(drive->dlen && drive->dmactl && !atadmasetup(drive, drive->dlen))
		drive->pktdma = Dma;
	else
		drive->pktdma = 0;

	outb(cmdport+Features, drive->pktdma);
	outb(cmdport+Count, 0);
	outb(cmdport+Sector, 0);
	len = 16*drive->secsize;
	outb(cmdport+Bytelo, len);
	outb(cmdport+Bytehi, len>>8);
	outb(cmdport+Dh, drive->dev);
	ctlr->done = 0;
	ctlr->curdrive = drive;
	ctlr->command = Cpkt;		/* debugging */
	if(drive->pktdma)
		atadmastart(ctlr, drive->write);
	outb(cmdport+Cmd, Cpkt);

	if((drive->info[Iconfig] & Mdrq) != 0x0020){
		microdelay(1);
		as = ataready(cmdport, ctlport, 0, Bsy, Drq|Chk, 4*1000);
		if(as < 0)
			r = Timeout;
		else if(as & Chk)
			r = Check;
		else
			atapktinterrupt(drive);
	}
	iunlock(ctlr);

	if(!drive->pktdma)
		sleep(ctlr, atadone, ctlr);
	else for(timeo = 0; !ctlr->done; timeo++){
		tsleep(ctlr, atadone, ctlr, 1000);
		if(ctlr->done)
			break;
		ilock(ctlr);
		atadmainterrupt(drive, 0);
		if(!drive->error && timeo > 10){
			ataabort(drive, 0);
			atadmastop(ctlr);
			drive->dmactl = 0;
			drive->error |= Abrt;
		}
		if(drive->error){
			drive->status |= Chk;
			ctlr->curdrive = nil;
		}
		iunlock(ctlr);
	}

	qunlock(ctlr);

	if(drive->status & Chk)
		r = Check;

	return r;
}

static uchar cmd48[256] = {
	[Crs]	Crs48,
	[Crd]	Crd48,
	[Crdq]	Crdq48,
	[Crsm]	Crsm48,
	[Cws]	Cws48,
	[Cwd]	Cwd48,
	[Cwdq]	Cwdq48,
	[Cwsm]	Cwsm48,
};

static int
atageniostart(Drive* drive, Devsize lba)
{
	Ctlr *ctlr;
	uchar cmd;
	int as, c, cmdport, ctlport, h, len, s, use48;

	use48 = 0;
	if((lba>>28) || drive->count > 256){
		if(!(drive->flags & Dllba))
			return -1;
		use48 = 1;
		c = h = s = 0;
	}else if(drive->dev & Lba){
		c = (lba>>8) & 0xFFFF;
		h = (lba>>24) & 0x0F;
		s = lba & 0xFF;
	}else{
		c = lba/(drive->s*drive->h);
		h = ((lba/drive->s) % drive->h);
		s = (lba % drive->s) + 1;
	}

	ctlr = drive->ctlr;
	cmdport = ctlr->cmdport;
	ctlport = ctlr->ctlport;
	if(ataready(cmdport, ctlport, drive->dev, Bsy|Drq, 0, 101*1000) < 0)
		return -1;
	ilock(ctlr);
	if(drive->dmactl && !atadmasetup(drive, drive->count*drive->secsize)){
		if(drive->write)
			drive->command = Cwd;
		else
			drive->command = Crd;
	}
	else if(drive->rwmctl){
		drive->block = drive->rwm*drive->secsize;
		if(drive->write)
			drive->command = Cwsm;
		else
			drive->command = Crsm;
	}
	else{
		drive->block = drive->secsize;
		if(drive->write)
			drive->command = Cws;
		else
			drive->command = Crs;
	}
	drive->limit = drive->data + drive->count*drive->secsize;
	cmd = drive->command;
	if(use48){
		outb(cmdport+Count, drive->count>>8);
		outb(cmdport+Count, drive->count);
		outb(cmdport+Lbalo, lba>>24);
		outb(cmdport+Lbalo, lba );
		outb(cmdport+Lbamid, lba>>32);
		outb(cmdport+Lbamid, lba>>8);
		outb(cmdport+Lbahi, lba>>40);
		outb(cmdport+Lbahi, lba>>16);
		outb(cmdport+Dh, drive->dev|Lba);
		cmd = cmd48[cmd];

		if(DEBUG & Dbg48BIT)
			print("using 48-bit commands\n");
	}else{
		outb(cmdport+Count, drive->count);
		outb(cmdport+Sector, s);
		outb(cmdport+Cyllo, c);
		outb(cmdport+Cylhi, c>>8);
		outb(cmdport+Dh, drive->dev|h);
	}
	ctlr->done = 0;
	ctlr->curdrive = drive;
	ctlr->command = drive->command;	/* debugging */
	outb(cmdport+Cmd, cmd);

	switch(drive->command){
	case Cws:
	case Cwsm:
		microdelay(1);
		as = ataready(cmdport, ctlport, 0, Bsy, Drq|Err, 10*1000);
		if(as < 0 || (as & Err)){
			iunlock(ctlr);
			return -1;
		}
		len = drive->block;
		if(drive->data+len > drive->limit)
			len = drive->limit-drive->data;
		outss(cmdport+Data, drive->data, len/2);
		break;

	case Crd:
	case Cwd:
		atadmastart(ctlr, drive->write);
		break;
	}
	iunlock(ctlr);

	return 0;
}

static int
atagenioretry(Drive* drive)
{
	if(drive->dmactl){
		drive->dmactl = 0;
		print("atagenioretry: disabling dma\n");
	}
	else if(drive->rwmctl)
		drive->rwmctl = 0;
	else
		return Check;

	return Retry;
}

static int
atagenio(Drive* drive, uvlong lba, int count)
{
	Ctlr *ctlr;
	int maxio;

	dprint("atagenio(d, %d, %lld, %d)\n", drive->write, lba, count);

	ctlr = drive->ctlr;
	if(drive->data == nil)
		return Ok;
	if(drive->dlen < count*drive->secsize)
		count = drive->dlen/drive->secsize;
	qlock(ctlr);
	if(ctlr->maxio)
		maxio = ctlr->maxio;
	else if(drive->flags & Dllba)
		maxio = 65536;
	else
		maxio = 256;
	while(count){
		if(count > maxio)
			drive->count = maxio;
		else
			drive->count = count;
		if(atageniostart(drive, lba)){
			ilock(ctlr);
			atanop(drive, 0);
			iunlock(ctlr);
			qunlock(ctlr);
			return atagenioretry(drive);
		}

		tsleep(ctlr, atadone, ctlr, 60*1000);
		if(!ctlr->done){
			/*
			 * What should the above timeout be? In
			 * standby and sleep modes it could take as
			 * long as 30 seconds for a drive to respond.
			 * Very hard to get out of this cleanly.
			 */
			atadumpstate(drive, lba, count);
			ataabort(drive, 1);
			qunlock(ctlr);
			return atagenioretry(drive);
		}

		if(drive->status & Err){
			qunlock(ctlr);
			return Check;
		}
		count -= drive->count;
		lba += drive->count;
	}
	qunlock(ctlr);

	dprint("atagenio ok\n");
	return Ok;
}

static int
rw(Drive *d, int rw, void *c, long bytes, uvlong lba)
{
	int status;

	qlock(d);
retry:
	d->write = rw;
	d->data = c;
	d->dlen = bytes;

	d->status = 0;
	d->error = 0;
	if(d->pkt)
		status = Check; // atapktio(d, lba, bytes);
	else
		status = atagenio(d, lba, bytes);
	if(status == Retry){
		dprint("retry: dma %.8ux rwm %.4ux\n", d->dmactl, d->rwmctl);
		goto retry;
	}
	qunlock(d);
	if(status != Ok)
		return status;

	return Ok;
}

static void
atainterrupt(Ureg*, void* arg)
{
	Ctlr *ctlr;
	Drive *drive;
	int cmdport, len, status;

	ctlr = arg;

	if(ctlr == 0){
		print("unexpected ataintr\n");
		return;
	}
//	if(ctlr->pcidev)
//		iprint("ataintr(%x, %x)\n", ctlr->pcidev->vid, ctlr->pcidev->did);
//	else
//		iprint("ataintr(?, ?)\n");

	ilock(ctlr);
	if(inb(ctlr->ctlport+As) & Bsy){
		iunlock(ctlr);
		if(DEBUG & DbgBsy)
			print("IBsy+");
		return;
	}
	cmdport = ctlr->cmdport;
	status = inb(cmdport+Status);
	if((drive = ctlr->curdrive) == nil){
		iunlock(ctlr);
		if((DEBUG & DbgINL) && ctlr->command != Cedd)
			print("Inil%2.2uX+", ctlr->command);
		return;
	}

	if(status & Err)
		drive->error = inb(cmdport+Error);
	else switch(drive->command){
	default:
		drive->error = Abrt;
		break;

	case Crs:
	case Crsm:
		if(!(status & Drq)){
			drive->error = Abrt;
			break;
		}
		len = drive->block;
		if(drive->data+len > drive->limit)
			len = drive->limit-drive->data;
		inss(cmdport+Data, drive->data, len/2);
		drive->data += len;
		if(drive->data >= drive->limit)
			ctlr->done = 1;
		break;

	case Cws:
	case Cwsm:
		len = drive->block;
		if(drive->data+len > drive->limit)
			len = drive->limit-drive->data;
		drive->data += len;
		if(drive->data >= drive->limit){
			ctlr->done = 1;
			break;
		}
		if(!(status & Drq)){
			drive->error = Abrt;
			break;
		}
		len = drive->block;
		if(drive->data+len > drive->limit)
			len = drive->limit-drive->data;
		outss(cmdport+Data, drive->data, len/2);
		break;

	case Cpkt:
		atapktinterrupt(drive);
		break;

	case Crd:
	case Cwd:
		atadmainterrupt(drive, drive->count*drive->secsize);
		break;

	case Cstandby:
		ctlr->done = 1;
		break;
	}
	iunlock(ctlr);

	if(drive->error){
		status |= Err;
		ctlr->done = 1;
	}

	if(ctlr->done){
		ctlr->curdrive = nil;
		drive->status = status;
		wakeup(ctlr);
	}
}

static void
atapnp(void)
{
	int i, ispc87415, maxio, pi, r, span;
	Ctlr *legacy[2], *ctlr;
	Pcidev *p;

	memset(legacy, 0, sizeof legacy);
	if(ctlr = ataprobe(Ctlr0cmd, Ctlr0ctl, IrqATA0)){
		atactlr[natactlr++] = ctlr;
		legacy[0] = ctlr;
	}
	if(ctlr = ataprobe(Ctlr1cmd, Ctlr1ctl, IrqATA1)){
		atactlr[natactlr++] = ctlr;
		legacy[1] = ctlr;
	}
	p = nil;
	while(p = pcimatch(p, 0, 0)){
		/*
		 * Look for devices with the correct class and sub-class
		 * code and known device and vendor ID; add native-mode
		 * channels to the list to be probed, save info for the
		 * compatibility mode channels.
		 * Note that the legacy devices should not be considered
		 * PCI devices by the interrupt controller.
		 * For both native and legacy, save info for busmastering
		 * if capable.
		 * Promise Ultra ATA/66 (PDC20262) appears to
		 * 1) give a sub-class of 'other mass storage controller'
		 *    instead of 'IDE controller', regardless of whether it's
		 *    the only controller or not;
		 * 2) put 0 in the programming interface byte (probably
		 *    as a consequence of 1) above).
		 * Sub-class code 0x04 is 'RAID controller', e.g. VIA VT8237.
		 */
		if(p->ccrb != 0x01)
			continue;

		/*
		 * file server special: ccru is a short in the FS kernel,
		 * thus the cast to uchar.
		 */
		switch ((uchar)p->ccru) {
		case 1:
		case 4:
		case 0x80:
			break;
		default:
			continue;
		}

		pi = p->ccrp;
		ispc87415 = 0;
		maxio = 0;
		span = BMspan;

		switch((p->did<<16)|p->vid){
		default:
			continue;

		case (0x0002<<16)|0x100B:	/* NS PC87415 */
			/*
			 * Disable interrupts on both channels until
			 * after they are probed for drives.
			 * This must be called before interrupts are
			 * enabled because the IRQ may be shared.
			 */
			ispc87415 = 1;
			pcicfgw32(p, 0x40, 0x00000300);
			break;
		case (0x1000<<16)|0x1042:	/* PC-Tech RZ1000 */
			/*
			 * Turn off prefetch. Overkill, but cheap.
			 */
			r = pcicfgr32(p, 0x40);
			r &= ~0x2000;
			pcicfgw32(p, 0x40, r);
			break;
		case (0x4D38<<16)|0x105A:	/* Promise PDC20262 */
		case (0x4D30<<16)|0x105A:	/* Promise PDC202xx */
		case (0x4D68<<16)|0x105A:	/* Promise PDC20268 */
		case (0x4D69<<16)|0x105A:	/* Promise Ultra/133 TX2 */
		case (0x3373<<16)|0x105A:	/* Promise 20378 RAID */
		case (0x3149<<16)|0x1106:	/* VIA VT8237 SATA/RAID */
		case (0x3112<<16)|0x1095:   	/* SiI 3112 SATA/RAID */
			maxio = 15;
			span = 8*1024;
		case (0x3114<<16)|0x1095:	/* SiI 3114 SATA/RAID */
			pi = 0x85;
			break;
		case (0x0004<<16)|0x1103:	/* HighPoint HPT366 */
			pi = 0x85;
			/*
			 * Turn off fast interrupt prediction.
			 */
			if((r = pcicfgr8(p, 0x51)) & 0x80)
				pcicfgw8(p, 0x51, r & ~0x80);
			if((r = pcicfgr8(p, 0x55)) & 0x80)
				pcicfgw8(p, 0x55, r & ~0x80);
			break;
		case (0x0640<<16)|0x1095:	/* CMD 640B */
			/*
			 * Bugfix code here...
			 */
			break;
		case (0x7441<<16)|0x1022:	/* AMD 768 */
			/*
			 * Set:
			 *	0x41	prefetch, postwrite;
			 *	0x43	FIFO configuration 1/2 and 1/2;
			 *	0x44	status register read retry;
			 *	0x46	DMA read and end of sector flush.
			 */
			r = pcicfgr8(p, 0x41);
			pcicfgw8(p, 0x41, r|0xF0);
			r = pcicfgr8(p, 0x43);
			pcicfgw8(p, 0x43, (r & 0x90)|0x2A);
			r = pcicfgr8(p, 0x44);
			pcicfgw8(p, 0x44, r|0x08);
			r = pcicfgr8(p, 0x46);
			pcicfgw8(p, 0x46, (r & 0x0C)|0xF0);
		case (0x7469<<16)|0x1022:	/* AMD 3111 */
			/*
			 * This can probably be lumped in with the 768 above.
			 */
		case (0x01BC<<16)|0x10DE:	/* nVidia nForce1 */
		case (0x0065<<16)|0x10DE:	/* nVidia nForce2 */
		case (0x0085<<16)|0x10DE:	/* nVidia nForce2 MCP */
		case (0x00D5<<16)|0x10DE:	/* nVidia nForce3 */
		case (0x00E5<<16)|0x10DE:	/* nVidia nForce3 Pro */
		case (0x0035<<16)|0x10DE:	/* nVidia nForce3 MCP */
		case (0x0053<<16)|0x10DE:	/* nVidia nForce4 */
			/*
			 * Ditto, although it may have a different base
			 * address for the registers (0x50?).
			 */
		case (0x4376<<16)|0x1002:	/* ATI Radeon Xpress 200M */
			break;
		case (0x0211<<16)|0x1166:	/* ServerWorks IB6566 */
			{
				Pcidev *sb;

				sb = pcimatch(nil, 0x1166, 0x0200);
				if(sb == nil)
					break;
				r = pcicfgr32(sb, 0x64);
				r &= ~0x2000;
				pcicfgw32(sb, 0x64, r);
			}
			span = 32*1024;
			break;
		case (0x5513<<16)|0x1039:	/* SiS 962 */
		case (0x0646<<16)|0x1095:	/* CMD 646 */
		case (0x0571<<16)|0x1106:	/* VIA 82C686 */
		case (0x1230<<16)|0x8086:	/* 82371FB (PIIX) */
		case (0x7010<<16)|0x8086:	/* 82371SB (PIIX3) */
		case (0x7111<<16)|0x8086:	/* 82371[AE]B (PIIX4[E]) */
		case (0x2411<<16)|0x8086:	/* 82801AA (ICH) */
		case (0x2421<<16)|0x8086:	/* 82801AB (ICH0) */
		case (0x244A<<16)|0x8086:	/* 82801BA (ICH2, Mobile) */
		case (0x244B<<16)|0x8086:	/* 82801BA (ICH2, High-End) */
		case (0x248A<<16)|0x8086:	/* 82801CA (ICH3, Mobile) */
		case (0x248B<<16)|0x8086:	/* 82801CA (ICH3, High-End) */
		case (0x24CA<<16)|0x8086:	/* 82801DBM (ICH4, Mobile) */
		case (0x24CB<<16)|0x8086:	/* 82801DB (ICH4, High-End) */
		case (0x24DB<<16)|0x8086:	/* 82801EB (ICH5) */
		case (0x266F<<16)|0x8086:	/* 82801FB (ICH6) */
		case (0x269e<<16)|0x8086:	/* ? */
			break;
		}

		for(i = 0; i < 2; i++){
			if(pi & (1<<(2*i))){
				ctlr = ataprobe(p->mem[0+2*i].bar & ~0x01,
						p->mem[1+2*i].bar & ~0x01,
						p->intl);
				if(ctlr == nil)
					continue;

				if(ispc87415) {
					ctlr->ienable = pc87415ienable;
					print("pc87415disable: not yet implemented\n");
				}
				atactlr[natactlr++] = ctlr;
				ctlr->tbdf = p->tbdf;
			} else if((ctlr = legacy[i]) == nil)
				continue;

			ctlr->pcidev = p;
			ctlr->maxio = maxio;
			ctlr->span = span;
			if(!(pi & 0x80))
				continue;
			ctlr->bmiba = (p->mem[4].bar & ~0x01) + i*8;
		}
	}
}

static int
ataenable(Ctlr *ctlr)
{
	if(ctlr->bmiba){
#define ALIGN	(4 * 1024)
		if(ctlr->pcidev != nil)
			pcisetbme(ctlr->pcidev);
		// ctlr->prdt = xspanalloc(Nprd*sizeof(Prd), 4, 4*1024);
		ctlr->prdtbase = ialloc(Nprd * sizeof(Prd) + ALIGN, 0);
		ctlr->prdt = (Prd *)(((ulong)ctlr->prdtbase + ALIGN) & ~(ALIGN - 1));
	}
	dprint("ata irq %d %ux\n", ctlr->irq, ctlr->tbdf);
	setvec(24+ctlr->irq, atainterrupt, ctlr);
	outb(ctlr->ctlport+Dc, 0);
	if(ctlr->ienable)
		ctlr->ienable(ctlr);

	return 1;
}

static void
statc(Ctlr *c)
{
	Drive *d;
	int j;

	for(j = 0; j < NCtlrdrv; j++){
		d = c->drive[j];
		if(d == 0 || d->fflag == 0)
			continue;
		print("h%d:\n", d->driveno);
		print("  r\t%W\n", d->rate+Read);
		print("  w\t%W\n", d->rate+Write);
	}
}

static void
cmd_stat(int, char*[])
{
	Ctlr *c;
	int i;

	for(i = 0; i < nelem(atactlr); i++){
		c = atactlr[i];
		if(c == nil)
			continue;
		statc(c);
	}
}

int
sdinit(void)
{
	return 0;
}

/* find all the controllers, enable interrupts, set up SDevs & SDunits */
int
atainit(void)
{
	uint i;
	static int once;

	if(once++)
		return 1;

	atapnp();
	for(i = 0; i < nelem(atactlr); i++)
		if(atactlr[i])
			ataenable(atactlr[i]);
	cmd_install("stati", "-- ide/ata stats", cmd_stat);
	return 1;
}

static Drive*
atadev(Device *d)
{
	int i, j;
	Drive *dr;

	if(d->private)
		return d->private;

	i = d->wren.ctrl;
	j = d->wren.targ;

	for(; i < natactlr; i++){
		if(j < NCtlrdrv){
			dr = atactlr[i]->drive[j];
//			if(dr->state&Dready){
				d->private = dr;
				return dr;
//			}
//			return 0;
		}
		j -= NCtlrdrv;
	}
	panic("ata: bad drive %Z\n", d);
	return 0;
}

void
ideinit(Device *dv)
{
	Drive *d;
	vlong s, b;
	char *lba;

	atainit();
top:
	d = atadev(dv);
	if(d == 0){
		print("\t\t" "%d.%d.%d not ready yet\n", dv->wren.ctrl, dv->wren.targ, dv->wren.lun);
		delay(500);
		goto top;
	}
	if(d->online++ == 0)
		return;

	dofilter(d->rate+Read);
	dofilter(d->rate+Write);

	s = d->sectors;
	b = (s*d->secsize)/RBUFSIZE;
	if(d->lba){
		lba = "";
		if(d->flags&Dllba)
			lba = "L";
		print("\t\t" "%lld sectors/%lld blocks %sLBA\n", s, b, lba);
	}else
		print("\t\t" "%d.%d.%d chs/%lld blocks\n", d->c, d->h, d->s, b);
}

Devsize
idesize(Device *dv)
{
	Drive *d;

	d = atadev(dv);
	/*
	 * dividing first is sloppy but reduces the range of intermediate
	 * values, avoiding possible overflow.
	return (d->sectors / RBUFSIZE) * d->secsize;
	 */
	return (d->sectors * d->secsize) / RBUFSIZE;
}

int
ideread(Device *dv, Devsize b, void *c)
{
	int rv;
	Drive *d;

	d = atadev(dv);
	if(d == 0)
		return 1;
	rv = rw(d, 0, c, RBUFSIZE, b*(RBUFSIZE/d->secsize));
	if(rv)
		return 1;
	d->rate[Read].count++;
	d->fflag = 1;
	return 0;
}

int
idewrite(Device *dv, Devsize b, void *c)
{
	int rv;
	Drive *d;

	d = atadev(dv);
	if(d == 0)
		return 1;

	rv = rw(d, 1, c, RBUFSIZE, b*(RBUFSIZE/d->secsize));
	if(rv)
		return 1;
	d->rate[Write].count++;
	d->fflag = 1;
	return 0;
}

int
idesecsize(Device *dv)
{
	Drive *d;

	d = atadev(dv);
	if(d == 0)
		panic("idesecsize");
	return d->secsize;
}
