#include <u.h>
#include <libc.h>
#include <disk.h>
#include <ctype.h>

enum {
	OPmodesense	= 0x1a,		// scsii opcodes
	OPlogsense	= 0x4d,
	OPreaddefects	= 0x37,

	LSwriteerr	= 0x02,		// log sense pages
	LSreaderr	= 0x03,
	LStemp		= 0x0d,
	LSstartstop	= 0x0e,

	Primary		= 0x10,		// read defects flags
	Grown		= 0x08,

	Cumulative	= (1<<6),	// log sence data source

	Ecorrected_fast	= 0,		// Errors corrected without substantial delay
	Ecorrected_slow	= 1,		// Errors corrected with possible delays
	Ebytes_xfered	= 2,		// Total (e.g. rewrites or rereads) 
	Etot_corrected	= 3,		// Total errors corrected
	Ealg_run	= 4,		// Total times correction algorithm processed
	Ebytes		= 5,		// Total bytes processed
	Euncorrected	= 6,		// Total uncorrected errors
};


void
life(Scsi *dev)
{
	double life, used;
	uchar cmd[10], buf[128];

	life = 0;
	used = 0;
	memset(cmd, 0, sizeof(cmd));
        cmd[0] = OPlogsense;
        cmd[2] = LSstartstop|Cumulative;
        cmd[7] = (sizeof(buf)>>8)&0xff;	
        cmd[8] = sizeof(buf)&0xff;

	if (scsi(dev, cmd, sizeof(cmd), buf, sizeof(buf), Sread) >= 37){
		used = (buf[28]<<24)|(buf[29]<<16)|(buf[30]<<16)|buf[31];
		life = (buf[34]<<24)|(buf[35]<<16)|(buf[36]<<16)|buf[37];
		print("%6.2f%%", (used/life)*100.);
	}
	else
		print("     ?%%");
}

void
errors(Scsi *dev, int logpage)
{
	uchar cmd[10], buf[128];
	int i, j, n, code, len, value, errs[6];
	
	memset(cmd, 0, sizeof(cmd));
        cmd[0] = OPlogsense;
        cmd[2] = logpage|Cumulative;
        cmd[7] = (sizeof(buf)>>8)&0xff;	
        cmd[8] = sizeof(buf)&0xff;

	memset(errs, 0, sizeof(errs));
	if ((n = scsi(dev, cmd, sizeof(cmd), buf, sizeof(buf), Sread)) >= 6){
		for (i = 4; i < n;){
			code = (buf[i]<<8)|buf[i+1];
			i++;				// flags
			len = buf[i++];
			for (value = 0, j = 0; j < len; j++)
				value = (value<<8)|buf[i++];
			if (code >= 0 && code <= nelem(errs))
				errs[code] = value;
		}
	}
	if (n == -1)
		print("       ?");
	else
		print("%8g", (double)(errs[Ecorrected_fast]+
			errs[Ecorrected_slow]+ errs[Etot_corrected]));
}

void
temp(Scsi *dev)
{
	int n;
	uchar cmd[10], buf[128];

	memset(cmd, 0, sizeof(cmd));
        cmd[0] = OPlogsense;
        cmd[2] = LStemp|Cumulative;
        cmd[7] = (sizeof(buf)>>8)&0xff;	
        cmd[8] = sizeof(buf)&0xff;

	if ((n = scsi(dev, cmd, sizeof(cmd), buf, sizeof(buf), Sread)) >= 15){
		print("  %3d°C/%d°C ", buf[9], buf[15]);
	}
	else
	if (n >= 9){
		print("  %3d°C/?°C ", buf[9]);
	}
	else
		print("    ?°C/?°C ");
}

void
defects(Scsi *dev)
{
	int defects;
	uchar cmd[10], buf[64];

	defects = -1;
	memset(cmd, 0, sizeof(cmd));
        cmd[0] = OPreaddefects;
        cmd[8] = sizeof(buf);
	if (scsi(dev, cmd, sizeof(cmd), buf, 6, Sread) >= 3){
		switch(buf[1] & 7){
		case 0:			// block format
			defects = ((buf[2]<<8) | buf[3])/2;
			break;
		case 4:			// index format
			defects = ((buf[2]<<8) | buf[3])/8;
			break;
		case 5:			// phys sector format
			defects = ((buf[2]<<8) | buf[3])/8;
			break;
		case 6:			// vendor specific
			defects = (buf[2]<<8) | buf[3];
			break;
		}
	}
	if (defects != -1)
		print("%6g  ", (double)defects+1);
	else
		print("     ?  ");
}

void
main(int argc, char *argv[])
{
	int i;
	Scsi *dev;

	if (argc < 2){
		fprint(2, "usage: %s /dev/sdXX ...\n", argv[0]);
		exits("usage");
	}

       	for (i = 1; i < argc; i++){
		if((dev = openscsi(argv[i])) == nil){
			fprint(2, "%s: %s - %r\n", argv[0], argv[i]);
			continue;
		}
		print("%-16s ", argv[i]);
		defects(dev);
		errors(dev, LSreaderr);
		errors(dev, LSwriteerr);
		life(dev);
		temp(dev);
		print("\n");
		closescsi(dev);
	}
	exits(0);
}

