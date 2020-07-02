#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <ndb.h>
#include "ndbhf.h"

static Ndb*	doopen(char*);
static void	hffree(Ndb*);

static char *deffile = "/lib/ndb/local";

/*
 *  the database entry in 'file' indicates the list of files
 *  that makeup the database.  Open each one and search in
 *  the same order.
 */
Ndb*
ndbopen(char *file)
{
	Ndb *db, *first, *last;
	Ndbs s;
	Ndbtuple *t, *nt;

	if(file == 0)
		file = deffile;
	db = doopen(file);
	if(db == 0)
		return 0;
	db->flags |= Ndbhead;
	first = last = db;
	t = ndbsearch(db, &s, "database", "");
	Bseek(&db->b, 0, 0);
	if(t == 0){
		db->flags |= Ndbnodel;
		return db;
	}
	for(nt = t; nt; nt = nt->entry){
		if(strcmp(nt->attr, "file") != 0)
			continue;
		if(strcmp(nt->val, file) == 0){
			/* default file can be reordered in the list */
			if(first->next == 0)
				continue;
			if(strcmp(first->file, file) == 0){
				db = first;
				first = first->next;
				last->next = db;
				db->next = 0;
				last = db;
			}
			continue;
		}
		db = doopen(nt->val);
		if(db == 0)
			continue;
		last->next = db;
		last = db;
	}
	ndbfree(t);
	return first;
}

/*
 *  open a single file
 */
static Ndb*
doopen(char *file)
{
	Ndb *db;

	db = (Ndb*)malloc(sizeof(Ndb));
	if(db == 0)
		return 0;
	memset(db, 0, sizeof(Ndb));
	strncpy(db->file, file, sizeof(db->file)-1);

	if(ndbreopen(db) < 0){
		free(db);
		return 0;
	}

	return db;
}

/*
 *  dump any cached information, forget the hash tables, and reopen a single file
 */
int
ndbreopen(Ndb *db)
{
	int fd;
	Dir *d;

	/* forget what we know about the open files */
	if(db->mtime){
		_ndbcacheflush(db);
		hffree(db);
		close(Bfildes(&db->b));
		Bterm(&db->b);
		db->mtime = 0;
	}

	/* try the open again */
	fd = open(db->file, OREAD);
	if(fd < 0)
		return -1;
	d = dirfstat(fd);
	if(d == nil){
		close(fd);
		return -1;
	}

	db->qid = d->qid;
	db->mtime = d->mtime;
	db->length = d->length;
	Binits(&db->b, fd, OREAD, db->buf, sizeof(db->buf));
	free(d);
	return 0;
}

static Ndb*
finddb(Ndb **t, char *f)
{
	Ndb *x;

	for(; x = *t; t = &x->next)
		if(strcmp(f, x->file) == 0){
			*t = x->next;
			return x;
		}
	return nil;
}

static Ndb**
adddb(Ndb **ll, Ndb **dd, Ndb **db, char *name)
{
	Ndb *d;

	*dd = nil;
	d = finddb(db, name);
	if(d == nil)
		d = doopen(name);
	else
		d->next = nil;
	if(d == nil)
		return ll;
	*dd = d;
	*ll = d;
	ll = &d->next;
	return ll;
}

Ndb*
ndbreopendb(Ndb *db)
{
	Ndb **ll, *d, *d0;
	Ndbs s;
	Ndbtuple *t, *p;

	if(ndbreopen(db) == -1)
		return db;
	d0 = nil;
	ll = &d0;
	t = ndbsearch(db, &s, "database", "");
	Bseek(&db->b, 0, 0);
	if(t == nil)
		return db;
	for(; t != nil; t = ndbsnext(&s, "database", "")){
		for(p = t; p != nil; p = p->entry){
			if(strcmp(p->attr, "file") != 0)
				continue;
			ll = adddb(ll, &d, &db, p->val);
		}
		ndbfree(t);
	}
	for(d = *ll; d != nil;){
		if((d->flags & Ndbnodel) == 0){
			d = d->next;
			continue;
		}
		ll = adddb(ll, &d, &db, d->file);
		d = *ll;
	}
	ndbclose(db);
	return d0;
}

/*
 *  close the database files
 */
void
ndbclose(Ndb *db)
{
	Ndb *nextdb;

	for(; db; db = nextdb){
		nextdb = db->next;
		_ndbcacheflush(db);
		hffree(db);
		close(Bfildes(&db->b));
		Bterm(&db->b);
		free(db);
	}
}

/*
 *  free the hash files belonging to a db
 */
static void
hffree(Ndb *db)
{
	Ndbhf *hf, *next;

	for(hf = db->hf; hf; hf = next){
		next = hf->next;
		close(hf->fd);
		free(hf);
	}
	db->hf = 0;
}

/*
 *  return true if any part of the database has changed
 */
int
ndbchanged(Ndb *db)
{
	Ndb *ndb;
	Dir *d;

	for(ndb = db; ndb != nil; ndb = ndb->next){
		d = dirfstat(Bfildes(&ndb->b));
		if(d == nil)
			continue;
		if(ndb->qid.path != d->qid.path
		|| ndb->qid.vers != d->qid.vers){
			free(d);
			return 1;
		}
		free(d);
	}
	return 0;
}
