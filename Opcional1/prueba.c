#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

int main(){
	unsigned char buff[100];
	int readed, i, fd = open("/proc/modtimer", O_RDONLY);	
	
	while(readed = read(fd, buff, 100))
		for(i = 0; i < readed; i++)
			printf("%hhu\n",buff[i]);

	close(fd);

	return 0;
}
