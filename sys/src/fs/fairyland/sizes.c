#include "all.h"

extern void exits(char*);

void
main(void)
{
	print("RBUFSIZE	%ud\n", RBUFSIZE);
	print("BUFSIZE   	%ud\n", BUFSIZE);
	print("DIRPERBUF	%ud\n", DIRPERBUF);
	print("INDPERBUF	%ud\n", INDPERBUF);
	print("FEPERBUF	%ud\n", FEPERBUF);
	print("SMALLBUF	%ud\n", SMALLBUF);
	print("LARGEBUF	%ud\n", LARGEBUF);
	print("RAGAP		%ud\n", RAGAP);
	print("CEPERBK 	%ud\n", CEPERBK);
	print("BKPERBLK	%ud\n", BKPERBLK);
	print("sizeof Dentry	%ud\n", sizeof(Dentry));
	exits("");
}
