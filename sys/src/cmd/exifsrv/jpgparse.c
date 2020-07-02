#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include "exif.h"

static jmp_buf Failed;
static Img Imgs[32];		// FIXME: should really be realloc'ed on demand
static int Verbose = 0;

static void
fail(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	vfprint(2, fmt, arg);
	va_end(arg);
	longjmp(Failed, 1);
}

/********************************************/

int
rint(Img *ip, int n)
{
	int i, val, x;

	val = 0;
	for(i = 0; i < n; i++){
		if((x = Bgetc(ip->bp)) == -1)
			fail("%s unexpected EOF - %r\n", ip->file);
		if(ip->intel)
			val |= x << i*8;
		else
			val |= x << (n-i-1)*8;
	}
	return val;
}

void
rmem(Img *ip, void *buf, int n)
{
	memset(buf, 0, n);
	if(Bread(ip->bp, buf, n) != n)
		fail("%s unexpected EOF - %r\n", ip->file);
}

/********************************************/

char *
val2name(Exif *ex, int val)
{
	Namval *nv;

	if(ex->nv == nil)
		return nil;
	for(nv = ex->nv; nv->name; nv++)
		if(nv->val == val)
			return nv->name;
	return nil;
}

void
tag_distance(Img *ip, int base, Exif *, int, int, int val)
{
	double a, b;

	Bseek(ip->bp, base+val, 0);
	a = rint(ip, 4);
	b = rint(ip, 4);
	fmtprint(&ip->mfmt, "%g m", a/b);
}

void
tag_lens(Img *ip, int base, Exif *, int, int, int val)
{
	double a, b;

	Bseek(ip->bp, base+val, 0);
	a = rint(ip, 4);
	b = rint(ip, 4);
	fmtprint(&ip->mfmt, "%g mm\n", a/b);
}

void
tag_apex(Img *ip, int base, Exif *ep, int, int, int val)
{
	double a, b;
	double k = 30.0/32.0;

	Bseek(ip->bp, base+val, 0);
	a = rint(ip, 4);
	b = rint(ip, 4);
	switch(ep->tag){
	case 0x9202:		// aperture
	case 0x9204:		// exposure bias
	case 0x9205:		// max aperture
		fmtprint(&ip->mfmt, "F%-5.1f\n", pow(2.0, (a/b)/2.0) + 0.05);
		break;
	case 0x9203:		// brightness
		if((uint)a == ~0u)
			fmtprint(&ip->mfmt, "unknown\n");
		else
			fmtprint(&ip->mfmt, "%+.1f\n", pow(2.0, a/b)*k + 0.05);
		break;
	case 0x9201:		// shutter speed
		fmtprint(&ip->mfmt, "1/%.1f\n", pow(2.0, a/b) + 0.05);
		break;
	}
}

void
tag_version(Img *ip, int, Exif *, int, int, int val)
{
	fmtprint(&ip->mfmt, "V%c%c.%c%c\n",
		val&0xff, (val>>8)&0xff, (val>>16)&0xff, (val>>24)&0xff);
}

void
tag_shutter(Img *ip, int base, Exif *, int, int, int val)
{
	double a, b;

	Bseek(ip->bp, base+val, 0);
	a = rint(ip, 4);
	b = rint(ip, 4);
	fmtprint(&ip->mfmt, "1/%g\n", b/a);
}

void
tag_comment(Img *ip, int base, Exif *, int, int num, int val)
{
	int c;

	fmtprint(&ip->mfmt, "'");
	Bseek(ip->bp, base+val, 0);
	while(num--){
		if((c = Bgetc(ip->bp)) == -1)
			fail("%s unexpected EOF - %r\n", ip->file);
		if(isprint(c))
			fmtprint(&ip->mfmt, "%c", c);
	}
	fmtprint(&ip->mfmt, "'\n");
}

void
tag_other(Img *ip, int base, Exif *ep, int fmt, int num, int val)
{
	char *str;
	double a, b;
	int c;

	switch(fmt){
	case 1:
		if((str = val2name(ep, (uchar)val)) != nil)
			fmtprint(&ip->mfmt, "%s ", str);
		else
			fmtprint(&ip->mfmt, "%ud ", (uchar)val);
		break;
	case 2:
		Bseek(ip->bp, base+val, 0);
		while(num--){
			if((c = Bgetc(ip->bp)) == -1)
				fail("%s unexpected EOF - %r\n", ip->file);
			if(isprint(c))
				fmtprint(&ip->mfmt, "%c", c);
		}
		fmtprint(&ip->mfmt, " ");
		break;
	case 3:			// ushort
		if((str = val2name(ep, (ushort)val)) != nil)
			fmtprint(&ip->mfmt, "%s ", str);
		else
			fmtprint(&ip->mfmt, "%ud ", (ushort)val);
		break;
	case 4:			// uint
		if((str = val2name(ep, (uint)val)) != nil)
			fmtprint(&ip->mfmt, "%s ", str);
		else
			fmtprint(&ip->mfmt, "%ud ", (uint)val);
		break;
	case 5:			// unsigned fraction
		Bseek(ip->bp, base+val, 0);
		a = (unsigned)rint(ip, 4);
		b = (unsigned)rint(ip, 4);
		fmtprint(&ip->mfmt, "%g ", a/b);
		break;
	case 9:			// signed int
		if((str = val2name(ep, val)) != nil)
			fmtprint(&ip->mfmt, "%s ", str);
		else
			fmtprint(&ip->mfmt, "%d ", val);
		break;
	case 10:		// signed fraction
		Bseek(ip->bp, base+val, 0);
		a = rint(ip, 4);
		b = rint(ip, 4);
		fmtprint(&ip->mfmt, "%g ", a/b);
		break;
	case 7:			// unknown
		if((str = val2name(ep, val)) != nil){
			fmtprint(&ip->mfmt, "%s ", str);
			break;
		}
		/* FALLTHRU */	
	default:		// undefined
		fmtprint(&ip->mfmt, "[tag=0x%04x fmt=%d, num=%d val=%d] ", ep->tag, fmt, num, val);
		break;
	}
	fmtprint(&ip->mfmt, "\n");
}

/****************************************************/

void
decode(Img *ip, int base, int tag, int fmt, int num, int val)
{
	Exif *ep;

	for(ep = Table; ep->name; ep++)
		if(ep->tag == tag)
			break;
	if(! ep->useful && !Verbose)
		return;

	if(ep->name == nil)
		fmtprint(&ip->mfmt, "[tag=0x%04x]: ", tag);
	else
		fmtprint(&ip->mfmt, "%s: ", ep->name);

	if(ep->name && ep->func)
		(*ep->func)(ip, base, ep, fmt, num, val);
	else
		tag_other(ip, base, ep, fmt, num, val);
}

void
ifd(Img *ip, uvlong base, int off, int ifdnum)
{
	uvlong was;
	int n, i, tag, fmt, num, val, next;

	Bseek(ip->bp, base+off, 0);
	n = rint(ip, 2);		// slots in IFD
	for(i = 0; i < n; i++){
		tag = rint(ip, 2);	// tag
		fmt = rint(ip, 2);	// data format
		num = rint(ip, 4);	// num items
		val = rint(ip, 4);	// value / pointer

		if(fmt < 0 || fmt > 20)
			fail("%s silly number format, lost in file?\n", ip->file);

		was = Bseek(ip->bp, 0, 1);
		switch(tag){
		case EXIF:	// offset to EXIF idf
			ifd(ip, base, val, ifdnum);
			break;
		case EX_toff:
			ip->toff = base+val;
			break;
		case EX_tlen:
			ip->tlen = val;
			break;
		}
		if(ip->mode == 'm')
			decode(ip, base, tag, fmt, num, val);
		Bseek(ip->bp, was, 0);
	}
	next = rint(ip, 4);
	if(next)
		ifd(ip, base, next, ifdnum+1);
	Bseek(ip->bp, base+off, 0);

}

void
app1(Img *ip, int)
{
	int end, off;
	uvlong base;
	char exif[] ="Exif\0";
	char buf[sizeof(exif)];

	rmem(ip, buf, sizeof(exif));
	if(memcmp(buf, exif, sizeof(exif)) != 0)
		return;

	base = Bseek(ip->bp, 0, 1);
	end = rint(ip, 2);
	switch(end){
	case 0x4949:
		ip->intel = 1;
		break;
	case 0x4d4d:
		ip->intel = 0;
		break;
	default:
		fail("%s 0x%04x bad endian flag\n", ip->file, end);
	}
	rint(ip, 2);		// 42, a magic number it appears
	off = rint(ip, 2);	// offset to IFD
	if(off == 0)		// some files are broken
		off = 8;

	ifd(ip, base, off, 0);
}

void
app0(Img *ip, int len)
{
	char buf[64];
	char jfif[] ="JFIF";
	char jfxx[] ="JFXX";
	int code, xth, yth;

	rmem(ip, buf, sizeof(jfif));
	if(memcmp(buf, jfif, sizeof(jfif)) != 0)
		return;
	len -= sizeof(jfif);

	rint(ip, 1);		// major version
	rint(ip, 1);		// minor version
	rint(ip, 1);		// units, zero -> xden/yden == aspect ratio
	rint(ip, 2);		// xdensity/aspect
	rint(ip, 2);		// ydensity/aspect
	xth = rint(ip, 1);	// x size fo thumbnail
	yth = rint(ip, 1);	// ysize of thumbnail
	len -= 9;
	if(xth == 0 || yth == 0)
		return;

	rmem(ip, buf, sizeof(jfxx));
	if(memcmp(buf, jfxx, sizeof(jfxx)) != 0)
		return;
	len -= sizeof(jfxx);

	code = rint(ip, 1);
	len -= 1;
	if(code != JFXX_jpeg)
		return;
	ip->toff = Bseek(ip->bp, 1, 0);
	ip->tlen = len;
}

void
sof(Img *ip, int len)
{
	USED(len);

	rint(ip, 1);
	rint(ip, 2);
	rint(ip, 2);
	rint(ip, 1);
}

int
parse(Img *ip)
{
	int tag, len, n;

	if(setjmp(Failed) != 0)
		return -1;

	while(1){
		do{
			if((tag = Bgetc(ip->bp)) == -1)
				fail("%s unexpected EOF - %r", ip->file);
			if(tag != 0xff)
				continue;
			do{
				if((tag = Bgetc(ip->bp)) == -1)
					fail("%s unexpected EOF - %r", ip->file);

			}while(tag == 0xff);
		}while(tag == 0);

		switch(tag){
		case SOI:
		case EOI:
		case TEM:
		case RST+0: case RST+1: case RST+2: case RST+3:
		case RST+4: case RST+5: case RST+6: case RST+7:
			continue;	// stand alone tags
		default:
			break;		// tags with length and data
		}
	
		if((n = Bgetc(ip->bp)) == -1)
			fail("%s unexpected EOF - %r", ip->file);
		len = (n << 8) & 0xff00;
		if((n = Bgetc(ip->bp)) == -1)
			fail("%s unexpected EOF - %r", ip->file);
		len |= n & 0xff;
		len -= 2;		// length includes the length field

		switch(tag){
		case APP+0:		// jpeg - image spec
			app0(ip, len);
			break;
		case APP+1:		// exif - image spec, thumbnamil spec & data
			app1(ip, len);
			break;
		case SOF:		// jpeg - start of frame
			sof(ip, len);
			break;
		case SOS:		// jpeg - compressed data followed by EOI and EOF
			return 0;
		default:
			break;
		}
	}
}

int
jpgopen(char *phys, int)
{
	int rc, fd;
	char *virt;

	if((virt = strrchr(phys, '/')) == nil){
		werrstr("jpg: %s - path too short, no virtual file name", phys);
		return -1;
	}
	*virt++ = 0;

	for(fd = 0; fd < nelem(Imgs); fd++)
		if(Imgs[fd].bp == nil)
			break;
	if(Imgs[fd].bp){
		werrstr("jpg: no free img descriptors");
		return -1;
	}

	if((Imgs[fd].bp = Bopen(phys, OREAD)) == nil)
		return -1;
	if((Imgs[fd].file = strdup(phys)) == nil)
		return -1;

	if(strcmp(virt, "metadata") == 0){
		fmtstrinit(&Imgs[fd].mfmt);
		Imgs[fd].mode = 'm';
		rc = parse(&Imgs[fd]);
		Imgs[fd].mdata = fmtstrflush(&Imgs[fd].mfmt);
		Imgs[fd].mlen = strlen(Imgs[fd].mdata);
	}
	else
	if(strcmp(virt, "fullsize.jpg") == 0){
		Imgs[fd].mode = 'f';
		Imgs[fd].mdata = nil;
		rc = parse(&Imgs[fd]);
	}
	else
	if(strcmp(virt, "thumbnail.jpg") == 0){
		Imgs[fd].mode = 't';
		Imgs[fd].mdata = nil;
		Imgs[fd].tlen = 0;
		rc = parse(&Imgs[fd]);
		if(Imgs[fd].tlen == 0)		// no thumbnail available (should resize on the fly).
			Imgs[fd].mode = 'f';
	}
	else{
		werrstr("%s - not found\n", virt);
		return -1;
	}
	if(rc == -1){
		Bterm(Imgs[fd].bp);
		Imgs[fd].bp = nil;
		free(Imgs[fd].file);
		free(Imgs[fd].mdata);
		return -1;
	}
	return fd;
}

long
jpgpread(int fd, void *buf, long len, vlong off)
{
	if(Imgs[fd].mode == 't'){
		if(off < 0LL || off >= Imgs[fd].tlen)
			return 0;
		if(Bseek(Imgs[fd].bp, Imgs[fd].toff+off, 0) != Imgs[fd].toff+off)
			return -1;
		if((len+off) > Imgs[fd].tlen)
			len = Imgs[fd].tlen - off;
		return Bread(Imgs[fd].bp, buf, len);
	}
	if(Imgs[fd].mode == 'f'){
		if(Bseek(Imgs[fd].bp, off, 0) != off)
			return -1;
		return Bread(Imgs[fd].bp, buf, len);
	}
	if(Imgs[fd].mode == 'm'){
		if(off > Imgs[fd].mlen)
			return 0;
		if(len + off > Imgs[fd].mlen)
			len = Imgs[fd].mlen - off;
		memmove(buf, Imgs[fd].mdata + off, len);
		return len;
	}
	return -1;
}

int
jpgclose(int fd)
{

	Bterm(Imgs[fd].bp);
	Imgs[fd].bp = nil;
	free(Imgs[fd].file);
	free(Imgs[fd].mdata);
	return 0;
}

