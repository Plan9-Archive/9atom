#include <u.h>
#include <libc.h>
#include <bio.h>

enum {
	Decode	= 0<<0,
	Encode	= 1<<0,,

	Size16	= 0<<1,
	Size32	= 1<<1,
	Size64	= 2<<1,
};

typedef int (Code)(uchar*, int, char*, int);

void
xcode(Biobuf *b, Code *f)
{
	char *s;
	uchar out[8192];
	int n;

	for(; s = Brdstr(b, '\n', 1); free(s)){
		n = f(out, sizeof out, s, strlen(s));
		if(n == -1)
			sysfatal("decode error: %r");
		write(1, out, n);
	}
}

void
usage(void)
{
	fprint(2, "usage: dec64 [-h36] ....\n");
	exits("usage");
}

Code *codetab[] = {
[Size16 | Decode]		dec16,
//[Size16 | Encode]		enc16,
[Size32 | Decode]		dec32,
//[Size32 | Encode]		enc32,
[Size64 | Decode]		dec64,
//[Size64 | Encode]		enc64,
};

void
main(int argc, char **argv)
{
	int code, size, i;
	Biobuf b0, *b;

	code = Decode;
	size = Size64;
	ARGBEGIN{
	case 'd':
		code = Decode;
		break;
//	case 'e':
//		code = Encode;
//		break;
	case 'h':
		size = Size16;
		break;
	case '3':
		size = Size32;
		break;
	case '6':
		size = Size64;
		break;
	}ARGEND

	if(argc == 0){
		if(Binit(&b0, 0, OREAD) == -1)
			sysfatal("Binit: %r");
		xcode(&b0, codetab[size|code]);
		Bterm(&b0);
	}else for(i = 0; i < argc; i++){
		b = Bopen(argv[i], OREAD);
		if(b == nil)
			sysfatal("Bopen: %r");
		xcode(b, codetab[size|code]);
		Bterm(b);
	}
}
