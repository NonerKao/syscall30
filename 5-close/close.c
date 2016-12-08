#include<unistd.h>
#include<sys/types.h>
#include<fcntl.h>

int main(){
	//ext4
	close(open("/home/noner/test.txt", O_CREAT|O_TRUNC|O_RDWR, 0777));

	// tty
	close(1);

	return 0;
}
