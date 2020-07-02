#include <u.h>
#include <goo.h>

void
sysfatal(char *fmt, ...)
{
	char buf[512];
	va_list args;

	va_start(args, fmt);
	vsnprint(buf, sizeof buf, fmt, args);
	va_end(args);
	fprint(2, "sysfatal: %s\n", buf);
	exits("sysfatal");
}

#ifdef LINUX
char *argv0;

extern void exit(int);

void
exits(char *s)
{
	if(s&&*s)
		exit(1);
	exit(0);
}

#include <errno.h>
extern char* strerror(int);

int
errstr(char* buf, uint n)
{
	int i;
	char *s;

	if(errno == 0){
		if(n)
			*buf = 0;
		return 0;
	}
	s = strerror(errno);
	if(s == 0){
		i = sizeof "unknown";
		s = "unknown";
	}else
		i = strlen(s);
	if(i > n)
		i = n;
	memmove(buf, "unknown", i);
	buf[i] = 0;
	return 0;
}

enum
{
	Bit1	= 7,
	Bitx	= 6,
	Bit2	= 5,
	Bit3	= 4,
	Bit4	= 3,
	Bit5	= 2,

	T1	= ((1<<(Bit1+1))-1) ^ 0xFF,	/* 0000 0000 */
	Tx	= ((1<<(Bitx+1))-1) ^ 0xFF,	/* 1000 0000 */
	T2	= ((1<<(Bit2+1))-1) ^ 0xFF,	/* 1100 0000 */
	T3	= ((1<<(Bit3+1))-1) ^ 0xFF,	/* 1110 0000 */
	T4	= ((1<<(Bit4+1))-1) ^ 0xFF,	/* 1111 0000 */
	T5	= ((1<<(Bit5+1))-1) ^ 0xFF,	/* 1111 1000 */

	Rune1	= (1<<(Bit1+0*Bitx))-1,		/* 0000 0000 0000 0000 0111 1111 */
	Rune2	= (1<<(Bit2+1*Bitx))-1,		/* 0000 0000 0000 0111 1111 1111 */
	Rune3	= (1<<(Bit3+2*Bitx))-1,		/* 0000 0000 1111 1111 1111 1111 */
	Rune4	= (1<<(Bit4+3*Bitx))-1,		/* 0011 1111 1111 1111 1111 1111 */

	Maskx	= (1<<Bitx)-1,			/* 0011 1111 */
	Testx	= Maskx ^ 0xFF,			/* 1100 0000 */

	Bad	= Runeerror,
};

int
chartorune(Rune *rune, char *str)
{
	int c, c1, c2, c3;
	long l;

	/*
	 * one character sequence
	 *	00000-0007F => T1
	 */
	c = *(uchar*)str;
	if(c < Tx) {
		*rune = c;
		return 1;
	}

	/*
	 * two character sequence
	 *	0080-07FF => T2 Tx
	 */
	c1 = *(uchar*)(str+1) ^ Tx;
	if(c1 & Testx)
		goto bad;
	if(c < T3) {
		if(c < T2)
			goto bad;
		l = ((c << Bitx) | c1) & Rune2;
		if(l <= Rune1)
			goto bad;
		*rune = l;
		return 2;
	}

	/*
	 * three character sequence
	 *	0800-FFFF => T3 Tx Tx
	 */
	c2 = *(uchar*)(str+2) ^ Tx;
	if(c2 & Testx)
		goto bad;
	if(c < T4) {
		l = ((((c << Bitx) | c1) << Bitx) | c2) & Rune3;
		if(l <= Rune2)
			goto bad;
		*rune = l;
		return 3;
	}

	/*
	 * four character sequence
	 *	10000-10FFFF => T4 Tx Tx Tx
	 */
	if(UTFmax >= 4) {
		c3 = *(uchar*)(str+3) ^ Tx;
		if(c3 & Testx)
			goto bad;
		if(c < T5) {
			l = ((((((c << Bitx) | c1) << Bitx) | c2) << Bitx) | c3) & Rune4;
			if(l <= Rune3)
				goto bad;
			if(l > Runemax)
				goto bad;
			*rune = l;
			return 4;
		}
	}

	/*
	 * bad decoding
	 */
bad:
	*rune = Bad;
	return 1;
}

int
runenlen(Rune *r, int nrune)
{
	int nb, c;

	nb = 0;
	while(nrune--) {
		c = *r++;
		if(c <= Rune1)
			nb++;
		else
		if(c <= Rune2)
			nb += 2;
		else
		if(c <= Rune3 || c > Runemax)
			nb += 3;
		else
			nb += 4;
	}
	return nb;
}

int
fullrune(char *str, int n)
{
	int c;

	if(n <= 0)
		return 0;
	c = *(uchar*)str;
	if(c < Tx)
		return 1;
	if(c < T3)
		return n >= 2;
	if(UTFmax == 3 || c < T4)
		return n >= 3;
	return n >= 4;
}

char*
utfrune(char *s, long c)
{
	long c1;
	Rune r;
	int n;

	if(c < Runesync)		/* not part of utf sequence */
		return strchr(s, c);

	for(;;) {
		c1 = *(uchar*)s;
		if(c1 < Runeself) {	/* one byte rune */
			if(c1 == 0)
				return 0;
			if(c1 == c)
				return s;
			s++;
			continue;
		}
		n = chartorune(&r, s);
		if(r == c)
			return s;
		s += n;
	}
}

int
getfields(char *str, char **args, int max, int mflag, char *set)
{
	Rune r;
	int nr, intok, narg;

	if(max <= 0)
		return 0;

	narg = 0;
	args[narg] = str;
	if(!mflag)
		narg++;
	intok = 0;
	for(;; str += nr) {
		nr = chartorune(&r, str);
		if(r == 0)
			break;
		if(utfrune(set, r)) {
			if(narg >= max)
				break;
			*str = 0;
			intok = 0;
			args[narg] = str + nr;
			if(!mflag)
				narg++;
		} else {
			if(!intok && mflag)
				narg++;
			intok = 1;
		}
	}
	return narg;
}

#endif
