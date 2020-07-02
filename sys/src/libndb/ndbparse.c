#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <ndb.h>
#include "ndbhf.h"

/*
 *  Parse a data base entry.  Entries may span multiple
 *  lines.  An entry starts on a left margin.  All subsequent
 *  lines must be indented by white space.  An entry consists
 *  of tuples of the forms:
 *	attribute-name
 *	attribute-name=value
 *	attribute-name="value with white space"
 *
 *  The parsing returns a 2-dimensional structure.  The first
 *  dimension joins all tuples. All tuples on the same line
 *  form a ring along the second dimension.
 */

/*
 *  parse the next entry in the file
 */
Ndbtuple*
ndbparse(Ndb *db)
{
	char *line;
	Ndbtuple *t;
	Ndbtuple *first, *last;
	int len;

	first = last = 0;
	for(;;){
		if((line = Brdline(&db->b, '\n')) == 0)
			break;
		len = Blinelen(&db->b);
		if(line[len-1] != '\n')
			break;
		if(first && !ISWHITE(*line) && *line != '#'){
			Bseek(&db->b, -len, 1);
			break;
		}
		t = _ndbparseline(line);
		if(t == 0)
			continue;
		setmalloctag(t, getcallerpc(&db));
		if(first)
			last->entry = t;
		else
			first = t;
		last = t;
		while(last->entry)
			last = last->entry;
	}
	ndbsetmalloctag(first, getcallerpc(&db));
	return first;
}

Ndbtuple*
ndbparsedbs(Ndbs *s, Ndb *db)
{
	Ndbtuple *t;

	if(db == nil)
		sysfatal("ndbparsedbs: nil db %#p\n", getcallerpc(&s));
	if(s->db == nil){
		s->db = db;
		ndbreopen(s->db);
	//	Bseek(&db->b, 0, 0);
	}
	for(;;){
		if(t = ndbparse(s->db))
			return t;
		if(s->db->next == nil)
			return nil;
		s->db = s->db->next;
		ndbreopen(s->db);
	//	Bseek(&db->b, 0, 0);
	}
}
