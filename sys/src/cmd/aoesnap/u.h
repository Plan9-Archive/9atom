typedef unsigned char	uchar;
typedef unsigned short	ushort;
typedef unsigned int	uint;
typedef unsigned long	ulong;
typedef unsigned long long	uvlong;
typedef long long		vlong;
typedef unsigned int	Rune;

#define	SET(x)	((x)=0)
#define	USED(x)	if(x){}else{}
#ifdef __GNUC__
#	if __GNUC__ >= 3
#		undef USED
#		define USED(x) { ulong __y __attribute__ ((unused)); __y = (ulong)(x); }
#	endif
#endif

#define create(a, b, c)	creat(a, c)
#include <stdarg.h>
