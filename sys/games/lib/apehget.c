#define _POSIX_SOURCE
#define _BSD_EXTENSION

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define _PLAN9_SOURCE
#define _PLAN9_EXTENSION

#include <u.h>
#include "/sys/src/ape/lib/ap/plan9/sys9.h"

typedef unsigned int u32int;
typedef unsigned long long u64int;
#include <fmt.h>
#include <utf.h>
#include <mp.h>
#include <libsec.h>

char *argv0;

char	*net;
char	tcpdir[40];
int debug = 0;

#define HOST "svn.python.org"

int	httprcode(int);
void	initibuf(void);
int	readline(int, char*, int);
int	readibuf(int, char*, int);
int	dfprint(int, char*, ...);



int
dfprint(int fd, char *fmt, ...)
{
	char buf[4*1024];
	va_list arg;

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	if(debug)
		fprint(2, "%d -> %s", fd, buf);
	return fprint(fd, "%s", buf);
}

/* get the http response code */
int
httprcode(int fd)
{
	int n;
	char *p;
	char buf[256];

	n = readline(fd, buf, sizeof(buf)-1);
	if(n <= 0)
		return n;
	if(debug)
		fprint(2, "%d <- %s\n", fd, buf);
	p = strchr(buf, ' ');
	if(strncmp(buf, "HTTP/", 5) != 0 || p == nil){
		werrstr("bad response from server");
		return -1;
	}
	buf[n] = 0;
	return atoi(p+1);
}

/*
 *  buffered io
 */
struct
{
	char *rp;
	char *wp;
	char buf[4*1024];
} b;

void
initibuf(void)
{
	b.rp = b.wp = b.buf;
}

/*
 *  read a possibly buffered line, strip off trailing while
 */
int
readline(int fd, char *buf, int len)
{
	int n;
	char *p;
	int eof = 0;

	len--;

	for(p = buf;;){
		if(b.rp >= b.wp){
			n = read(fd, b.wp, sizeof(b.buf)/2);
			if(n < 0)
				return -1;
			if(n == 0){
				eof = 1;
				break;
			}
			b.wp += n;
		}
		n = *b.rp++;
		if(len > 0){
			*p++ = n;
			len--;
		}
		if(n == '\n')
			break;
	}

	/* drop trailing white */
	for(;;){
		if(p <= buf)
			break;
		n = *(p-1);
		if(n != ' ' && n != '\t' && n != '\r' && n != '\n')
			break;
		p--;
	}
	*p = 0;

	if(eof && p == buf)
		return -1;

	return p-buf;
}

void
unreadline(char *line)
{
	int i, n;

	i = strlen(line);
	n = b.wp-b.rp;
	memmove(&b.buf[i+1], b.rp, n);
	memmove(b.buf, line, i);
	b.buf[i] = '\n';
	b.rp = b.buf;
	b.wp = b.rp + i + 1 + n;
}

static int
reporter(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprint(2, fmt, ap);
	fprint(2, "\n");
	va_end(ap);
	return 0;
}

int main(int argc, char *argv[])
{
	struct addrinfo hints, *res;
	int error;
	int s;
	const char *cause = NULL;
	int tfd;
	TLSconn conn;
	char addrstr[100];
	void *ptr;
	int code;

	USED(argc);
	USED(argv);

	debug = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(HOST, "443", &hints, &res);
	if(error) {
		printf("getaddrinfo failed\n");
		return 0;
	}

	printf("sys=%s\n", HOST);
	s = -1;
	ptr = NULL;
	while(res) {
		inet_ntop(res->ai_family, res->ai_addr->sa_data, addrstr, 100);
		switch(res->ai_family) {
			case AF_INET:
				ptr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
				break;
			case AF_INET6:
				ptr = &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
				break;
		}

		inet_ntop(res->ai_family, ptr, addrstr, 100);
		printf("\tip=%s\n", addrstr);

		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if(s<0) {
			cause = "socket";
		} else {
			if(connect(s, res->ai_addr, res->ai_addrlen) < 0) {
				cause = "connect";
				close(s);
				s = -1;
			} else
				break;
		}

		res = res->ai_next;
	}

	if(s<0) {
		printf("cause: %s\n", cause);
		return 0;
	}

	memset(&conn, 0, sizeof conn);
	conn.trace = reporter;
	tfd = tlsClient(s, &conn);
	if(tfd < 0){
		fprint(2, "tlsClient: %r\n");
		close(s);
		return 0;
	}

	if(conn.cert)
		free(conn.cert);
	close(s);

	dfprint(tfd, "GET / HTTP/1.0\r\n"
			"Host: acme.buf.io\r\n"
			"User-agent: Plan9/tester\r\n"
			"Cache-Control: no-cache\r\n"
			"Pragma: no-cache\r\n");
	dfprint(tfd, "\r\n");

	initibuf();
	code = httprcode(tfd);
	printf("code: %d\n", code);

	close(tfd);

	return 0;
}


