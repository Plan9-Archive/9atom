#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>

typedef struct Convert	Convert;

struct Convert {
	char *name;
	char *cmd;
	char *truecmd;	/* cmd for true color */
};

enum {
	Ipic,
	Itiff,
	Ijpeg,
	Igif,
	Iinferno,
	Ifax,
	Icvt2pic,
	Iplan9bm,
	Iccittg4,
	Ippm,
	Ipng,
	Iyuv,
	Ibmp,
	Iico,
	Itga,

	Ipdf,
	Ips,
	Idvi,
	Ims,
	Iroff,
};

/*
 * N.B. These commands need to read stdin if %a is replaced
 * with an empty string.
 */
Convert cvt[] = {
[Ipic]		{ "plan9",	"fb/3to1 rgbv %a |fb/pcp -tplan9" },
[Itiff]		{ "tiff",	"fb/tiff2pic %a | fb/3to1 rgbv | fb/pcp -tplan9" },
[Iplan9bm]	{ "plan9bm",	"cat %a",	"cat %a" },
[Ijpeg]		{ "jpeg",	"jpg -9 %a", 	"jpg -t9 %a" },
[Igif]		{ "gif",	"gif -9 %a", 	"gif -t9 %a" },
[Iinferno]	{ "inferno",	nil },
[Ifax]		{ "fax",	"aux/g3p9bit -g %a" },
[Icvt2pic]	{ "unknown",	"fb/cvt2pic %a |fb/3to1 rgbv" },
[Ippm]		{ "ppm",		"ppm -9 %a",	"ppm -t9 %a" },
/* ``temporary'' hack for hobby */
[Iccittg4]	{ "ccitt-g4",	"cat %a|rx nslocum /usr/lib/ocr/bin/bcp -M|fb/pcp -tcompressed -l0" },
[Ipng]		{ "png",	"png -9 %a",	"png -t9 %a" },
[Iyuv]		{ "yuv",	"yuv -9 %a", 	"yuv -t9 %a" },
[Ibmp]		{ "bmp",		"bmp -9 %a", 	"bmp -t9 %a" },
[Iico]		{ "ico",	"ico -9 %a",	"ico -t9 %a" },
[Itga]		{ "tga",	"tga -9 %a",	"tga -t9 %a" },
[Ipdf]		{ "pdf", },
[Ips]		{ "ps", },
[Idvi]		{ "dvi", },
[Ims]		{ "ms", },
[Iroff]		{ "troff", },
};

static int
afmt(Fmt *fmt)
{
	char *s;

	s = va_arg(fmt->args, char*);
	if(s == nil || s[0] == '\0')
		return fmtstrcpy(fmt, "");
	else
		return fmtprint(fmt, "%q", s);
}

enum {
	Nbuf	= 32,
};

static int
type(char *name, char *ibuf, int nibuf)
{
	char buf[Nbuf+1];
	uchar xbuf[Nbuf];
	int l, type;
	Biobuf *b;

	l = 0;
	if(name){
		l = strlen(name);
		if((b = Bopen(name, OREAD)) == nil) {
			werrstr("Bopen: %r");
			return -1;
		}

		if(Bread(b, xbuf, sizeof xbuf) != sizeof xbuf) {
			werrstr("short read: %r");
			return -1;
		}
		Bterm(b);
		memcpy(buf, xbuf, Nbuf);
		buf[32] = 0;
	}else{
		if(nibuf > Nbuf)
			nibuf = Nbuf;
		memcpy(buf, ibuf, nibuf);
		memset(buf + nibuf, 0, Nbuf+1-nibuf);
	}

	if(memcmp(buf, "%PDF-", 5) == 0)
		type = Ipdf;
	else if(memcmp(buf, "\x04%!", 2) == 0)
		type = Ips;
	else if(buf[0] == '\x1B' && strstr((char*)buf, "@PJL"))
		type = Ips;
	else if(memcmp(buf, "%!", 2) == 0)
		type = Ips;
	else if(memcmp(buf, "\xF7\x02\x01\x83\x92\xC0\x1C;", 8) == 0)
		type = Idvi;
	else if(memcmp(buf, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0)
		type = Ims;
	else if(strncmp((char*)buf, "x T ", 4) == 0)
		type = Iroff;
	else if(memcmp(buf, "GIF", 3) == 0)
		type = Igif;
	else if(memcmp(buf, "\111\111\052\000", 4) == 0) 
		type = Itiff;
	else if(memcmp(buf, "\115\115\000\052", 4) == 0)
		type = Itiff;
	else if(memcmp(buf, "\377\330\377", 3) == 0)
		type = Ijpeg;
	else if(memcmp(buf, "\211PNG\r\n\032\n", 3) == 0)
		type = Ipng;
	else if(memcmp(buf, "compressed\n", 11) == 0)
		type = Iinferno;
	else if(memcmp(buf, "\0PC Research, Inc", 17) == 0)
		type = Ifax;
	else if(memcmp(buf, "TYPE=ccitt-g31", 14) == 0)
		type = Ifax;
	else if(memcmp(buf, "II*", 3) == 0)
		type = Ifax;
	else if(memcmp(buf, "TYPE=ccitt-g4", 13) == 0)
		type = Iccittg4;
	else if(memcmp(buf, "TYPE=", 5) == 0)
		type = Ipic;
	else if(buf[0] == 'P' && '0' <= buf[1] && buf[1] <= '9')
		type = Ippm;
	else if(memcmp(buf, "BM", 2) == 0)
		type = Ibmp;
	else if(memcmp(buf, "          ", 10) == 0 &&
		'0' <= buf[10] && buf[10] <= '9' &&
		buf[11] == ' ')
		type = Iplan9bm;
	else if(strtochan((char*)buf) != 0)
		type = Iplan9bm;
	else if (l > 4 && strcmp(name + l -4, ".yuv") == 0)
		type = Iyuv;
	else if (l > 4 && strcmp(name + l -4, ".ico") == 0)
		type = Iico;
	else if (l > 4 && strcmp(name + l -4, ".tga") == 0)
		type = Itga;
	else
		type = Icvt2pic;
	return type;
}

uchar rbuf[64*1024];
void
runit(char *s, char *name, char *buf, int n)
{
	int p[2];

	if(name == 0 && n > 0){
		if(pipe(p) == -1)
			sysfatal("pipe: %r");
		switch(fork()){
		case -1:
			sysfatal("fork: %r");
		default:
			close(p[1]);
			dup(p[0], 0);
			break;
		case 0:
			close(p[0]);
			write(p[1], buf, n);
			for(;;){
				n = read(0, rbuf, sizeof rbuf);
				if(n <= 0)
					break;
				write(p[1], rbuf, n);
			}
			exits("");
		}
	}
	execl("/bin/rc", "rc", "-c", s, nil);
	exits("exec");
}

void
usage(void)
{
	fprint(2, "usage: imgtype [-Bet] ...\n");
	exits("usage");
}

char flag[0x80];
char *buf[1024];
int nbuf;

void
main(int argc, char **argv)
{
	char *fmt, *m, *name, xbuf[Nbuf+1];
	int i, n, t;
	Biobuf o;
	Convert *c;

	quotefmtinstall();
	fmtinstall('a', afmt);

	ARGBEGIN{
	case 'b':
		if(nbuf == nelem(buf))
			sysfatal("too many bufs");
		buf[nbuf++] = EARGF(usage());
		break;
	case 'B':
	case 'e':
	case 't':
		flag[ARGC()] = 1;
		break;
	default:
		usage();
	}ARGEND
	if(nbuf > 0 && flag['e'])
		usage();
	Binit(&o, 1, OWRITE);
	for(i = 0;; i++){
		n = 0;
		if(i < argc)
			t = type(argv[i], nil, 0);
		else if(i < argc + nbuf)
			t = type(nil, buf[i-argc], strlen(buf[i-argc]));
		else if(flag['B'] || i == 0){
			n = read(0, xbuf, sizeof xbuf-1);
			if(n == 0)
				break;
			if(n == -1)
				sysfatal("read: %r");
			xbuf[n] = 0;
			t = type(nil, xbuf, n);
		}else
			break;
		if(t == -1)
			sysfatal("type: %r");
		c = cvt + t;
		fmt = c->cmd;
		if(flag['t'])
			fmt = c->truecmd;
		if(fmt == nil)
			sysfatal("no format for %s\n", c->name);
		name =  i < argc? argv[i]: nil;
		m = smprint(fmt, name);
		if(flag['e'] == 0)
			Bprint(&o, "%s\n", m);
		else
			runit(m, name, xbuf, n);
		free(m);
	}

	Bterm(&o);
	exits("");
}
