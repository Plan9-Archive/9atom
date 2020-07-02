#include	<u.h>
#include	<libc.h>
#include	<bio.h>

int
Bputc(Biobufhdr *bp, int c)
{
	int i;

	assert(bp->state != Bractive && bp->state != Bracteof);
	for(;;) {
		i = bp->ocount;
		if(i) {
			bp->ebuf[i++] = c;
			bp->ocount = i;
			return 0;
		}
		if(Bflush(bp) == Beof)
			break;
	}
	return Beof;
}
