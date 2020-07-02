#include <u.h>
#include <libc.h>
#include <mp.h>
#include <ip.h>

void
main(int argc, char *argv[])
{
	mpint *x, *y;
	char *name, *e, *n;
	int l1, l2, l3, k;
	char buf[4096];

	if(argc != 2){
		fprint(2, "usage: blob2rsa user");
		exits("usage");
	}
	fmtinstall('M', mpfmt);
	k = read(0, buf, 4096);
	if(k < 0)
		fprint(2, "read error: %r\n");
	l1 = nhgetl(buf);
	name = buf + 4;
	l2 = nhgetl(buf+4+l1);
	e = buf+4+l1+4;
	l3 = nhgetl(buf+4+l1+4+l2);
	n = buf+4+l1+4+l2+4;
	name[l1] = 0;
	print("key proto=rsa service=%s user=%s ", name, argv[1]);
	x = betomp((uchar *)e, l2, nil);
	y = betomp((uchar *)n, l3, nil);
	print("ek=%M n=%M\n", x, y);
}
