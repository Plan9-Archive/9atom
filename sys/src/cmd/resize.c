#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

static void
resample(Memimage *dst, Rectangle r, Memimage *src, Rectangle sr, int bilinear)
{
	uchar *pdst0, *pdst, *psrc0, *psrc;
	ulong s00, s01, s10, s11;
	int tx, ty, b, bpp, bpl;
	Point sp, dp, _sp, qp, ssize, dsize;

	ssize = subpt(subpt(sr.max, sr.min), Pt(1, 1));
	dsize = subpt(subpt(r.max, r.min), Pt(1, 1));
	pdst0 = byteaddr(dst, r.min);
	bpp = src->depth/8;
	bpl = src->width*sizeof(int);

	qp.x = (ssize.x<<12)/(dsize.x==0? 1: dsize.x);
	qp.y = (ssize.y<<12)/(dsize.y==0? 1: dsize.y);
	_sp.y = sr.min.y<<12;
	for(dp.y=0; dp.y<=dsize.y; dp.y++){
		sp.y = _sp.y>>12;
		ty = _sp.y&0xFFF;
		if(!bilinear && ty >= 0x800)
			sp.y++;
		pdst = pdst0;
		sp.x = sr.min.x;
		psrc0 = byteaddr(src, sp);
		_sp.x = 0;
		for(dp.x=0; dp.x<=dsize.x; dp.x++){
			sp.x = _sp.x>>12;
			tx = _sp.x&0xFFF;
			psrc = psrc0 + sp.x*bpp;
			if(!bilinear){
				if(tx >= 0x800)
					psrc += bpp;
				for(b=0; b<bpp; b++)
					pdst[b] = psrc[b];
			}else{
				s00 = (0x1000-tx)*(0x1000-ty);
				s01 = tx*(0x1000-ty);
				s10 = (0x1000-tx)*ty;
				s11 = tx*ty;
				for(b=0; b<bpp; b++)
					pdst[b] = s11*psrc[bpl+bpp+b] + s10*psrc[bpl+b]
						+ s01*psrc[bpp+b] + s00*psrc[b] >>24;
			}
			pdst += bpp;
			_sp.x += qp.x;
		}
		pdst0 += dst->width*sizeof(int);
		_sp.y += qp.y;
	}
}

void
usage(void)
{
	fprint(2, "usage: resize -s width height [-b] [image]\n");
	exits("usage");
}

int
getint(char *s, int *percent)
{
	int i;

	i = strtol(s, &s, 0);
	if(i < 0)
		i = -i;
	*percent = *s == '%';
	return i;
}


void
main(int argc, char **argv)
{
	char buf[128];
	int tchan, bilinear, percent[2];
	Memimage *im, *nim, *cvt;
	Rectangle r;

	r = Rect(0, 0, 0, 0);
	bilinear = 0;
	memset(percent, 0, sizeof percent);
	ARGBEGIN{
	case 'b':
		bilinear = 1;
		break;
	case 's':
		r.max.x = getint(EARGF(usage()), percent);
	case 'y':
		r.max.y = getint(EARGF(usage()), percent + 1);
		break;
	case 'x':
		r.max.x = getint(EARGF(usage()), percent);
		break;
	default:
		usage();
	}ARGEND
	if(argc > 1)
		usage();
	if(Dx(r) == 0 && Dy(r) == 0)
		usage();
	memimageinit();
	im = readmemimage(open(argv[0]? argv[0] : "/fd/0", OREAD));
	if(im == nil)
		sysfatal("readmemimage: %r");
	if(percent[0] != 0)
		r.max.x = Dx(im->r)*Dx(r)/100;
	if(percent[1] != 0)
		r.max.y = Dy(im->r)*Dy(r)/100;
	if(r.max.y == 0)
		r.max.y = (Dx(r) * Dy(im->r)) / Dx(im->r);
	if(r.max.x == 0)
		r.max.x = (Dy(r) * Dx(im->r)) / Dy(im->r);

	switch(im->chan){
	case RGB24:
		break;
	case RGBA32:
	case ARGB32:
	case XRGB32:
	case CMAP8:
	case RGB15:
	case RGB16:
		tchan = RGB24;
		goto Convert;

	case GREY1:
	case GREY2:
	case GREY4:
	case GREY8:
		tchan = RGB24;
	Convert:
		cvt = allocmemimage(im->r, tchan);
		if(cvt == nil)
			sysfatal("allocmemimage: %r");
		memimagedraw(cvt, cvt->r, im, im->r.min, nil, ZP, S);
		im = cvt;
		break;
	default:
		sysfatal("can't handle channel type %s", chantostr(buf, im->chan));
	}

	nim = allocmemimage(r, im->chan);
	resample(nim, r, im, im->r, bilinear);
	writememimage(1, nim);
	exits(0);
}
