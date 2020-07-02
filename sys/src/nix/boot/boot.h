typedef struct Method	Method;
struct Method
{
	char	*name;
	void	(*config)(Method*);
	int	(*connect)(void);
	char	*arg;
};
enum
{
	Statsz=	256,
	Nbarg=	16,
};

extern void	authentication(int);
extern char*	bootdisk;
extern char*	rootdir;
extern int	(*cfs)(int);
extern int	cpuflag;
extern char	cputype[];
extern int	fflag;
extern int	kflag;
extern Method	method[];
extern char	sys[];
extern uchar	statbuf[Statsz];
extern int	bargc;
extern char	*bargv[Nbarg];
extern void	partitions(void);

/* libc equivalent */
extern int	cache(int);
extern void	fatal(char*);
extern void	getpasswd(char*, int);
extern void	key(int, Method*);
extern int	outin(char*, char*, int);
extern int	plumb(char*, char*, int*, char*);
extern int	readfile(char*, char*, int);
extern void	run(char *file, ...);
extern int	sendmsg(int, char*);
extern void	setenv(char*, char*);
extern void	settime(int, int, char*);
extern void	srvcreate(char*, int);
extern void	warning(char*);
extern int	writefile(char*, char*, int);
extern void	boot(int, char **);
extern int	parsefields(char*, char**, int, char*);

/* methods */
extern void	configil(Method*);
extern int	connectil(void);

extern void	configtcp(Method*);
extern int	connecttcp(void);

extern void	configlocal(Method*);
extern int	connectlocal(void);

extern void	configembed(Method*);
extern int	connectembed(void);

extern void	configip(int, char**, int);

/* hack for passing authentication address */
extern char	*authaddr;
