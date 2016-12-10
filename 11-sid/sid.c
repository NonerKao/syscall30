#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[], char *envp[]){

	pid_t pid;

	printf("This process is of session %d\n", getsid(0));
	if((pid = fork()) == 0){
		setsid();
		system("bash");
	} else {
		waitpid(pid, NULL, 0);
	}
	printf("Process %d if of session %d\n", getpid(), getsid(0));
}
