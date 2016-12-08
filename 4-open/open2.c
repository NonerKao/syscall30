#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdio.h>

int main(){
	int fd = open("/home/noner/test", O_RDWR | O_TRUNC | O_CREAT, 0777);
	printf("openning file descriptor %d\n", fd);
	write(fd, "Hello World!\n", 13);
}
