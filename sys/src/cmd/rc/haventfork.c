#include "rc.h"
#include "getflags.h"
#include "exec.h"
#include "io.h"
#include "fns.h"

int havefork = 0;

static char **
rcargv(char *s)
{
	int argc;
	char **argv;
	word *p;

	p = vlook("*")->val;
	argv = malloc((count(p)+6)*sizeof(char*));
	argc = 0;
	argv[argc++] = argv0;
	if(flag['e'])
		argv[argc++] = "-Se";
	else
		argv[argc++] = "-S";
	argv[argc++] = "-c";
	argv[argc++] = s;
	for(p = vlook("*")->val; p; p = p->next)
		argv[argc++] = p->word;
	argv[argc] = 0;
	return argv;
}

void
Xasync(void)
{
	uint pid;
	char buf[20], **argv;

	Updenv();

	argv = rcargv(runq->code[runq->pc].s);
	pid = ForkExecute(argv0, argv, -1, 1, 2);
	free(argv);

	if(pid == 0) {
		Xerror("proc failed");
		return;
	}

	runq->pc++;
	sprint(buf, "%d", pid);
	setvar("apid", newword(buf, (word *)0));
}

enum { Stralloc = 100, };

void
Xbackq(void)
{
	char **argv;
	int l, n, pw, inc, pid;
	int pfd[2];
	char *s, *wd, *ewd, *stop;
	struct io *f;
	var *ifs = vlook("ifs");
	word *v, *nextv;
	Rune r;

	stop = "";
	pw = 0;
	if(runq->argv && runq->argv->words){
		stop = runq->argv->words->word;
		pw = 1;
	}
	if(pipe(pfd)<0){
		Xerror("can't make pipe");
		return;
	}

	Updenv();

	argv = rcargv(runq->code[runq->pc].s);
	pid = ForkExecute(argv0, argv, -1, pfd[1], 2);
	free(argv);

	close(pfd[1]);

	if(pid == 0) {
		Xerror("proc failed");
		close(pfd[0]);
		return;
	}

	f = openfd(pfd[0]);
	s = wd = ewd = 0;
	v = 0;
	inc = Stralloc;
	for(;;){
		/* always eat 4 byte runes */
		if(s+2*UTFmax>=ewd){
			l = s-wd;
			wd = erealloc(wd, l+inc);
			ewd = wd+l+inc-1;
			s = wd+l;
			inc *= 2;
		}
		if((n = rutf(s, &r, f))==EOF)
			break;
		s[n] = 0;
		if(strstr(stop, s)){
			if(s!=wd){
				*s='\0';
				v = newword(wd, v);
				s = wd;
			}
		}else
			s += n;
	}
	if(s!=wd){
		*s='\0';
		v = newword(wd, v);
	}
	if(wd)
		efree(wd);
	closeio(f);
	Waitfor(pid, 1);
	if(pw)
		popword();	/* ditch split in "stop" */
	/* v points to reversed arglist -- reverse it onto argv */
	while(v){
		nextv=v->next;
		v->next=runq->argv->words;
		runq->argv->words=v;
		v=nextv;
	}
	runq->pc++;
}

void
Xpipe(void)
{
	thread *p=runq;
	int pc=p->pc, pid;
	int rfd=p->code[pc+1].i;
	int pfd[2];
	char **argv;

	if(pipe(pfd)<0){
		Xerror1("can't get pipe");
		return;
	}

	Updenv();

	argv = rcargv(runq->code[pc+2].s);
	pid = ForkExecute(argv0, argv, 0, pfd[1], 2);
	free(argv);
	close(pfd[1]);

	if(pid == 0) {
		Xerror("proc failed");
		close(pfd[0]);
		return;
	}

	start(p->code, pc+4, runq->local);
	pushredir(ROPEN, pfd[0], rfd);
	p->pc=p->code[pc+3].i;
	p->pid=pid;
}

void
Xpipefd(void)
{
	Abort();
}

void
Xsubshell(void)
{
	char **argv;
	int pid;

	Updenv();

	argv = rcargv(runq->code[runq->pc].s);
	pid = ForkExecute(argv0, argv, -1, 1, 2);
	free(argv);

	if(pid < 0) {
		Xerror("proc failed");
		return;
	}

	Waitfor(pid, 1);
	runq->pc++;
}

/*
 *  start a process running the cmd on the stack and return its pid.
 */
int
execforkexec(void)
{
	char **argv;
	char file[1024];
	int nc;
	word *path;
	int pid;

	if(runq->argv->words==0)
		return -1;
	argv = mkargv(runq->argv->words);

	for(path = searchpath(runq->argv->words->word);path;path = path->next){
		nc = strlen(path->word);
		if(nc < sizeof file - 1){	/* 1 for / */
			strcpy(file, path->word);
			if(file[0]){
				strcat(file, "/");
				nc++;
			}
			if(nc+strlen(argv[1])<sizeof(file)){
				strcat(file, argv[1]);
				pid = ForkExecute(file, argv+1, mapfd(0), mapfd(1), mapfd(2));
				if(pid >= 0){
					free(argv);
					return pid;
				}
			}
		}
	}
	free(argv);
	return -1;
}