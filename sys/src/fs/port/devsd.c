/*
 * Storage Device.
 */
#include "all.h"
#include "io.h"
#include "mem.h"

#include "sd.h"
#undef error

#define parttrace(...) //print(__VA_ARGS__)
#define partaddtrace(...) //print(__VA_ARGS__)

extern SDifc sdataifc;

SDifc* sdifc[] = {
	&sdataifc,
	nil,
};

static SDev* sdlist;
static SDunit** sdunit;
static int sdnunit;
static int _sdmask;
static int cdmask;
static int sdmask;
static QLock sdqlock;

// didn't want to confuse the diff.
static int _sdinit(void);

static char devletters[] = "0123456789"
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static SDev *devs[sizeof devletters-1];

static int
sdnextid(SDev *dev, int id)
{
	char *p;
	int id0;

	p = strchr(devletters, id);
	if(p == nil)
		panic("bad driveletter %d\n", id);
	id0 = id = p-devletters;
	for(;;){
		if(devs[id] == 0)
			break;
		id = id+1%nelem(devs);
		if(id == id0)
			panic("no drives available");
	}

//print("assigining %c; requested %c\n", devletters[id], devletters[id0]);

	devs[id] = dev;
	return devletters[id];
}

void
sdaddpart(SDunit* unit, char* name, Devsize start, Devsize end)
{
	SDpart *pp;
	int i, partno;

	partaddtrace("add %d %s %s %lld %lld\n", unit->npart, unit->name,
			name, start, end);
	/*
	 * Check name not already used
	 * and look for a free slot.
	 */
	if(unit->part != nil){
		partno = -1;
		for(i = 0; i < SDnpart; i++){
			pp = &unit->part[i];
			if(!pp->valid){
				if(partno == -1)
					partno = i;
				break;
			}
			if(strcmp(name, pp->name) == 0){
				if(pp->start == start && pp->end == end){
					parttrace("already present\n");
					return;
				}
			}
		}
	}else{
		if((unit->part = ialloc(sizeof(SDpart)*SDnpart, 0)) == nil){
			parttrace("malloc failed\n");
			return;
		}
		partno = 0;
	}

	/*
	 * Check there is a free slot and size and extent are valid.
	 */
	if(partno == -1 || start > end || end > unit->sectors){
		print("cannot add %s!%s [%llud,%llud) to disk [0,%llud): %s\n",
			unit->name, name, start, end, unit->sectors,
			partno==-1 ? "no free partitions" :
				"partition boundaries out of range");
		return;
	}
	pp = &unit->part[partno];
	pp->start = start;
	pp->end = end;
	strncpy(pp->name, name, NAMELEN);
	pp->valid = 1;
	unit->npart++;
}

void
sddelpart(SDunit* unit,  char* name)
{
	int i;
	SDpart *pp;

	parttrace("del %d %s %s\n", unit->npart, unit->name, name);
	/*
	 * Look for the partition to delete.
	 * Can't delete if someone still has it open.
	 * If it's the last valid partition zap the
	 * whole table.
	 */
	pp = unit->part;
	for(i = 0; i < SDnpart; i++){
		if(strncmp(name, pp->name, NAMELEN) == 0)
			break;
		pp++;
	}
	if(i >= SDnpart)
		return;
	pp->valid = 0;

	unit->npart--;
	if(unit->npart == 0){
//		free(unit->part);
		unit->part = nil;
	}
}

static void
add9loadparts(SDunit *unit)
{
	char *s, *v[16], *w[3], buf[80];
	int i, n;
		
	snprint(buf, sizeof buf, "%spart", unit->name);
	if((s = getconf(buf)) == 0)
		return;

	strncpy(buf, s, sizeof buf-2);
	buf[sizeof buf-1] = 0;
	n = getfields(buf, v, nelem(v), 0, "/");
	for(i = 0; i<n; i++){
		if(getfields(v[i], w, nelem(w), 0, " ") != 3)
			continue;
		sdaddpart(unit, w[0], strtoull(w[1], 0, 0), strtoull(w[2], 0, 0));
	}
}

static int
sdinitpart(SDunit* unit)
{
	unit->sectors = unit->secsize = 0;
	unit->npart = 0;
	if(unit->part){
//		free(unit->part);
		unit->part = nil;
	}

	parttrace("sdinitpart\n");
	if(unit->inquiry[0] & 0xC0)
		return 0;
	switch(unit->inquiry[0] & 0x1F){
	case 0x00:			/* DA */
	case 0x04:			/* WORM */
	case 0x05:			/* CD-ROM */
	case 0x07:			/* MO */
		break;
	default:
		parttrace("unknown device type: %x\n", unit->inquiry[0] & 0x1F);
		return 0;
	}

	if(unit->dev->ifc->online == nil || unit->dev->ifc->online(unit) == 0){
		parttrace("offline\n");
		return 0;
	}
	parttrace("calling sdaddpart\n");
	sdaddpart(unit, "data", 0, unit->sectors);
	add9loadparts(unit);
	return 1;
}

SDunit*
sdgetunit(SDev* sdev, int subno)
{
	int index;
	SDunit *unit;

	parttrace("sdgetunit(%s, %d)\n", sdev->ifc->name, subno);
	if(sdlist == nil)
		_sdinit();
	if(subno >= sdnunit){
		parttrace("sdgetunit: partition out-of-range %d >= %d\n", subno, sdnunit);
		return nil;
	}
	/*
	 * Associate a unit with a given device and sub-unit
	 * number on that device.
	 * The device will be probed if it has not already been
	 * successfully accessed.
	 */
	qlock(&sdqlock);
	index = sdev->index+subno;
	unit = sdunit[index];
	if(unit == nil){
		if((unit = ialloc(sizeof(SDunit), 0)) == nil){
			qunlock(&sdqlock);
			return nil;
		}

		if(sdev->enabled == 0 && sdev->ifc->enable)
			sdev->ifc->enable(sdev);
		sdev->enabled = 1;

		snprint(unit->name, NAMELEN, "sd%c%d", sdev->idno, subno);
		unit->subno = subno;
		unit->dev = sdev;

		/*
		 * No need to lock anything here as this is only
		 * called before the unit is made available in the
		 * sdunit[] array.
		 */
		if(unit->dev->ifc->verify(unit) == 0){
			qunlock(&sdqlock);
//			free(unit);
			parttrace("sdgetunit() -> nil\n");
			return nil;
		}
		sdunit[index] = unit;
	}
	qunlock(&sdqlock);
	parttrace("sdgetunit() -> %s\n", unit->name);
	return unit;
}

SDunit*
sdindex2unit(int index)
{
	SDev *sdev;

	/*
	 * Associate a unit with a given index into the top-level
	 * device directory.
	 * The device will be probed if it has not already been
	 * successfully accessed.
	 */
	for(sdev = sdlist; sdev != nil; sdev = sdev->next)
		if(index >= sdev->index && index < sdev->index+sdev->nunit)
			return sdgetunit(sdev, index-sdev->index);
	return nil;
}

static int
_sdinit(void)
{
	ulong m;
	int i;
	SDev *sdev, *tail;
	SDunit *unit;

	/*
	 * Probe all configured controllers and make a list
	 * of devices found, accumulating a possible maximum number
	 * of units attached and marking each device with an index
	 * into the linear top-level directory array of units.
	 */
	parttrace("_sdinit()\n");
	tail = nil;
	for(i = 0; sdifc[i] != nil; i++){
		parttrace("probe %s\n", sdifc[i]->name);
		if((sdev = sdifc[i]->pnp()) == nil)
			continue;
		if(sdlist != nil)
			tail->next = sdev;
		else
			sdlist = sdev;
		for(tail = sdev; tail->next != nil; tail = tail->next){
			tail->idno = sdnextid(tail, tail->idno);
			parttrace("\t" "[%2d.%2d]", sdnunit, sdnunit+tail->nunit-1);
			tail->index = sdnunit;
			sdnunit += tail->nunit;
		}
		tail->idno = sdnextid(tail, tail->idno);
		parttrace("\t" "[%2d.%2d]\n", sdnunit, sdnunit+tail->nunit-1);
		tail->index = sdnunit;
		sdnunit += tail->nunit;
	}

	/*
	 * Legacy and option code goes here. This will be hard...
	 */

	/*
	 * The maximum number of possible units is known, allocate
	 * placeholders for their datastructures; the units will be
	 * probed and structures allocated when attached.
	 * Allocate controller names for the different types.
	 */
	if(sdnunit == 0)
		return 0;
	if((sdunit = ialloc(sdnunit*sizeof(SDunit*), 0)) == nil)
		return 0;
//	sddetach = _sddetach;

	for(i = 0; sdifc[i] != nil; i++){
		if(sdifc[i]->id)
			sdifc[i]->id(sdlist);
	}

	m = 0;
	cdmask = sdmask = 0;
	for(i=0; i<sdnunit && i < 32; i++) {
		unit = sdindex2unit(i);
		if(unit == nil)
			continue;
		sdinitpart(unit);
//		partition(unit);
		if(unit->npart > 0){	/* BUG */
			if((unit->inquiry[0] & 0x1F) == 0x05)
				cdmask |= (1<<i);
			else
				sdmask |= (1<<i);
			m |= (1<<i);
		}
	}

//notesdinfo();
	_sdmask = m;
	return m;
}

//int
//cdinit(void)
//{
//	if(sdnunit == 0)
//		_sdinit();
//	return cdmask;
//}

int
sdinit(void)
{
	if(sdnunit == 0)
		_sdinit();
	return sdmask;
}

void
sdinitdev(int i, char *s)
{
	SDunit *unit;

	unit = sdindex2unit(i);
	strcpy(s, unit->name);
}

//void
//sdprintdevs(int i)
//{
//	char *s;
//	SDunit *unit;
//
//	unit = sdindex2unit(i);
//	for(i=0; i<unit->npart; i++){
//		s = unit->part[i].name;
//		if(strncmp(s, "dos", 3) == 0
//		|| strncmp(s, "9fat", 4) == 0
//		|| strncmp(s, "fs", 2) == 0)
//			print(" %s!%s", unit->name, s);
//	}
//}

SDpart*
sdfindpart(SDunit *unit, char *name)
{
	int i;

	parttrace("findpart %d %s %s\t\n", unit->npart, unit->name, name);
	for(i=0; i<unit->npart; i++) {
		parttrace("%s...", unit->part[i].name);
		if(strcmp(unit->part[i].name, name) == 0){
			parttrace("\n");
			return &unit->part[i];
		}
	}
	parttrace("not found\n");
	return nil;
}

long
sdbio(SDunit *unit, SDpart *pp, void* va, long len, Off off)
{
	Off l;
	Off bno, max, nb, offset;
	char *a;
	static uchar *b;
	static ulong bsz;

	a = va;
	qlock(&unit->ctl);
	if(unit->changed){
		qunlock(&unit->ctl);
		return 0;
	}

	/*
	 * Check the request is within bounds.
	 * Removeable drives are locked throughout the I/O
	 * in case the media changes unexpectedly.
	 * Non-removeable drives are not locked during the I/O
	 * to allow the hardware to optimise if it can; this is
	 * a little fast and loose.
	 * It's assumed that non-removable media parameters
	 * (sectors, secsize) can't change once the drive has
	 * been brought online.
	 */
	bno = (off/unit->secsize) + pp->start;
	nb = ((off+len+unit->secsize-1)/unit->secsize) + pp->start - bno;
	max = SDmaxio/unit->secsize;
	if(nb > max)
		nb = max;
	if(bno+nb > pp->end)
		nb = pp->end - bno;
	if(bno >= pp->end || nb == 0){
		qunlock(&unit->ctl);
		return 0;
	}
	if(!(unit->inquiry[1] & 0x80))
		qunlock(&unit->ctl);

	if(bsz < nb*unit->secsize){
		b = ialloc(nb*unit->secsize, 0);
		bsz = nb*unit->secsize;
	}
//	b = sdmalloc(nb*unit->secsize);
//	if(b == nil)
//		return 0;

	offset = off%unit->secsize;
	l = unit->dev->ifc->bio(unit, 0, 0, b, nb, bno);
	if(l < 0) {
//		sdfree(b);
		return 0;
	}

	if(l < offset)
		len = 0;
	else if(len > l - offset)
		len = l - offset;
	if(len)
		memmove(a, b+offset, len);
//	sdfree(b);

	if(unit->inquiry[1] & 0x80)
		qunlock(&unit->ctl);

	return len;
}

#ifdef DMA
long
sdrio(SDreq *r, void* a, long n)
{
	if(n >= SDmaxio || n < 0)
		return 0;

	r->data = nil;
	if(n){
		if((r->data = malloc(n)) == nil)
			return 0;
		if(r->write)
			memmove(r->data, a, n);
	}
	r->dlen = n;

	if(r->unit->dev->ifc->rio(r) != SDok){
// cgascreenputs("1", 1);
		if(r->data != nil){
			sdfree(r->data);
			r->data = nil;
		}
		return 0;
	}
// cgascreenputs("2", 1);

	if(!r->write && r->rlen > 0)
		memmove(a, r->data, r->rlen);
// cgascreenputs("3", 1);
	if(r->data != nil){
		sdfree(r->data);
		r->data = nil;
	}

// cgascreenputs("4", 1);
	return r->rlen;
}
#endif /* DMA */

void*
sdmalloc(void *p, ulong sz)
{
	if(p != nil) {
		memset(p, 0, sz);
		return p;
	}
	return ialloc(sz, 0);
}

/*
 * SCSI simulation for non-SCSI devices
 */
int
sdsetsense(SDreq *r, int status, int key, int asc, int ascq)
{
	int len;
	SDunit *unit;
	
	unit = r->unit;
	unit->sense[2] = key;
	unit->sense[12] = asc;
	unit->sense[13] = ascq;

	if(status == SDcheck && !(r->flags & SDnosense)){
		/* request sense case from sdfakescsi */
		len = sizeof unit->sense;
		if(len > sizeof r->sense-1)
			len = sizeof r->sense-1;
		memmove(r->sense, unit->sense, len);
		unit->sense[2] = 0;
		unit->sense[12] = 0;
		unit->sense[13] = 0;
		r->flags |= SDvalidsense;
		return SDok;
	}
	return status;
}

int
sdfakescsi(SDreq *r, void *info, int ilen)
{
	uchar *cmd, *p;
	uvlong len;
	SDunit *unit;
	
	cmd = r->cmd;
	r->rlen = 0;
	unit = r->unit;
	
	/*
	 * Rewrite read(6)/write(6) into read(10)/write(10).
	 */
	switch(cmd[0]){
	case 0x08:	/* read */
	case 0x0A:	/* write */
		cmd[9] = 0;
		cmd[8] = cmd[4];
		cmd[7] = 0;
		cmd[6] = 0;
		cmd[5] = cmd[3];
		cmd[4] = cmd[2];
		cmd[3] = cmd[1] & 0x0F;
		cmd[2] = 0;
		cmd[1] &= 0xE0;
		cmd[0] |= 0x20;
		break;
	}

	/*
	 * Map SCSI commands into ATA commands for discs.
	 * Fail any command with a LUN except INQUIRY which
	 * will return 'logical unit not supported'.
	 */
	if((cmd[1]>>5) && cmd[0] != 0x12)
		return sdsetsense(r, SDcheck, 0x05, 0x25, 0);
	
	switch(cmd[0]){
	default:
		return sdsetsense(r, SDcheck, 0x05, 0x20, 0);
	
	case 0x00:	/* test unit ready */
		return sdsetsense(r, SDok, 0, 0, 0);
	
	case 0x03:	/* request sense */
		if(cmd[4] < sizeof unit->sense)
			len = cmd[4];
		else
			len = sizeof unit->sense;
		if(r->data && r->dlen >= len){
			memmove(r->data, unit->sense, len);
			r->rlen = len;
		}
		return sdsetsense(r, SDok, 0, 0, 0);
	
	case 0x12:	/* inquiry */
		/* warning: useless or misleading comparison: UCHAR < 0x100 */
		if(cmd[4] < sizeof unit->inquiry)
			len = cmd[4];
		else
			len = sizeof unit->inquiry;
		if(r->data && r->dlen >= len){
			memmove(r->data, r->sense, len);
			r->rlen = len;
		}
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x1B:	/* start/stop unit */
		/*
		 * nop for now, can use power management later.
		 */
		return sdsetsense(r, SDok, 0, 0, 0);
	
	case 0x25:	/* read capacity */
		if((cmd[1] & 0x01) || cmd[2] || cmd[3])
			return sdsetsense(r, SDcheck, 0x05, 0x24, 0);
		if(r->data == nil || r->dlen < 8)
			return sdsetsense(r, SDcheck, 0x05, 0x20, 1);
		
		/*
		 * Read capacity returns the LBA of the last sector.
		 */
		len = unit->sectors - 1;
		p = r->data;
		*p++ = len>>24;
		*p++ = len>>16;
		*p++ = len>>8;
		*p++ = len;
		len = 512;
		*p++ = len>>24;
		*p++ = len>>16;
		*p++ = len>>8;
		*p++ = len;
		r->rlen = p - (uchar*)r->data;
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x9E:	/* long read capacity */
		if((cmd[1] & 0x01) || cmd[2] || cmd[3])
			return sdsetsense(r, SDcheck, 0x05, 0x24, 0);
		if(r->data == nil || r->dlen < 8)
			return sdsetsense(r, SDcheck, 0x05, 0x20, 1);	
		/*
		 * Read capcity returns the LBA of the last sector.
		 */
		len = unit->sectors - 1;
		p = r->data;
		*p++ = len>>56;
		*p++ = len>>48;
		*p++ = len>>40;
		*p++ = len>>32;
		*p++ = len>>24;
		*p++ = len>>16;
		*p++ = len>>8;
		*p++ = len;
		len = 512;
		*p++ = len>>24;
		*p++ = len>>16;
		*p++ = len>>8;
		*p++ = len;
		r->rlen = p - (uchar*)r->data;
		return sdsetsense(r, SDok, 0, 0, 0);
	
	case 0x5A:	/* mode sense */
		return sdmodesense(r, cmd, info, ilen);
	
	case 0x28:	/* read */
	case 0x2A:	/* write */
	case 0x88:	/* read16 */
	case 0x8a:	/* write16 */
		return SDnostatus;
	}
}

int
sdmodesense(SDreq *r, uchar *cmd, void *info, int ilen)
{
	int len;
	uchar *data;
	
	/*
	 * Fake a vendor-specific request with page code 0,
	 * return the drive info.
	 */
	if((cmd[2] & 0x3F) != 0 && (cmd[2] & 0x3F) != 0x3F)
		return sdsetsense(r, SDcheck, 0x05, 0x24, 0);
	len = (cmd[7]<<8)|cmd[8];
	if(len == 0)
		return SDok;
	if(len < 8+ilen)
		return sdsetsense(r, SDcheck, 0x05, 0x1A, 0);
	if(r->data == nil || r->dlen < len)
		return sdsetsense(r, SDcheck, 0x05, 0x20, 1);
	data = r->data;
	memset(data, 0, 8);
	data[0] = ilen>>8;
	data[1] = ilen;
	if(ilen)
		memmove(data+8, info, ilen);
	r->rlen = 8+ilen;
	return sdsetsense(r, SDok, 0, 0, 0);
}
