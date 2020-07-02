#include <u.h>
#include <libc.h>
static char t16e[] = "0123456789ABCDEF";

int
dec16(uchar *out, int lim, char *in, int n)
{
	int c, w = 0, i = 0;
	uchar *start = out;
	uchar *eout = out + lim;

	while(n-- > 0){
		c = *in++;
		if('0' <= c && c <= '9')
			c = c - '0';
		else if('a' <= c && c <= 'z')
			c = c - 'a' + 10;
		else if('A' <= c && c <= 'Z')
			c = c - 'A' + 10;
		else
			continue;
		w = (w<<4) + c;
		i++;
		if(i == 2){
			if(out + 1 > eout)
				return -1;
			*out++ = w;
			w = 0;
			i = 0;
		}
	}
	return out - start;
}

int
enc16(char *out, int lim, uchar *in, int n)
{
	uint c;
	char *start = out;

	if(2*n+1 > lim)
		return -1;
	while(n-- > 0){
		c = *in++;
		*out++ = t16e[c>>4];
		*out++ = t16e[c&0xf];
	}
	*out = 0;
	return out - start;
}
