#include <u.h>
#include <libc.h>
#include <ip.h>
#include <auth.h>

void newchannel(int, char *, int);
char *get_string(char *, char *);
char *confine(char *, char *);
void runcmd(int, int, char *, char *, char *, char *);

int errfd, slfd, toppid, sflag, tflag, prevent;
char *shell;
char *restdir;
char *srvpt;
char *nsfile = nil;
char *uname;

void
usage(void)
{
	fprint(2, "usage: sshsession [-s shell] [-r restdir] [-R restdir] [-S srvpt] [-n namespace] [-t]\n");
	exits("usage");
}

main(int argc, char *argv[])
{
	char *netdir, *filnam, *p, *q;
	int ctlfd, fd, n;
	char buf[128];

	rfork(RFNOTEG);
	toppid = getpid();
	errfd = create("/tmp/ssh.err", OWRITE, 0664);
	slfd = open("/dev/syslog", OWRITE);
	shell = "/bin/rc -il";
	ARGBEGIN {
	case 'n':
		nsfile = EARGF(usage());
		break;
	case 'R':
		prevent = 1;
	case 'r':
		restdir = EARGF(usage());
		break;
	case 's':
		sflag = 1;
		shell = EARGF(usage());
		break;
	case 't':
		tflag = 1;
		break;
	case 'S':
		srvpt = EARGF(usage());
		break;
	default:
		usage();
		break;
	} ARGEND;

	uname = getenv("user");
	if (uname == nil)
		uname = "none";
	netdir = getenv("net");
	fprint(errfd, "net is %s\n", netdir);
	filnam = smprint("%s/clone", netdir);
	ctlfd = open(filnam, ORDWR);
	if (ctlfd < 0) {
		fprint(errfd, "could not clone: %s: %r\n", filnam);
		exits(nil);
	}
	free(filnam);
	filnam = smprint("%s/data", netdir);
	fd = open(filnam, OREAD);
	if (fd < 0) {
		fprint(errfd, "Couldn't open data: %r\n");
		fprint(ctlfd, "hangup");
		exits(nil);
	}
	n = read(fd, buf, 128);
	close(fd);
	free(filnam);
	if (n < 0) {
		fprint(errfd, "Read error for cap: %r\n");
		fprint(ctlfd, "hangup");
		exits(nil);
	}
	else if (n > 0) {
		buf[n] = '\0';
		if (strcmp(buf, "n/a") != 0) {
			fd = open("#¤/capuse", OWRITE);
			if (fd < 0) {
				fprint(errfd, "Couldn't open capuse: %r\n");
				fprint(ctlfd, "hangup");
				exits(nil);
			}
			if (write(fd, buf, n) < 0) {
				fprint(errfd, "Write to capuse failed: %r\n");
				fprint(ctlfd, "hangup");
				exits(nil);
			}
			close(fd);
			p = strchr(buf, '@');
			if (p) {
				++p;
				q = strchr(p, '@');
				if (q) {
					*q = '\0';
					uname = strdup(p);
				}
				if (!tflag) {
					if (newns(p, nsfile) < 0)
						fprint(errfd, "newns failed: %r\n");
				}
			}
		}
	}
	n = read(ctlfd, buf, 128);
	buf[n] = '\0';
	fprint(ctlfd, "announce session");
	filnam = smprint("%s/%s/listen", netdir, buf);
	fprint(errfd, "listen is %s\n", filnam);
	if (access(netdir, AEXIST) < 0) {
		p = smprint("/srv/%s", srvpt ? srvpt : "sshtun");
		fd = open(p, ORDWR);
		if (fd < 0) {
			fprint(errfd, "srv open failed; %r\n");
			fprint(ctlfd, "hangup");
			exits(nil);
		}
		mount(fd, -1, "/net", MBEFORE, "");
	}
	while (1) {
		fd = open(filnam, ORDWR);
		if (fd < 0) {
			fprint(errfd, "listen failed: %r\n");
			fprint(ctlfd, "hangup");
			exits(nil);
		}
		n = read(fd, buf, 128);
		fprint(errfd, "read from listen file returned %d\n", n);
		if (n <= 0) {
			fprint(errfd, "read on listen failed: %r\n");
			fprint(ctlfd, "hangup");
			exits(nil);
		}
		buf[n] = '\0';
		fprint(errfd, "read %s\n", buf);
		switch (fork()) {
		case 0:
			close(ctlfd);
			newchannel(fd, netdir, atoi(buf));
			break;
		case -1:
			fprint(errfd, "fork failed: %r\n");
			fprint(ctlfd, "hangup");
			exits(nil);
			break;
		default:
			close(fd);
			break;
		}
	}
}

void
newchannel(int fd, char *conndir, int channum)
{
	char *p, *q, *reqfile, *datafile;
	int n, reqfd, datafd, motdfd, want_reply, already_done;
	char buf[32768], buf2[10240], cmd[1024];

	close(fd);
	already_done = 0;
	reqfile = smprint("%s/%d/request", conndir, channum);
	reqfd = open(reqfile, ORDWR);
	if (reqfd < 0) {
		fprint(errfd, "Couldn't open request file: %r\n");
		exits(nil);
	}
	datafile = smprint("%s/%d/data", conndir, channum);
	datafd = open(datafile, ORDWR);
	if (datafd < 0) {
		fprint(errfd, "Couldn't open data file: %r\n");
		exits(nil);
	}
	while (1) {
		n = read(reqfd, buf, 32768);
		fprint(errfd, "read from request file returned %d\n", n);
		if (n == 0) {
			exits(nil);
		}
		else if (n < 0) {
			fprint(errfd, "Read failed: %r\n");
			exits(nil);
		}
		for (p = buf; p < buf + n && *p != ' '; ++p) ;
		*p = '\0';
		++p;
		want_reply = (*p == 't');
		if (strcmp(buf, "pty-req") == 0) {
			if (want_reply)
				fprint(reqfd, "success");
		}
		else if (strcmp(buf, "x11-req") == 0) {
			if (want_reply)
				fprint(reqfd, "failure");
		}
		else if (strcmp(buf, "env") == 0) {
			if (want_reply)
				fprint(reqfd, "failure");
		}
		else if (strcmp(buf, "shell") == 0) {
			if (already_done) {
				if (want_reply)
					fprint(reqfd, "failure");
				continue;
			}
			switch (fork()) {
			case 0:
				if (sflag)
					snprint(cmd, 1024, "-s%s", shell);
				else
					snprint(cmd, 1024, "");
				if (slfd > 0)
					fprint(slfd, "starting ssh shell for %s\n", uname);
				else
					syslog(1, "ssh", "starting ssh shell for %s", uname);
				motdfd = open("/sys/lib/motd", OREAD);
				if (motdfd >= 0) {
					while ((n = read(motdfd, buf, 8192)) > 0) {
						p = buf2;
						for (q = buf; q < buf+n; ++q) {
							if (*q == '\n')
								*p++ = '\r';
							*p++ = *q;
						}
						write(datafd, buf2, p-buf2);
					}
					close(motdfd);
				}
//				runcmd(reqfd, datafd, "con", "/bin/conssim", cmd, nil);
				runcmd(reqfd, datafd, "con", "/bin/ip/telnetd", "-nt", nil);
				exits(nil);
			case -1:
				if (want_reply)
					fprint(reqfd, "failure");
				fprint(2, "Cannot fork: %r\n");
				exits(nil);
				break;
			default:
				already_done = 1;
				if (want_reply)
					fprint(reqfd, "success");
				break;
			}
		}
		else if (strcmp(buf, "exec") == 0) {
			if (already_done) {
				if (want_reply)
					fprint(reqfd, "failure");
				continue;
			}
			switch (fork()) {
			case 0:
				if (restdir)
					chdir(restdir);
				if (!prevent || (q = getenv("sshsession")) && strcmp(q, "allow") == 0)
					get_string(p+1, cmd);
				else
					confine(p+1, cmd);
				if (slfd > 0)
					fprint(slfd, "running %s for %s\n", cmd, uname);
				else
					syslog(1, "ssh", "running %s for %s", cmd, uname);
				runcmd(reqfd, datafd, "rx", "/bin/rc", "-lc", cmd);
				exits(nil);
			case -1:
				if (want_reply)
					fprint(reqfd, "failure");
				fprint(errfd, "Cannot fork: %r\n");
				exits(nil);
				break;
			default:
				already_done = 1;
				if (want_reply)
					fprint(reqfd, "success");
				break;
			}
		}
		else if (strcmp(buf, "subsystem") == 0) {
			if (want_reply)
				fprint(reqfd, "failure");
		}
		else if (strcmp(buf, "window-change") == 0) {
			if (want_reply)
				fprint(reqfd, "success");
		}
		else if (strcmp(buf, "xon-xoff") == 0) {
		}
		else if (strcmp(buf, "signal") == 0) {
		}
		else if (strcmp(buf, "exit-status") == 0) {
		}
		else if (strcmp(buf, "exit-signal") == 0) {
		}
		else
			fprint(errfd, "Unknown channel request: %s\n", buf);	
	}
}

char *
get_string(char *q, char *s)
{
	int n;

	n = nhgetl(q);
	q += 4;
	memmove(s, q, n);
	s[n] = '\0';
	q += n;
	return q;
}

char *
confine(char *q, char *s)
{
	int i, n, m;
	char *p, *e, *r, *buf, *toks[32];

	n = nhgetl(q);
	q += 4;
	buf = malloc(n+1);
	memmove(buf, q, n);
	buf[n]  = 0;
	m = tokenize(buf, toks, 32);
	e = s + n + 1;
	for (i = 0, r = s; i < m; ++i) {
		p = strrchr(toks[i], '/');
		if (p) {
			if (*(p+1))
				r = seprint(r, e, "%s ", p+1);
			else
				r = seprint(r, e, ". ");
		}
		else
			r = seprint(r, e, "%s ", toks[i]);
	}
	free(buf);
	q += n;
	return q;
}

void
runcmd(int reqfd, int datafd, char *svc, char *cmd, char *arg1, char *arg2)
{
	char *p;
	int fd, cmdpid, child;

	cmdpid = rfork(RFPROC|RFMEM|RFNOTEG|RFFDG);
	switch (cmdpid) {
	case -1:
		fprint(errfd, "fork failed: %r\n");
		break;
	case 0:
		if (restdir == nil) {
			p = smprint("/usr/%s", uname);
			if (access(p, AREAD) == 0)
				chdir(p);
			free(p);
		}
		p = strrchr(cmd, '/');
		if (p)
			++p;
		else
			p = cmd;
		dup(datafd, 0);
		dup(datafd, 1);
		dup(datafd, 2);
		close(datafd);
		putenv("service", svc);
		fprint(errfd, "starting %s\n", cmd);
		execl(cmd, p, arg1, arg2, nil);
		fprint(errfd, "cannot exec %s: %r\n", cmd);
		break;
	default:
		close(datafd);
		while (1) {
			fprint(errfd, "waiting for child %d\n", cmdpid);
			child = waitpid();
			fprint(errfd, "child %d passed\n", child);
			if (child == cmdpid || child == -1)
				break;
		}
		if (child == -1)
			fprint(errfd, "wait failed: %r\n");
		if (slfd > 0)
			fprint(slfd, "closing ssh session for %s\n", uname);
		else
			syslog(1, "ssh", "closing ssh session for %s", uname);
		fprint(errfd, "closing connection\n");
		write(reqfd, "close", 5);
		p = smprint("/proc/%d/notepg", toppid);
		fd = open(p, OWRITE);
		write(fd, "interrupt", 3);
		close(fd);
		break;
	}
	exits(nil);
}
