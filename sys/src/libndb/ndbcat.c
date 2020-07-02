#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <ndb.h>

Ndb*
ndbcat(Ndb *a, Ndb *b)
{
	Ndb *db = a;

	/*
	 * make additions persistant, though
	 * not in the database= line
	 */
	if(a != nil)
		a->flags |= Ndbhead;
	if(b != nil)
		b->flags |= Ndbhead;

	if(a == nil)
		return b;
	while(a->next != nil)
		a = a->next;
	a->next = b;
	return db;
}
