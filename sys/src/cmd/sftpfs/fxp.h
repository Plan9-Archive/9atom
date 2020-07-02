/* Copyright © 2008 Fazlul Shahriar*/

#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include <bio.h>

/*
 * util.c
 */
extern	char*	estrstrdup(char*, char*);
extern	char*	estr3dup(char*, char*, char*);
extern	char*	egrow(char*, char*, char*);
extern	char*	eappend(char*, char*, char*);
extern	void		hexdump(char*, uchar*, int);


/*
 * map.c
 */
typedef struct Map Map;
typedef struct User User;
typedef struct Group Group;

struct User{
	char	*name;
	uint	uid;
	uint	gid;
	uint	g[16];
	uint	ng;
	uchar	*auth;
	int	nauth;
};

struct Group{
	char	*name;	/* same pos as in User struct */
	uint	gid;	/* same pos as in User struct */
};

struct Map{
	int	nuser;
	int	ngroup;
	User	*user;
	User	**ubyname;
	User	**ubyid;
	Group	*group;
	Group	**gbyname;
	Group	**gbyid;
};

extern	Map*	readmap(char*, char*);
extern	void	closemap(Map*);
extern	char*	uidtostr(Map*, u32int);
extern	char*	gidtostr(Map*, u32int);

/*
 * fxp.c
 */
typedef struct String String;
typedef struct String FHandle;

struct String{
	u32int	len;
	uchar	*s;
};

extern	int	fxpinit(char*,char,char*);
extern	void	fxpterm(void);
extern	FHandle*	fxpopendir(char*);
extern	int	fxpclose(FHandle*);
extern	long	fxpreaddir(FHandle*, Dir***);
extern	FHandle*	fxpopen(char*, int);
extern	FHandle*	fxpcreate(char*, int, ulong);
extern	int	fxpread(FHandle*, void*, long, vlong);
extern	long	fxpwrite(FHandle*, void*, long, vlong);
extern	int	fxpmkdir(char*, ulong);
extern	int	fxprmdir(char*);
extern	int	fxpremove(char*);
extern	Dir*	fxpstat1(char*, char**);
extern	Dir*	fxpstat(char*);
extern	int	fxpsetstat(char*, Dir*);
extern	FHandle*	handledup(FHandle*);
extern	int	fxprename(char*, char*);
extern	void	freedir(Dir*);
extern	void	fxpreadmap(char*, char*);

/* sftp defined errors */
static char Enofile[] = "no such file";
static char Eperm[] = "permission denied";
static char Efail[] = "failed";
static char Emsg[] = "bad message";
static char Enocn[] = "no connection";	/* fake */
static char Elostcn[] = "connection lost";	/* fake */
static char Eunsup[] = "operation unsupported";

/* our errors */
static char Ebotch[] = "sftp protocol botch";
static char Ehand[] = "bad handle";
static char Epath[] = "bad fid path";
static char Eopen[] = "handle already open";
static char Einter[] = "internal error";
static char Ebuf[] = "short buffer";
