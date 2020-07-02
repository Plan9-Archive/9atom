#include <u.h>
#include <libc.h>

void
main(int argc, char *argv[])
{
	char *p, buf[4];
	int i;
	long secs;

	if(argc>1){
		for(secs = strtoul(argv[1], &p, 0); secs > 0; secs--)
			sleep(1000);
		/*
		 * no floating point because it is useful to
		 * be able to run sleep when bootstrapping
		 * a machine.
		 */
		if(*p == '.'){
			p++;
			for(i = 0; i < 3; i++)
				if(*p)
					buf[i] = *p++;
				else
					buf[i] = '0';
			buf[3] = 0;
			sleep(strtoul(buf, 0, 10));
		}
	}
	exits(0);
}
