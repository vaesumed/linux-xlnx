//
//this code was developed my Miklos Szeredi <miklos@szeredi.hu>
//and modified by Ram Pai <linuxram@us.ibm.com>
// sample usage:
//              smount /tmp shared
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/fsuid.h>

#ifndef MS_REC
#define MS_REC		0x4000	/* 16384: Recursive loopback */
#endif

#ifndef MS_SHARED
#define MS_SHARED		1<<20	/* Shared */
#endif

#ifndef MS_PRIVATE
#define MS_PRIVATE		1<<18	/* Private */
#endif

#ifndef MS_SLAVE
#define MS_SLAVE		1<<19	/* Slave */
#endif

#ifndef MS_UNBINDABLE
#define MS_UNBINDABLE		1<<17	/* Unbindable */
#endif

int main(int argc, char *argv[])
{
	int type;
	if(argc != 3) {
		fprintf(stderr, "usage: %s dir "
		"<rshared|rslave|rprivate|runbindable|shared|slave"
		"|private|unbindable>\n" , argv[0]);
		return 1;
	}

	fprintf(stdout, "%s %s %s\n", argv[0], argv[1], argv[2]);

	if (strcmp(argv[2],"rshared")==0)
		type=(MS_SHARED|MS_REC);
	else if (strcmp(argv[2],"rslave")==0)
		type=(MS_SLAVE|MS_REC);
	else if (strcmp(argv[2],"rprivate")==0)
		type=(MS_PRIVATE|MS_REC);
	else if (strcmp(argv[2],"runbindable")==0)
		type=(MS_UNBINDABLE|MS_REC);
	else if (strcmp(argv[2],"shared")==0)
		type=MS_SHARED;
	else if (strcmp(argv[2],"slave")==0)
		type=MS_SLAVE;
	else if (strcmp(argv[2],"private")==0)
		type=MS_PRIVATE;
	else if (strcmp(argv[2],"unbindable")==0)
		type=MS_UNBINDABLE;
	else {
		fprintf(stderr, "invalid operation: %s\n", argv[2]);
		return 1;
	}
	setfsuid(getuid());

	if(mount("", argv[1], "dontcare", type, "") == -1) {
		perror("mount");
		return 1;
	}
	return 0;
}
