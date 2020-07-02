typedef struct Dospart	Dospart;

struct Dospart
{
	uchar	flag;		/* active flag */
	uchar	shead;		/* starting head */
	uchar	scs[2];		/* starting cylinder/sector */
	uchar	type;		/* partition type */
	uchar	ehead;		/* ending head */
	uchar	ecs[2];		/* ending cylinder/sector */
	uchar	start[4];	/* starting sector */
	uchar	len[4];		/* length in sectors */
};

enum{
	FAT12		= 0x01,
	FAT16		= 0x04,
	EXTEND	= 0x05,
	FATHUGE	= 0x06,
	FAT32		= 0x0b,
	FAT32X		= 0x0c,
	EXTHUGE	= 0x0f,
	DMDDO	= 0x54,
	PLAN9		= 0x39,
	LEXTEND	= 0x85,
};

#define	GSHORT(p) (((p)[1]<<8)|(p)[0])
#define	GLONG(p) ((GSHORT(p+2)<<16)|GSHORT(p))
