#include "all.h"

Devsize
wormsize(Device*)
{
	return 0;
}

int
wormwrite(Device*, Devsize, void*)
{
	return 1;
}

int
wormread(Device*, Devsize, void*)
{
	return 1;
}

Devsize
wormsizeside(Device*, int)
{
	return 0;
}

void
wormsidestarts(Device *, int, Sidestarts*)
{
}

void
jukeinit(Device*)
{
}

void
wormprobe(void)
{
}
