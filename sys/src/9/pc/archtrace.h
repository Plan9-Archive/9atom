enum {
	Maxprobe	= 32,
};

typedef struct Archtrace Archtrace;
struct Archtrace {
	uchar	nprobe;
	uchar	*text[Maxprobe];
	uchar	probe[Maxprobe];
	uchar	orig[Maxprobe];
};

void	_tracein(void);
void	_traceout(void);
char	*archtracectlr(Archtrace*, char*, char*);
void	archtraceinstall(Archtrace*);
void	archtraceuninstall(Archtrace*);
int	mkarchtrace(Archtrace*, uchar*, uchar**);
