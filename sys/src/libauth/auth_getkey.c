#include <u.h>
#include <libc.h>
#include <auth.h>

int
auth_getkey(char *params)
{
	int pid;
	Waitmsg *w;

	switch(pid = fork()){
	case -1:
		werrstr("auth_getkey: fork %r");
		return -1;
	case 0:
		execl("/boot/factotum", "getkey", "-g", params, nil);
		execl("/factotum", "getkey", "-g", params, nil);
		exits("no /factotum or /boot/factotum");
	default:
		for(;;){
			w = wait();
			if(w == nil)
				return 0;
			if(w->pid == pid){
				if(w->msg[0] != '\0'){
					werrstr("auth_getkey: %s: didn't get key %s",
						w->msg, params);
					free(w);
					return -1;
				}
				free(w);
				return 0;
			}
		}
	}
}
