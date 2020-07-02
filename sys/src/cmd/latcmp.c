#include <u.h>
#include <libc.h>
#include <bio.h>

typedef struct Lathe Lathe;
struct Lathe {
	double	lat;
	double	lng;
};
#pragma	varargck	type	"φ"	Lathe

double	a = 6378137.;
double	b = 6356752.3142;		/* b ≡ a(1-f) */
double	f = 1 / 298.257223563;
double	π = 3.14159265358979323846;
double	R = 6371 * 10;
Lathe	Z;

#define	Big	(1e30)
char	*line;
Biobuf	o;
double	min = Big;
double	(*lathe2m)(Lathe, Lathe);
char	flag[127];

int
φfmt(Fmt *f)
{
	Lathe φ;

	φ = va_arg(f->args, Lathe);
	if(f->flags & FmtSharp)
		fmtprint(f, "lat %g lng %g", φ.lat, φ.lng);
	else
		fmtprint(f, "%g %g", φ.lat, φ.lng);
	return 0;
}

double
dtorad(double d)
{
	return d*π/180.;
}

double
haversine(Lathe φ1, Lathe φ2)
{
	double lat1, lat2, x, y;
	Lathe Δ;

	Δ.lat = dtorad(φ2.lat - φ1.lat);
	Δ.lng = dtorad(φ2.lng - φ1.lng);
	lat1 = dtorad(φ1.lat);
	lat2 = dtorad(φ2.lat);

	x = sin(Δ.lat/2) * sin(Δ.lat/2) +
		sin(Δ.lng/2) * sin(Δ.lng/2) +
		cos(lat1) * cos(lat2);
	y = 2*atan2(sqrt(x), sqrt(1-x));
	return R*y;	/* meters */
}

/* Thaddeus Vincenty's formula; output in m */
double
vincenty(Lathe φ1, Lathe φ2)
{
	double L, U1, U2, λ, λ′;
	double sinσ, cosσ, σ, sinα, cos2α, cos2σm, C, u2, A, B, Δσ;
	double x, y, δ, δ′;

	L = dtorad(φ2.lng - φ1.lng);
	U1 = atan((1 - f) * tan(dtorad(φ1.lat)));	/* reduced latitude */
	U2 = atan((1 - f) * tan(dtorad(φ2.lat)));

	δ = 1e30;
	for(λ = L;; λ = λ′){			/* λ = lat difference on aux. sphere */
		x = cos(U2)*sin(λ);
		y = cos(U1)*sin(U2)-sin(U1)*cos(U2)*cos(λ);
		sinσ = sqrt(x*x + y*y);
		if(sinσ == 0)
			return 0;			/* same pt within prec */
		cosσ = sin(U1)*sin(U2) + cos(U1)*cos(U2)*cos(λ);
		σ = atan2(sinσ, cosσ);
		sinα = cos(U1) * cos(U2) * sin(λ) / sinσ;
		cos2α = 1 - sinα*sinα;
		if(cos2α != 0)
			cos2σm = cosσ - 2*sin(U1)*sin(U2)/cos2α;
		else
			cos2σm = 0.;		/* equatorial line */
		C = f/16*cos2α*(4+f*(4-3*cos2α));
		λ′ = L + (1-C) * f * sinα *
			(σ + C*sinσ*(cos2σm+C*cosσ*(-1+2*cos2σm*cos2σm)));
		δ′ = fabs(λ - λ′);
		if(δ′ < 1e-12)
			break;
		if(fabs(δ′) >= fabs(δ))
			return haversine(φ1, φ2);	/* fallback: formula diverged */
		δ = δ′;
	}

	u2 = cos2α * (a*a - b*b) / (b*b);
	A = 1 + u2/16384*(4096 + u2*(-768 + u2*(320 - 175*u2)));
	B = u2/1024 * (256 + u2*(-128 +u2*(74-47*u2)));
	x = cos2σm*cos2σm;
	Δσ = B*sinσ*(cos2σm + B/4*(cosσ*(-1 + 2*x)
		- B/6*cos2σm*(-3 + 4*sinσ*sinσ)*(-3 + 4*x)));

	return b*A*(σ-Δσ);	/* meters */	
}

Lathe
strtolathe(char *s, char **r)
{
	char *p0, *x;
	Lathe φ;

	if(r == nil)
		r = &x;
	s += strcspn(s, "0123456789-.");
	φ.lat = strtod(s, r);
	if(**r == 0 || *r == s || fabs(φ.lat) > 90.){
		werrstr("bad latitude");
		return Z;
	}
	p0 = *r;
	φ.lng = strtod(p0, r);
	if(*r == p0 || fabs(φ.lng) > 180.){
		werrstr("bad longitude");
		return Z;
	}
	return φ;
}

int
isZ(Lathe φ)
{
	return memcmp(&φ, &Z, sizeof Z) == 0;
}

Lathe
skyhere(void)
{
	char buf[128];
	int fd, n;
	Lathe φ;

	fd = open("/lib/sky/here", OREAD);
	if(fd == -1)
		return Z;
	n = read(fd, buf, sizeof buf);
	close(fd);
	if(n == -1)
		return Z;
	buf[n] = 0;
	φ = strtolathe(buf, nil);
	φ.lng = -φ.lng;
	return φ;
}

void
result(char *key, double d)
{
	if(!flag['m']){
		Bprint(&o, "%s	%g\n", key, d);
		return;
	}
	if(fabs(d) >= min)
		return;
	free(line);
	line = nil;
	if(key)
		line = strdup(key);
	min = d;
}

void
prmin(void)
{
	if(line != nil && !flag['q'])
		Bprint(&o, "%s	", line);
	Bprint(&o, "%g\n", min);
}

void
fildist(char *file, Lathe φ1)
{
	char *s;
	Biobuf *b;
	Lathe φ2;

	b = Bopen(file, OREAD);
	if(b == nil)
		sysfatal("Bopen: %r");
	for(; s = Brdstr(b, '\n', 1); free(s)){
		φ2 = strtolathe(s, nil);
		if(isZ(φ2))
			continue;
		result(s, lathe2m(φ1, φ2));
	}
	Bterm(b);
}

void
usage(void)
{
	fprint(2, "usage lat [-qm] [-l lat lng] [ptfile ...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char buf[128];
	int i, ells;
	Lathe φ[2];

	lathe2m = vincenty;
	fmtinstall(L'φ', φfmt);
	ells = 0;

	if(Binit(&o, 1, OWRITE) == -1)
		sysfatal("Binit: %r");

	ARGBEGIN{
	case 'q':
	case 'm':
	case 'k':
		flag[ARGC()] = 1;
		break;
	case 'l':
		φ[ells].lat = strtod(EARGF(usage()), nil);
		φ[ells].lng = strtod(EARGF(usage()), nil);
		if(fabs(φ[ells].lat) > 90. || fabs(φ[ells].lng) > 180.)
			usage();
		if(++ells == 2){
			snprint(buf, sizeof buf, "%φ", φ[1]);
			result(buf, lathe2m(φ[0], φ[1]));
			ells = 1;
		}
		break;
	default:
		usage();
	}ARGEND

	if(isZ(φ[0]))
		φ[0] = skyhere();
	if(isZ(φ[0]))
		sysfatal("bad origin: %r");

	if(flag['k']){
		/* kat — known answer test */
		φ[0] = (Lathe){33.8765, -83.3370};
		φ[1] = (Lathe){33.3587, -84.5617};
		Bprint(&o, "h	%g\n", haversine(φ[0], φ[1]));
		Bprint(&o, "v	%g\n", vincenty(φ[0], φ[1]));
	}else
		for(i = 0; i < argc; i++)
			fildist(argv[i], φ[0]);

	if(flag['m'])
		prmin();

	Bterm(&o);
	exits("");
}
