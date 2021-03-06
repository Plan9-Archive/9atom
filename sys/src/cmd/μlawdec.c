#include <u.h>
#include <libc.h>
#include <bio.h>

enum {
	Qmask	= 0xf,		/* quantization mask */
	Nsegs	= 8,		/* A-law segments */
	Segshift	= 4,
	Segmask	= 0x70,
};

static short segend[Nsegs] = {
	0xff,	0x1ff,	0x3ff,	0x7ff,
	0xfff,	0x1fff,	0x3fff,	0x7fff
};

/* copy from CCITT G.711 specifications */
static uchar u2a[128] = {			/* μ- to A-law conversions */
	1,	1,	2,	2,	3,	3,	4,	4,
	5,	5,	6,	6,	7,	7,	8,	8,
	9,	10,	11,	12,	13,	14,	15,	16,
	17,	18,	19,	20,	21,	22,	23,	24,
	25,	27,	29,	31,	33,	34,	35,	36,
	37,	38,	39,	40,	41,	42,	43,	44,
	46,	48,	49,	50,	51,	52,	53,	54,
	55,	56,	57,	58,	59,	60,	61,	62,
	64,	65,	66,	67,	68,	69,	70,	71,
	72,	73,	74,	75,	76,	77,	78,	79,
	81,	82,	83,	84,	85,	86,	87,	88,
	89,	90,	91,	92,	93,	94,	95,	96,
	97,	98,	99,	100,	101,	102,	103,	104,
	105,	106,	107,	108,	109,	110,	111,	112,
	113,	114,	115,	116,	117,	118,	119,	120,
	121,	122,	123,	124,	125,	126,	127,	128
};

static uchar a2u[128] = {			/* A- to μ-law conversions */
	1,	3,	5,	7,	9,	11,	13,	15,
	16,	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,	31,
	32,	32,	33,	33,	34,	34,	35,	35,
	36,	37,	38,	39,	40,	41,	42,	43,
	44,	45,	46,	47,	48,	48,	49,	49,
	50,	51,	52,	53,	54,	55,	56,	57,
	58,	59,	60,	61,	62,	63,	64,	64,
	65,	66,	67,	68,	69,	70,	71,	72,
	73,	74,	75,	76,	77,	78,	79,	79,
	80,	81,	82,	83,	84,	85,	86,	87,
	88,	89,	90,	91,	92,	93,	94,	95,
	96,	97,	98,	99,	100,	101,	102,	103,
	104,	105,	106,	107,	108,	109,	110,	111,
	112,	113,	114,	115,	116,	117,	118,	119,
	120,	121,	122,	123,	124,	125,	126,	127
};

/* speed doesn't matter.  table has 8 entires */
static int
search(int val, short *table, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (val <= *table++)
			return i;
	}
	return size;
}

/*
 * linear2alaw() - Convert a 16-bit linear PCM value to 8-bit A-law
 *
 *		Linear Input Code	Compressed Code
 *	------------------------	---------------
 *	0000000wxyza			000wxyz
 *	0000001wxyza			001wxyz
 *	000001wxyzab			010wxyz
 *	00001wxyzabc			011wxyz
 *	0001wxyzabcd			100wxyz
 *	001wxyzabcde			101wxyz
 *	01wxyzabcdef			110wxyz
 *	1wxyzabcdefg			111wxyz
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */
uchar
linear2alaw(int pcm)
{
	uchar a;
	int mask, seg;

	if (pcm >= 0) {
		mask = 0x80 | 0x55;
	} else {
		mask = 0x00 | 0x55;
		pcm = -pcm - 8;
	}

	/* Convert the scaled magnitude to segment number. */
	seg = search(pcm, segend, 8);

	/* Combine the sign, segment, and quantization bits. */
	if (seg >= 8)
		/* out of range, return maximum value. */
		return 0x7f ^ mask;
	else {
		a = seg << Segshift;
		if (seg < 2)
			a |= pcm>>4 & Qmask;
		else
			a |= pcm>>(seg + 3) & Qmask;
		return a ^ mask;
	}
}

/*
 * alaw2linear() - Convert an A-law value to 16-bit linear PCM
 */
int
alaw2linear(uchar a)
{
	int t, seg;

	a ^= 0x55;
	t = (a & Qmask) << 4;
	seg = (a & Segmask) >> Segshift;
	if(seg == 0)
		t += 0x008;
	else if(seg == 1)
		t += 0x108;
	else{
		t += 0x108;
		t <<= seg - 1;
	}
	return a&0x80? t: -t;
}

enum {
	Bias	= 0x84,		/* Bias for linear code. */
};

/*
 * linear2μlaw() - Convert a linear PCM value to μ-law
 *
 * In order to simplify the encoding process, the original linear magnitude
 * is biased by adding 33 which shifts the encoding range from (0 - 8158) to
 * (33 - 8191). The result can be seen in the following encoding table:
 *
 *	Biased Linear Input Code	Compressed Code
 *	------------------------	---------------
 *	00000001wxyza			000wxyz
 *	0000001wxyzab			001wxyz
 *	000001wxyzabc			010wxyz
 *	00001wxyzabcd			011wxyz
 *	0001wxyzabcde			100wxyz
 *	001wxyzabcdef			101wxyz
 *	01wxyzabcdefg			110wxyz
 *	1wxyzabcdefgh			111wxyz
 *
 * Each biased linear code has a leading 1 which identifies the segment
 * number. The value of the segment number is equal to 7 minus the number
 * of leading 0's. The quantization interval is directly available as the
 * four bits wxyz.  * The trailing bits (a - h) are ignored.
 *
 * Ordinarily the complement of the resulting code word is used for
 * transmission, and so the code word is complemented before it is returned.
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */
uchar
linear2μlaw(int pcm)
{
	uchar μ;
	int mask, seg;

	/* Get the sign and the magnitude of the value. */
	if (pcm < 0) {
		pcm = Bias - pcm;
		mask = 0x7f;
	} else {
		pcm += Bias;
		mask = 0xff;
	}

	/* Convert the scaled magnitude to segment number. */
	seg = search(pcm, segend, 8);

	/*
	 * Combine the sign, segment, quantization bits;
	 * and complement the code word.
	 */
	if (seg >= 8)
		/* out of range, return maximum value. */
		return 0x7f ^ mask;
	else {
		μ = seg<< 4 | pcm>>(seg + 3) & 0xf;
		return μ ^ mask;
	}
}

/*
 * μlaw2linear() - Convert a μ-law value to 16-bit linear PCM
 *
 * First, a biased linear code is derived from the code word. An unbiased
 * output can then be obtained by subtracting 33 from the biased code.
 *
 * Note that this function expects to be passed the complement of the
 * original code word. This is in keeping with ISDN conventions.
 */
int
μlaw2linear(uchar μ)
{
	int t;

	/* Complement to obtain normal μ-law value. */
	μ = ~μ;

	/*
	 * Extract and bias the quantization bits. Then
	 * shift up by the segment number and subtract out the bias.
	 */
	t = ((μ & Qmask) << 3) + Bias;
	t <<= (μ & Segmask) >> Segshift;

	return μ & 0x80? Bias - t: t - Bias;
}

/* A-law to μ-law conversion */
uchar
alaw2μlaw(uchar a)
{
//	a &= 0xff;
	if(a & 0x80)
		return 0xff ^ a2u[a ^ 0xd5];
	else
		return 0x7f ^ a2u[a ^ 0x55];
}

/* μ-law to A-law conversion */
uchar
μlaw2alaw(uchar μ)
{
//	μ &= 0xff;
	if(μ & 0x80)
		return 0xd5 ^ u2a[0xff ^ μ] - 1;
	else
		return 0x55 ^ u2a[0x7f ^ μ] - 1;
}

char flag[127];

static void
chat(int fd, char *fmt, ...)
{
	va_list args;

	if(flag['d'] == 0)
		return;
	va_start(args, fmt);
	vfprint(fd, fmt, args);
	va_end(args);
}

static void
setrate(int rate)
{
	int fd;

	fd = open("/dev/volume", OWRITE);
	if(fd == -1){
		fprint(2, "μlawdec: can't set rate %d: open: %r\n", rate);
		return;
	}
	if(fprint(fd, "speed %d", rate) == -1)
		fprint(2, "μlawdec: can't set rate %d: fprint: %r\n", rate);
	else
		chat(2, "μlawdec: rate %d\n", rate);
	close(fd);
}

enum {
	Sig	= 0,
	Offset	= 1,
	Enc	= 3,
	Rate	= 4,
	Nchan	= 5,
};

u32int
μlawword(uchar *buf, int w)
{
	return getbe(buf+4*w, 4);
}

void
μlawdec(int fd)
{
	int n;
	uchar buf[5*4];
	vlong off;
	Biobuf i, *o;

	if(Binit(&i, fd, OREAD) == -1)
		sysfatal("μlawdec: Binit: %r");
	if(Bread(&i, buf, sizeof buf) != sizeof buf)
		sysfatal("μlawdec: Bread: %r");
	off = μlawword(buf, Offset);
	if(off < 24)
		sysfatal("μlawdec: bad offset: %lld", off);
	if(μlawword(buf, Sig) != 0x2e736e64)
		sysfatal("μlawdec: not .au file");
	if(μlawword(buf, Sig) != 0x2e736e64)
		sysfatal("μlawdec: not .au file");
	if(μlawword(buf, Enc) != 1)
		sysfatal("μlawdec: not μlaw");
	setrate(μlawword(buf, Rate));

	Bseek(&i, off, 0);

	if((o = Bopen("/dev/audio", OWRITE)) == nil)
		sysfatal("μlawdec: Bopen: %r");

	for(;;){
		switch(Bread(&i, buf, 1)){
		case 0:
			goto done;
		case -1:
			sysfatal("μlawdec: Bread: %r");
		}
		n = μlaw2linear(buf[0]);
		buf[0] = n&0xff;
		buf[1] = (n&0xff00)>>8;
		buf[2] = buf[0];			/* fake stereo */
		buf[3] = buf[1];

		if(Bwrite(o, buf, 4) != 4)
			sysfatal("μlawdec: Bwrite: %r");
	}
done:
	Bterm(o);
	Bterm(&i);

	setrate(44100);
}

void
usage(void)
{
	fprint(2, "usage: μlawdec ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, fd;

	ARGBEGIN{
	case 'd':
		flag[ARGC()] = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc == 0)
		μlawdec(0);
	else
		for(i = 0; i < argc; i++){
			fd = open(argv[i], OREAD);
			if(fd == -1)
				sysfatal("μlawdec: open: %r");
			μlawdec(fd);
			close(fd);
		}
}
