#include<unistd.h>

int main(){
	char buf[5566];

	read(0, buf, 5566);
	write(1, buf, 20);

	return 0;
}
