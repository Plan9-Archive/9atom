#include "all.h"

int
devread(Device *d, Off b, void *c)
{
	if(devtab[d->type].read)
		return devtab[d->type].read(d, b, c);
	print("illegal device in read: %Z(%lld)\n", d, (Wideoff)b);
	return 1;
}

int
devwrite(Device *d, Off b, void *c)
{
	if (readonly)
		return 0;
	if(devtab[d->type].write)
		return devtab[d->type].write(d, b, c);
	if(d->type == Devnone)
		return 0;
	panic("illegal device in write: %Z(%lld)\n", d, (Wideoff)b);
	return 1;
}

Devsize
devsize(Device *d)
{
	if(devtab[d->type].size)
		return devtab[d->type].size(d);
	panic("illegal device in devsize: %Z", d);
	return 0;
}

Off
superaddr(Device *d)
{
	if(devtab[d->type].superaddr)
		return devtab[d->type].superaddr(d);
	return SUPER_ADDR;
}

Off
getraddr(Device *d)
{
	if(devtab[d->type].getraddr)
		return devtab[d->type].getraddr(d);
	return ROOT_ADDR;
}

void
devream(Device *d, int top)
{
	print("	devream: %Z %d\n", d, top);

	if(!devtab[d->type].ream){
		print("ream: unknown dev type %Z\n", d);
		return;
	}
	devtab[d->type].ream(d, top);
	devinit(d);
	if(devtab[d->type].c != 'c')		/* BOTCH */
	if(top) {
		wlock(&mainlock);
		rootream(d, ROOT_ADDR);
		superream(d, SUPER_ADDR);
		wunlock(&mainlock);
	}
}

void
devrecover(Device *d)
{
	print("recover: %Z\n", d);
	if(devtab[d->type].recover){
		devtab[d->type].recover(d);
		return;
	}
	print("recover: unknown dev type %Z\n", d);
}

void
devinit(Device *d)
{
	if(d->init)
		return;
	d->init = 1;
	print("	devinit %Z\n", d);
	if(devtab[d->type].init){
		devtab[d->type].init(d);
		return;
	}
	print("devinit unknown device %Z\n", d);
}

int
devsecsize(Device *d)
{
	if(devtab[d->type].secsize == 0)
		return 512;
	return devtab[d->type].secsize(d);
}
