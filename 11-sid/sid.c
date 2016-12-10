#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int main(){
	printf("This process if of session %d\n", getsid(0));
	if(fork() == 0){
		setsid();
	}

	printf("Process %d if of session %d\n", getpid(), getsid(0));
}
