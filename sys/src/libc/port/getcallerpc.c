#include <u.h>
#include <libc.h>

#pragma profile off
uintptr
getcallerpc(void*)
{
	return 0;
}
#pragma profile on
