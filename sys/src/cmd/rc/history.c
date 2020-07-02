#include "rc.h"
#include "exec.h"
#include "fns.h"
#include "io.h"

void
whistory(tree *t){
	char* s;
	int fd, flags;
	io* o;
	var* v;

	if(!runq->iflag || !t)
		return;
	v = vlook("history");
	if(!v->val || count(v->val) != 1)
		return;
	if(v->fn)
		return;	/* fn history {echo $*>>$history_file} ? */

	s = v->val->word;
	flags = OWRITE;
	if((fd=open(s, flags))<0 && (fd=create(s, flags, DMAPPEND|0666L))<0){
		/* setvar("history", 0); */
		return;
	}
	
	o = openfd(fd);
	seek(fd, 0, 2);
	pfmt(o, "%t\n", t);
	closeio(o);
}
