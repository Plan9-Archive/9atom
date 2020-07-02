enum {
	Tscsi	= 1,
	Tata	= 2,

	Xseg	= 3,	/* segmented download */
	Xfull	= 7,	/* full download */

	Nrb	= 32,
	Pathlen	= 256,
};

typedef struct Dtype Dtype;
typedef struct Dlparm Dlparm;
typedef struct Sdisk Sdisk;

struct Dtype {
	int	type;
	char	*tname;
	int	(*probe)(Sdisk*);
	int	(*dlmc)(Sdisk*, char*, uint);
};

struct Dlparm {
	int	dlmode;
	int	maxxfr;
};

struct Sdisk {
	Sdisk	*next;
	Dtype	*t;
	Dlparm;
	int	fd;
	Sfis;
	char	path[Pathlen];
	char	name[28];
};

int	scsiprobe(Sdisk*);
int	scsidl(Sdisk*, char*, uint);
int	ataprobe(Sdisk*);
int	atadl(Sdisk*, char*, uint);

void	eprint(Sdisk*, char *, ...);
