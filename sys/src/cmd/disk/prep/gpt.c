/*
 * efi gpt partition tables
 */
#include <u.h>
#include <libc.h>
#include <flate.h>
#include <efi.h>

typedef struct{
	uchar	sig[8];
	uchar	rev[4];
	uchar	hsize[4];
	uchar	hcrc[4];
	uchar	reserved[4];
	uchar	lba[8];
	uchar	altlba[8];
	uchar	start[8];
	uchar	end[8];
	uchar	guid[16];
	uchar	plba[8];
	uchar	np[4];
	uchar	psize[4];		// we say ≡ 128.
	uchar	pcrc[4];
}Gpt;

/*
HFS [Plus]	48465300-0000-11AA-AA11-00306543ECAC	Apple_HFS
UFS	55465300-0000-11AA-AA11-00306543ECAC	Apple_UFS
Boot	426F6F74-0000-11AA-AA11-00306543ECAC	Apple_Boot
RAID	52414944-0000-11AA-AA11-00306543ECAC	Apple_RAID
Offline RAID	52414944-5F4F-11AA-AA11-00306543ECAC	Apple_RAID_Offline
Label	4C616265-6C00-11AA-AA11-00306543ECAC	Apple_Label
*/

uchar p9partguid[16] = {
	'P', 'l', 'a', 'n', ' ', '9', 1, 0x80,
	0x80, 1, 0, 0, 0, 0, 0, 0
};

typedef struct{
	
}Gentry;

static ulong *crctab;
static void
initcrc(void)
{
	if(crctab)
		return;
	crctab = mkcrctab(0xedb88320);
	if(crctab == 0)
			sysfatal("malloc");
}

void
crcgpt(Gpt *g)
{
	static ulong *tab;

	initcrc();
	memset(g->hcrc, 0, sizeof g->hcrc);
	g->hcrc = blockcrc(tab, 0, g, sizeof *g);
}

Gpt*
initgpt(Gpt *g)
{
	memset(g, 0, sizeof *g);

	memcpy(g->sig, "EFI PART", 8);
	pbit32(g->rev, 0x00010000);
	pbit32(g->hsize, sizeof *g);
	efigid(g->guid);
	pbit32(g->psize, 128);

	return g;
}
