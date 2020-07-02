#include <u.h>
#include <libc.h>
#include <ip.h>
#include "dat.h"
#include "protos.h"

typedef struct{
	uchar	spi[4];
	uchar	seq[4];
}Hdr;

enum{
	Hsize	= 8,
};

enum{
	Ospi,
	Oseq,
};

static Field p_fields[] =
{
	{"spi",	Fnum,	Ospi,		"spi",	},
	{"seq",	Fnum,	Oseq,		"seq",	},
	{0}
};

static void
p_compile(Filter *f)
{
	if(f->op == '='){
		compile_cmp(esp.name, f, p_fields);
		return;
	}
	sysfatal("unknown esp field: %s", f->s);
}

static int
p_filter(Filter *f, Msg *m)
{
	Hdr *h;

	if(m->pe - m->ps < Hsize)
		return 0;

	h = (Hdr*)m->ps;
	m->ps += Hsize;

	switch(f->subop){
	case Ospi:
		return NetL(h->spi) == f->ulv;
	case Oseq:
		return NetL(h->seq) == f->ulv;
	}
	return 0;
}

static int
p_seprint(Msg *m)
{
	Hdr *h;

	if(m->pe - m->ps < Hsize)
		return 0;

	h = (Hdr*)m->ps;
	m->ps += Hsize;

	/* no next protocol */
	m->pr = nil;

	m->p = seprint(m->p, m->e, "spi=%.8ux seq=%.8ux",
		NetL(h->spi), NetL(h->seq));
	return 0;
}

Proto esp =
{
	"esp",
	p_compile,
	p_filter,
	p_seprint,
	nil,
	nil,
	p_fields,
	defaultframer,
};
