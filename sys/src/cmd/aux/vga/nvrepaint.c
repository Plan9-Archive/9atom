#include <u.h>
#include <libc.h>
#include <bio.h>

#include "pci.h"
#include "vga.h"

int dflag = 1;
int vflag = 1;
Ctlr palette;

void
sequencer(Vga* vga, int on)
{
	if (vga || on != 1)
		sysfatal("bad sequencer arguments");
}


void
main(int, char **)
{
	vgaxo(Crtx, 0x19, 64);
	vgaxo(Crtx, 0x1A, 0);
}
