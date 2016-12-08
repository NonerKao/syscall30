#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(){
	int number = 0;
	int wstatus;

	pid_t pid = fork();

	if(pid > 0){
		waitpid(pid, &wstatus, 0);
		if(WIFEXITED(wstatus))
			printf("The child exits.\n");
	}
	else{
		scanf("%d", &number);
	}

	printf("%d before return with %d\n", getpid(), number);
	return number;
}
