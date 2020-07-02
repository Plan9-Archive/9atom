#include <u.h>
#include <libc.h>
#include <libsec.h>

enum {
	Nanoi	= 1000000000,	/* 1/nano */
	Microi	= 1000000,
};

static	trace[0x80];

void
prset(char *label, uvlong t, uvlong v)
{
	uvlong x;

	if(v > 10000){
		x = v*Nanoi;
		x /= t;
		x *= 100;
	}else{
		x = v*Nanoi*100;
		x /= t;
	}
	print("%lld.%02lld%s", x/100, (x%100), label);
}

void
prrate(char *label, uvlong t, uvlong *tab)
{
	char buf[64];

	prset("r ", t, tab[0]);
	prset("w ", t, tab[1]);
	snprint(buf, sizeof buf, "r+w %s\n", label);
	prset(buf, t, tab[0]+tab[1]);
}

void
ioloop(int fd, uvlong nbytes, char *prog)
{
	char *buf, *p, *loop;
	int i, l, nest, lnest;
	uvlong ss, bytes[2], iops[2], byte0, maxlba, lba, t;

	/* silence compiler */
	for(i = 0; i < 2; i++){
		iops[i] = 0;
		bytes[i] = 0;
	}
	l = 0;
	t = 0;
	nest = 0;
	lnest = 0;
	loop = nil;
	byte0 = 0;
	lba = 0;
	ss = 512;
	maxlba = (nbytes - byte0) / ss;
	buf = malloc(ss);
	if(buf == nil)
		sysfatal("malloc");
	srand(nsec());
	for(p = prog;; ){
		switch(*p){
		default:
			sysfatal("bad char %c in prog %s\n", *p, prog);
		case 0:
			goto end;
		case ' ': case '\t': case '\n':
			break;
		case ':':							/* loop */
			if(lnest++ > 0)
				sysfatal("nexted :;");
			l = strtol(p+1, &loop, 0);
			p = loop;
			continue;
		case ';':							/* end */
			lnest--;
			if(loop == nil)
				sysfatal("malformd loop: extra ';'");
			if(--l > 0){
				p = loop;
				continue;
			}
			loop = nil;
			break;
		case '{':
			for(i = 0; i < 2; i++){
				iops[i] = 0;
				bytes[i] = 0;
			}
			if(nest++ > 0)
				sysfatal("nexted {}");
			t = - nsec();
			break;
		case '}':
			if(nest-- == 0)
				sysfatal("unbalanced }");
			t += nsec();
			print("%lld.%03lld\n", t/Nanoi, (t%Nanoi)/Microi);
			prrate("bytes/s", t, bytes);
			prrate("iops", t, iops);
			break;
		case 'o':
			byte0 = strtoull(p+1, &p, 0);
			maxlba = (nbytes - byte0) / ss;
			continue;
		case 'z':
			ss = strtoul(p+1, &p, 0);
			if(ss == 0)
				sysfatal("sector size zero");
			maxlba = (nbytes - byte0) / ss;
			buf = realloc(buf, ss);
			if(buf == nil)
				sysfatal("realloc: %r");
			genrandom((uchar*)buf, ss);
			continue;
		case 'r':							/* read */
			pread(fd, buf, ss, byte0 + lba*ss);
			if(trace['r'])
				print("read lba %lld\n", lba);
			iops[0]++;
			bytes[0] += ss;
			break;
		case 'w':							/* write */
			pwrite(fd, buf, ss, byte0 + lba*ss);
			if(trace['w'])
				print("read lba %lld\n", lba);
uvlong *v = (uvlong*)buf;
for(i = 0; i < ss/8; i+=1024)v[i]=lba ^ i;
			iops[1]++;
			bytes[1] += ss;
			break;
		case 's':							/* seek */
			p++;
			switch(*p){
			default:
				sysfatal("seek requires argument\n");
			case 'r':
				lba = frand()*maxlba;	/* awful.  no vlnrand() */
				p++;
				break;
			case 's':
				p++;
			case '-':
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				lba += strtoll(p, &p, 0);
				break;
			case '=':
				lba = strtoll(p+1, &p, 0);
				break;
			}
			if(lba >= maxlba)
				sysfatal("seek past end %lld\n", lba);
			if(trace['s'])
				print("seek lba %lld\n", lba);
			continue;
		}
		p++;
	}
end:
	free(buf);
}

void
usage(void)
{
	fprint(2, "usage: iop [-p prog] ... [file ...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *p, *prog[25];
	int i, j, nprog, fd;
	uvlong nbytes;
	Dir *d;

	nprog = 0;

	ARGBEGIN{
	case 'v':
		p = EARGF(usage());
		for(; *p; p++)
			trace[*p & 0x7f] = 1;
		break;
	case 'p':
		if(nprog == nelem(prog))
			sysfatal("too many programs");
		prog[nprog++] = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	for(i = 0; i < argc; i++){
		d = dirstat(argv[i]);
		if(d == nil)
			sysfatal("dirstat: %r");
		nbytes = d->length;
		free(d);

		fd = open(argv[i], ORDWR);
		if(fd == -1)
			sysfatal("open: %r");
		for(j = 0; j < nprog; j++)
			ioloop(fd, nbytes, prog[i]);
		close(fd);
	}
	exits("");
}
