#include <u.h>
#include <libc.h>

int
bintime(int fd, vlong *nsec, vlong *ticks, vlong *freq)
{
	uchar b[24];

	if(fd == -1)
		fd = open("/dev/bintime", OREAD|OCEXEC);
	if(fd == -1)
		return -1;
	if(pread(fd, b, sizeof b, 0) != sizeof b){
		close(fd);
		return -1;
	}
	if(nsec != nil)
		*nsec = getbe(b+0, 8);
	if(ticks != nil)
		*ticks = getbe(b+8, 8);
	if(freq != nil)
		*freq = getbe(b+16, 8);
	return fd;
}
