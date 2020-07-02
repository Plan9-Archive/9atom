#include <u.h>
#include <libc.h>

#define SIGN (1<<31)

double
fabs(double arg)
{
	FPdbleword x;

	x.x = arg;
	x.hi &= ~SIGN;
	return x.x;
}
