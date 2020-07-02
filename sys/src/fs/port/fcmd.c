#include	"all.h"

int
fchar(void)
{
	int n;

	n = BUFSIZE;
	if(n > MAXDAT)
		n = MAXDAT;
	if(uidgc.find >= uidgc.flen) {
		uidgc.find = 0;
		uidgc.flen = con_read(FID2, uidgc.uidbuf->iobuf, cons.offset, n);
		if(uidgc.flen <= 0)
			return -1;
		cons.offset += uidgc.flen;
	}
	return uidgc.uidbuf->iobuf[uidgc.find++] & 0xff;
}

int
fname(char *name)
{
	int i, c;

	/*
	 * read a name and return first char known not
	 * to be in the name.
	 */
	memset(name, 0, NAMELEN);
	for(i=0;; i++) {
		c = fchar();
		switch(c) {
		case '#':
			for(;;) {
				c = fchar();
				if(c == -1 || c == '\n')
					break;
			}

		case ' ':
		case '\n':

		case ':':
		case ',':
		case '=':
		case 0:
			return c;

		case -1:
			return 0;

		case '\t':
			return ' ';
		}
		if(i < NAMELEN-1)
			name[i] = c;
	}
}

