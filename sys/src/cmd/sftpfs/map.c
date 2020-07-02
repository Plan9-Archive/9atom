/* Copyright © 2003 Russ Cox, MIT; see /sys/src/libsunrpc/COPYING */

/*
 * name <-> int translation from nfs(4)
 */

#include "fxp.h"

static Map emptymap;
static int verbose;

static User*
finduser(User **u, int nu, char *s)
{
	int lo, hi, mid, n;

	hi = nu;
	lo = 0;
	while(hi > lo){
		mid = (lo+hi)/2;
		n = strcmp(u[mid]->name, s);
		if(n == 0)
			return u[mid];
		if(n < 0)
			lo = mid+1;
		else
			hi = mid;
	}
	return nil;
}

static int
strtoid(User **u, int nu, char *s, u32int *id)
{
	u32int x;
	char *p;
	User *uu;

	x = strtoul(s, &p, 10);
	if(*s != 0 && *p == 0){
		*id = x;
		return 0;
	}

	uu = finduser(u, nu, s);
	if(uu == nil)
		return -1;
	*id = uu->uid;
	return 0;
}

static char*
idtostr(User **u, int nu, u32int id)
{
	char buf[32];
	int lo, hi, mid;

	hi = nu;
	lo = 0;
	while(hi > lo){
		mid = (lo+hi)/2;
		if(u[mid]->uid == id)
			return estrdup9p(u[mid]->name);
		if(u[mid]->uid < id)
			lo = mid+1;
		else
			hi = mid;
	}
	snprint(buf, sizeof buf, "%ud", id);	
	return estrdup9p(buf);
}

char*
uidtostr(Map *map, u32int uid)
{
	return idtostr(map->ubyid, map->nuser, uid);
}

char*
gidtostr(Map *map, u32int gid)
{
	return idtostr((User**)map->gbyid, map->ngroup, gid);
}

static int
strtouid(Map *map, char *s, u32int *id)
{
	return strtoid(map->ubyname, map->nuser, s, id);
}

static int
strtogid(Map *map, char *s, u32int *id)
{
	return strtoid((User**)map->gbyid, map->ngroup, s, id);
}


static int
idcmp(const void *va, const void *vb)
{
	User **a, **b;

	a = (User**)va;
	b = (User**)vb;
	return (*a)->uid - (*b)->uid;
}

static int
namecmp(const void *va, const void *vb)
{
	User **a, **b;

	a = (User**)va;
	b = (User**)vb;
	return strcmp((*a)->name, (*b)->name);
}

void
closemap(Map *m)
{
	int i;

	for(i=0; i<m->nuser; i++){
		free(m->user[i].name);
		free(m->user[i].auth);
	}
	for(i=0; i<m->ngroup; i++)
		free(m->group[i].name);
	free(m->user);
	free(m->group);
	free(m->ubyid);
	free(m->ubyname);
	free(m->gbyid);
	free(m->gbyname);
	free(m);
}

Map*
readmap(char *passwd, char *group)
{
	char *s, *f[10], *p, *nextp, *name;
	int i, nf, line, uid, gid;
	Biobuf *b;
	Map *m;
	User *u;
	Group *g;

	m = emalloc9p(sizeof(Map));

	if((b = Bopen(passwd, OREAD)) == nil){
		free(m);
		return nil;
	}
	line = 0;
	for(; (s = Brdstr(b, '\n', 1)) != nil; free(s)){
		line++;
		if(s[0] == '#')
			continue;
		nf = getfields(s, f, nelem(f), 0, ":");
		if(nf < 4)
			continue;
		name = f[0];
		uid = strtol(f[2], &p, 10);
		if(f[2][0] == 0 || *p != 0){
			fprint(2, "%s:%d: non-numeric id in third field\n", passwd, line);
			continue;
		}
		gid = strtol(f[3], &p, 10);
		if(f[3][0] == 0 || *p != 0){
			fprint(2, "%s:%d: non-numeric id in fourth field\n", passwd, line);
			continue;
		}
		if(m->nuser%32 == 0)
			m->user = erealloc9p(m->user, (m->nuser+32)*sizeof(m->user[0]));
		u = &m->user[m->nuser++];
		u->name = estrdup9p(name);
		u->uid = uid;
		u->gid = gid;
		u->ng = 0;
		u->auth = 0;
		u->nauth = 0;
	}
	Bterm(b);
	m->ubyname = emalloc9p(m->nuser*sizeof(User*));
	m->ubyid = emalloc9p(m->nuser*sizeof(User*));
	for(i=0; i<m->nuser; i++){
		m->ubyname[i] = &m->user[i];
		m->ubyid[i] = &m->user[i];
	}
	qsort(m->ubyname, m->nuser, sizeof(m->ubyname[0]), namecmp);
	qsort(m->ubyid, m->nuser, sizeof(m->ubyid[0]), idcmp);

	if((b = Bopen(group, OREAD)) == nil){
		closemap(m);
		return nil;
	}
	line = 0;
	for(; (s = Brdstr(b, '\n', 1)) != nil; free(s)){
		line++;
		if(s[0] == '#')
			continue;
		nf = getfields(s, f, nelem(f), 0, ":");
		if(nf < 4)
			continue;
		name = f[0];
		gid = strtol(f[2], &p, 10);
		if(f[2][0] == 0 || *p != 0){
			fprint(2, "%s:%d: non-numeric id in third field\n", group, line);
			continue;
		}
		if(m->ngroup%32 == 0)
			m->group = erealloc9p(m->group, (m->ngroup+32)*sizeof(m->group[0]));
		g = &m->group[m->ngroup++];
		g->name = estrdup9p(name);
		g->gid = gid;

		for(p=f[3]; *p; p=nextp){
			if((nextp = strchr(p, ',')) != nil)
				*nextp++ = 0;
			else
				nextp = p+strlen(p);
			u = finduser(m->ubyname, m->nuser, p);
			if(u == nil){
				if(verbose)
					fprint(2, "%s:%d: unknown user %s\n", group, line, p);
				continue;
			}
			if(u->ng >= nelem(u->g)){
				fprint(2, "%s:%d: user %s is in too many groups; ignoring %s\n", group, line, p, name);
				continue;
			}
			u->g[u->ng++] = gid;
		}
	}
	Bterm(b);
	m->gbyname = emalloc9p(m->ngroup*sizeof(Group*));
	m->gbyid = emalloc9p(m->ngroup*sizeof(Group*));
	for(i=0; i<m->ngroup; i++){
		m->gbyname[i] = &m->group[i];
		m->gbyid[i] = &m->group[i];
	}
	qsort(m->gbyname, m->ngroup, sizeof(m->gbyname[0]), namecmp);
	qsort(m->gbyid, m->ngroup, sizeof(m->gbyid[0]), idcmp);

	return m;
}
