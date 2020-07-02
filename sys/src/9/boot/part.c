#include <u.h>
#include <libc.h>
#include "dosfs.h"

enum {
	Npart	= 32,

	Data	= 0,
	Ctl	= 1,
};

typedef struct Dev Dev;
struct Dev{
	char	dev[16];
	int	fd[2];
	uint	ss;
	uvlong	lba;
};

#define	dprint(...)		do{}while(0)

int
mbrread(Dev *d, uvlong sec, void *buf)
{
	uchar *u;

	if(d->ss < 0x200)
		return -1;
	if(pread(d->fd[Data], buf, d->ss, d->ss*sec) != d->ss)
		return -1;
	u = buf;

	if(u[0x1fe] != 0x55 || u[0x1ff] != 0xaa)
		return -1;
	return 0;
}

int
secread(Dev *d, uvlong sec, void *buf)
{
	if(pread(d->fd[Data], buf, d->ss, d->ss*sec) != d->ss)
		return -1;
	return 0;
}

void
addpart(Dev *d, char *part, uvlong start, uvlong end)
{
	dprint("%s: part %s %lld %lld\n", d->dev, part, start, end);
	if(fprint(d->fd[Ctl], "part %s %lld %lld\n", part, start, end) <= 0)
		fprint(2, "addpart: %s: %r\n", d->dev);
}

static int
p9part(Dev *d, uvlong sec, char*, char *buf)
{
	char *field[4], *line[Npart+1];
	uvlong start, end;
	int i, n;

	dprint("p9part %lld\n", sec);
	if(secread(d, sec+1, buf))
		return -1;
	buf[d->ss-1] = '\0';
	if(strncmp(buf, "part ", 5))
		return -1;

	n = getfields(buf, line, Npart+1, 1, "\n");
	dprint("p9part %d lines..", n);
	if(n == 0)
		return -1;
	for(i = 0; i < n; i++){
		if(strncmp(line[i], "part ", 5) != 0)
			break;
		if(getfields(line[i], field, 4, 0, " ") != 4)
			break;
		start = strtoull(field[2], 0, 0);
		end = strtoull(field[3], 0, 0);
		if(start >= end || end > d->lba)
			break;
		addpart(d, field[1], sec+start, sec+end);
	}
	return 0;
}

int
isdos(int t)
{
	return t==FAT12 || t==FAT16 || t==FATHUGE || t==FAT32 || t==FAT32X;
}

int
isextend(int t)
{
	return t==EXTEND || t==EXTHUGE || t==LEXTEND;
}

static int
mbrpart(Dev *d, uvlong sec, char *mbr, char *part)
{
	char name[10];
	int ndos, i, nplan9;
	ulong start, end;
	ulong firstx, nextx, npart;
	Dospart *dp;
	int (*repart)(Dev*, uvlong, char*, char*);

	sec = 0;
	dp = (Dospart*)&mbr[0x1BE];

	/* get the MBR (allowing for DMDDO) */
	if(mbrread(d, sec, mbr))
		return -1;
	for(i=0; i<4; i++)
		if(dp[i].type == DMDDO) {
			dprint("DMDDO %d\n", i);
			sec = 63;
			if(mbrread(d, sec, mbr))
				return -1;
			i = -1;	/* start over */
		}

	/*
	 * Read the partitions, first from the MBR and then
	 * from successive extended partition tables.
	 */
	nplan9 = 0;
	ndos = 0;
	firstx = 0;
	for(npart=0;;) {
		if(mbrread(d, sec, mbr) != 0)
			return -1;
		if(firstx)
			dprint("%s ext %llud ", d->dev, sec);
		else
			dprint("%s mbr ", d->dev);
		nextx = 0;
		for(i=0; i<4; i++) {
			start = sec+GLONG(dp[i].start);
			end = start+GLONG(dp[i].len);
			if(dp[i].type == 0 && start == 0 && end == 0){
				if(i == 0)
					return -1;
				continue;
			}
			dprint("type %x [%ld, %ld)", dp[i].type, start, end);
			repart = 0;
			if(dp[i].type == PLAN9) {
				if(nplan9 == 0)
					snprint(name, sizeof name, "plan9");
				else
					snprint(name, sizeof name, "plan9.%d", nplan9);
				repart = p9part;
				nplan9++;
			}else if(!ndos && isdos(dp[i].type)){
				ndos = 1;
				strcpy(name, "dos");
			}else
				snprint(name, sizeof name, "%ld", npart);
			npart++;
			if(end != 0){
				dprint(" %s..", name);
				addpart(d, name, start, end);
			}
			if(repart)
				repart(d, start, nil, part);
			
			/* nextx is relative to firstx (or 0), not sec */
			if(isextend(dp[i].type)){
				nextx = start-sec+firstx;
				dprint("link %lud...", nextx);
			}
		}
		dprint("\n");
		if(!nextx)
			break;
		if(!firstx)
			firstx = nextx;
		sec = nextx;
	}	
	return 0;
}

void
partition(Dev *d, uvlong lba)
{
	char *m, *p;

	m = malloc(8192);
	p = malloc(8192);
	!mbrpart(d, lba, m, p) || p9part(d, lba, nil, p);
	free(m);
	free(p);
}

static int
getsize(Dev *d)
{
	char *s0, *s, *p, *f[3];
	int l, r;

	l = 1024;	/* #S/sdctl has 0 size; guess */
	d->lba = 0;
	d->ss = 512;
	if((s0 = malloc(l + 1)) == nil)
		return -1;
	if((l = read(d->fd[Ctl], s0, l)) == -1){
		free(s0);
		return -1;
	}
	s0[l] = 0;
	r = 0;
	for(s = s0; p = strchr(s, '\n'); s = p + 1){
		*p = 0;
		if(tokenize(s, f, nelem(f)) < 1)
			continue;
		if(strcmp(f[0], "geometry") == 0){
			d->lba = strtoull(f[1], nil, 0);
			d->ss = strtoul(f[2], nil, 0);
			dprint("%s geometry %llud %ud\n", d->dev, d->lba, d->ss);
		}
//		else if(strcmp(f[0], "part") == 0 && strcmp(f[1], "data") != 0)
//			r = -1;
	}
	free(s0);
	return r;
}

static void
partitions0(char *s, int l)
{
	char *p, *q, *f[3], buf[20];
	int i;
	Dev d;

	dprint("partitions0\n");
	s[l] = 0;
	for(; p = strchr(s, '\n'); s = p + 1){
		*p = 0;
		if(tokenize(s, f, nelem(f)) < 1)
			continue;
		for(i = 0; i < 0x10; i++){
			snprint(buf, sizeof buf, "%s%xpart", f[0], i);
			if((q = getenv(buf)) != nil){
				free(q);
				continue;
			}
			d.fd[0] = -1;
			d.fd[1] = -1;
			snprint(d.dev, sizeof d.dev, "#S/%s%x", f[0], i);
			snprint(buf, sizeof buf, "%s/data", d.dev);
			d.fd[0] = open(buf, OREAD);
			if(d.fd[0] >= 0){
				snprint(buf, sizeof buf, "%s/ctl", d.dev);
				d.fd[1] = open(buf, ORDWR);
			}
			if(d.fd[0] >= 0 && d.fd[1] >= 0)
			if(getsize(&d) != -1)
				partition(&d, 0);
			close(d.fd[0]);
			close(d.fd[1]);
		}
	}
}

void
partitions(void)
{
	char *s;
	int fd, l;

	fd = open("#S/sdctl", OREAD);
	if(fd == -1)
		return;
	l = 1024;	/* #S/sdctl has 0 size; guess */
	if(s = malloc(l + 1))
	if((l = read(fd, s, l)) > 0)
		partitions0(s, l);
	free(s);
	close(fd);
}
