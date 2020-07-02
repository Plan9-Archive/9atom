#include <u.h>
#include <libc.h>
#include <auth.h>

extern int getgeom(int *cols, int *lines, int *width, int *height);

int isatty(int);
int doauth(int, char *);

char *user, *remote;
char *netdir, *subsystem;
int debug = 0;
static int stripcr = 0;
static int mflag = 0;
static int cooked = 0;
static int iflag = -1;
static int nopw = 0, nopka = 0;
static int chpid;
static int reqfd, dfd1, cfd1, dfd2, cfd2, consfd, kconsfd, cctlfd, notefd, keyfd, netpid, kbdpid;

void
usage(void)
{
	fprint(2, "usage: ssh [-CdkKmr] [-s subsystem] [-l user] [-n dir] [-z attr=val] addr [cmd [args]]\n");
	exits("usage");
}

int
handler(void *, char *note)
{
	char *p;
	int fd;

	if(strstr(note, "interrupt") != nil)
		return 1;
	if (chpid) {
		p = smprint("/proc/%d/note", chpid);
		fd = open(p, OWRITE);
		free(p);
		fprint(fd, "interrupt");
		close(fd);
	}
	if (iflag){
		if(!cooked)
			fprint(cctlfd, "rawoff");
		close(cctlfd);
		close(consfd);
	}
	fprint(reqfd, "close");
	close(reqfd);
	close(dfd2);
	close(dfd1);
	close(cfd2);
	close(cfd1);
	write(notefd, "kill", 4);
	close(notefd);
	return 1;
}

int
cmdmode(void)
{
	int n, m;
	char buf[256];

	while (1) {
reprompt:
		write(1, "\n>>> ", 5);
		n = 0;
		do {
			m = read(0, buf + n, 255 - n);
			write(1, buf + n, m);
			n += m;
			buf[n] = '\0';
			if (buf[n-1] == 0x15)
				goto reprompt;
		} while (buf[n-1] != '\n' && buf[n-1] != '\r');
		switch (buf[0]) {
		case '\n':
		case '\r':
			break;
		case 'q':
			return 1;
		case 'c':
			return 0;
		case 'C':
			cooked = 1 - cooked;
			if(cooked)
				fprint(cctlfd, "rawoff");
			else
				fprint(cctlfd, "rawon");
			return 0;
		case 'r':
			stripcr = 1 - stripcr;
			return 0;
		default:
			print("C - toggle cooked (local echo) mode\n");
			print("c - continue\n");
			print("h - help\n");
			print("q - quit\n");
			print("r - toggle carriage return stripping\n");
			break;
		}
	}
}

static int
wasintr(void)
{
	char err[ERRMAX];

	rerrstr(err, sizeof err);
	return strstr(err, "interrupt") != nil;
}

void
main(int argc, char *argv[])
{
	char *p, *q, *path;
	char *whichkey;
	int cols, lines, width, height;
	int conn, eofs, chan, n, i, lstart, nfd;
	char buf[32*1024];

	quotefmtinstall();
	keyfd = -1;
	whichkey = nil;
	ARGBEGIN {
	case 'C':
		cooked = 1;
		break;
	case 'd':
		debug++;
		break;
	case 'l':
		user = EARGF(usage());
		break;
	case 'r':
		stripcr = 1;
		break;
	case 'I':
		iflag = 0;
		break;
	case 'i':		/* Used by scp */
		iflag = 1;
		break;
	case 'v':
	case 'a':
	case 'x':
		break;
	case 'k':
		nopka = 1;
		break;
	case 'K':
		nopw = 1;
		break;
	case 'm':
		mflag = 1;
		break;
	case 'n':
		netdir = EARGF(usage());
		break;
	case 's':		/* Used by sftpfs */
		subsystem = EARGF(usage());
		break;
	case 'z':
		whichkey = EARGF(usage());
		break;
	default:
		usage();
		break;
	} ARGEND;
	if (argc == 0)
		usage();
	if (iflag == -1)
		iflag = isatty(0);
	if (subsystem)
		iflag = 0;
	remote = *argv;
	++argv;
	--argc;
	if (q = strchr(remote, '@')) {
		*q = 0;
		user = remote;
		remote = q+1;
	}
	if (!netdir) {
		q = strchr(remote, '!');
		if (q ) {
			n = q-remote;
			netdir = malloc(n+1);
			strncpy(netdir, remote, n);
			netdir[n] = '\0';
			p = strrchr(netdir, '/');
			if (p) {
				if (strcmp(p+1, "ssh") == 0)
					*p = '\0';
				else
					remote = smprint("%s/ssh", netdir);
			}
			else {
				free(netdir);
				netdir = nil;
			}
		}
	}
	if (!user)
		user = getuser();
	if (netdir)
		p = smprint("%s/ssh", netdir);
	else
		p = smprint("/net/ssh");
	if (access(p, OREAD) < 0) {
		if ((n = rfork(RFPROC|RFMEM|RFNOTEG|RFFDG)) == 0) {
			if (netdir)
				execl("/bin/sshtun", "sshtun", "-m", netdir, nil);
			else
				execl("/bin/sshtun", "sshtun", nil);
			exits(nil);
		}
		do {
			i = waitpid();
		} while (i != n && i >= 0);
	}
	free(p);
	if ((n = rfork(RFPROC|RFMEM|RFFDG|RFNOWAIT)) == 0) {
		if (netdir)
			p = smprint("%s/ssh/keys", netdir);
		else
			p = smprint("/net/ssh/keys");
		keyfd = open(p, ORDWR);
		free(p);
		if (keyfd < 0) {
			// fprint(2, "failed to open sskeys: %r\n");
			chpid = 0;
			exits(nil);
		}
		if(iflag)
			kconsfd = open("/dev/cons", ORDWR);
		if (kconsfd < 0)
			nopw = 1;
		n = read(keyfd, buf, 5);
		buf[5] = 0;
		if (n < 0)
			exits(nil);
		n = strtol(buf+1, nil, 10);
		n = readn(keyfd, buf+5, n);
		buf[n+5] = 0;
		switch (*buf) {
		case 'f':
			if (kconsfd >= 0)
				fprint(kconsfd, "%s\n", buf+5);
		case 'o':
			close(keyfd);
			if (kconsfd >= 0)
				close(kconsfd);
			break;
		default:
			if (kconsfd >= 0) {
				if (*buf == 'c') {
					fprint(kconsfd, "The following key has been offered by the server:\n");
					write(kconsfd, buf+5, n);
					fprint(kconsfd, "\n\n");
					fprint(kconsfd, "Add this key? (yes, no, session) ");
				}
				else {
					fprint(kconsfd, "The following key does NOT match the known key(s) for the server:\n");
					write(kconsfd, buf+5, n);
					fprint(kconsfd, "\n\n");
					fprint(kconsfd, "Add this key? (yes, no, session, replace) ");
				}
				n = read(kconsfd, buf, 10);
				write(keyfd, buf, n);
				seek(keyfd, 0, 2);
				readn(keyfd, buf, 5);
				buf[5] = 0;
				n = strtol(buf+1, nil, 10);
				n = readn(keyfd, buf+5, n);
				buf[n+5] = 0;
				switch (*buf) {
				case 'b':
				case 'f':
					fprint(kconsfd, "%s\n", buf+5);
				case 'o':
					close(keyfd);
					close(kconsfd);
				}
			}
			else {
				fprint(keyfd, "n");
				close(keyfd);
			}
		}
		chpid = 0;
		exits(nil);
	}
	chpid = n;
	atnotify(handler,1);
	if (netdir)
		p = smprint("%s/ssh", netdir);
	else
		p = smprint("ssh");
	q = netmkaddr(remote, p, "22");
	free(p);
	dfd1 = dial(q, nil, nil, &cfd1);
	if (dfd1 < 0) {
		fprint(2, "%s: dial: %r\n", argv0);
		if (chpid) {
			p = smprint("/proc/%d/note", chpid);
			nfd = open(p, OWRITE);
			fprint(nfd, "interrupt");
		}
		exits(nil);
	}
	seek(cfd1, 0, 0);
	n = read(cfd1, buf, 10);
	buf[n] = 0;
	conn = atoi(buf);
	if(iflag){
		consfd = open("/dev/cons", ORDWR);
		cctlfd = open("/dev/consctl", OWRITE);
	}
	if(iflag && !cooked && subsystem == nil)
		fprint(cctlfd, "rawon");
	if (doauth(cfd1, whichkey) < 0)
		goto bail;

	if (netdir)
		path = smprint("%s/ssh/%d!session",netdir, conn);
	else
		path = smprint("/net/ssh/%d!session", conn);

	dfd2 = dial(path, nil, nil, &cfd2);
	if (dfd2 < 0) {
		fprint(2, "%s: dial: %r\n", argv0);
		goto bail;
	}
	n = read(cfd2, buf, 10);
	buf[n] = 0;
	chan = atoi(buf);
	free(path);
	if (netdir)
		path = smprint("%s/ssh/%d/%d/request", netdir, conn, chan);
	else
		path = smprint("/net/ssh/%d/%d/request", conn, chan);

	reqfd = open(path, OWRITE);
	if(subsystem){
		fprint(reqfd, "subsystem %s", subsystem);
	}
	else if (argc == 0) {
		strcpy(buf, "dumb");
		if ((i = open("/env/TERM", OREAD)) >= 0){
			n = read(i, buf, 32);
			buf[n] = 0;
			close(i);
		}
		if(getgeom(&cols, &lines, &width, &height) == 0)
			fprint(reqfd, "shell %q %d %d %d %d %d",
				buf, cols, lines, width, height, cooked);
		else
			fprint(reqfd, "shell %s", buf);
	}
	else {
		q = buf;
		for (i = 0; i < argc; ++i) {
			q = seprint(q, buf+1024, " %s", argv[i]);
			if (q == nil)
				break;
		}
		if (q != nil)
			fprint(reqfd, "exec%s", buf);
		else {
			fprint(2, "Command too long\n");
			fprint(reqfd, "close");
			goto bail;
		}
	}
	switch (rfork(RFPROC|RFMEM|RFNOWAIT|RFNOTEG)) {
	case 0:
		netpid = getpid();
		while (1) {
			n = read(dfd2, buf, 32*1024);
			if (n <= 0)
				break;
			if (stripcr) {
				for (i = 0, p = buf, q = buf; i < n; ++i, ++q)
					if (*q != '\r')
						*p++ = *q;
			}
			else
				p = buf + n;
			write(1, buf, p-buf);
		}
		postnote(PNPROC, kbdpid, "kill");
		fprint(2, "Connection closed by server\n");
		break;
	case -1:
		fprint(2, "fork error: %r\n");
		goto bail;
	default:
		eofs = 0;
		lstart = 1;
		kbdpid = getpid();
		while (1) {
			n = read(0, buf, 32*1024);
			if (cooked && n < 0 && wasintr()) {
				buf[0] = 0x7f;
				n = 1;
			}
			if (cooked && n == 0) {
				if(eofs++ > 32)
					break;
				buf[0] = 0x04;
				n = 1;
			}
			else
				eofs = 0;

			if (n <= 0)
				break;
			if (!mflag && lstart && buf[0] == 0x1c) {
				if (cmdmode())
					break;
				else
					continue;
			}
			lstart = (buf[n-1] == '\n' || buf[n-1] == '\r');
			write(dfd2, buf, n);
		}
		postnote(PNPROC, netpid, "kill");
		fprint(2, "EOF on client side\n");
		break;
	}
bail:
	if(iflag){
		if(! cooked)
			fprint(cctlfd, "rawoff");
		close(cctlfd);
		close(consfd);
	}
	fprint(reqfd, "close");
	close(reqfd);
	close(dfd2);
	close(dfd1);
	close(cfd2);
	close(cfd1);
	write(notefd, "kill", 4);
	close(notefd);
	exits(nil);
}

int
isatty(int fd)
{
	char buf[64];

	buf[0] = '\0';
	fd2path(fd, buf, sizeof buf);
	if(strlen(buf)>=9 && strcmp(buf+strlen(buf)-9, "/dev/cons")==0)
		return 1;
	return 0;
}

int
doauth(int cfd1, char *whichkey)
{
	UserPasswd *up;
	int n;

 	if (!nopka) {
		if (whichkey)
			n = fprint(cfd1, "ssh-userauth K %q %q", user, whichkey);
		else
			n = fprint(cfd1, "ssh-userauth K %q", user);
		if (n >= 0)
			return 0;
	}
	if (nopw)
		return -1;
	up = auth_getuserpasswd(iflag ? auth_getkey : nil, "proto=pass service=ssh server=%q user=%q",
		remote, user);
	if (up == nil) {
		fprint(2, "Failure to get password: %r\n");
		return -1;
	}
	n = fprint(cfd1, "ssh-userauth k %s %q", user, up->passwd);
	if (n >= 0)
		return 0;
	fprint(2, "auth %r\n");
	return -1;
}
