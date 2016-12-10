#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int main(){
	fork();
	printf("This process is %d\n", getpid());
	printf("The parent process is %d\n", getppid());
}
