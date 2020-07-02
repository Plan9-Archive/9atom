#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>
#include <memlayer.h>

enum {
	Nbuf	= 32,
};

typedef struct C C;

struct C {
	char		*path;
	int		fd;
	Memimage	*im;
};

void		intimg(C*, char*, int);
Memimage	*pageimg(C*, ulong);
void		closeimg(C*);
void		composer(C*, int, ulong, ulong);
ulong		strtoop(char*);

char		flag[0x80];
