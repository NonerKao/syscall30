#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define SYSCALL_ERROR(ret, call)\
	if(ret == -1){\
		fprintf(stderr, call " error at %d\n", __LINE__);\
		exit(-1);\
	}

int main(){
	int wstatus;
	int fd[2];
	const char *msgo = "Hi, this is from %d through stdout.\n";
	const char *msge = "Hi, this is from %d through stderr.\n";

	FILE *target; 

	int ret = pipe(fd);
	SYSCALL_ERROR(ret, "pipe");

	pid_t pid = fork();
	SYSCALL_ERROR(ret, "fork");
	if(pid == 0){
		ret = close(fd[0]);
		SYSCALL_ERROR(ret, "close");
		ret = dup2(fd[1], 1);
		SYSCALL_ERROR(ret, "dup2");

		//char *argv[] = {"/bin/ls", "-l", NULL};
		//execve("/bin/ls", argv, NULL);

		pause();
		printf(msgo, getpid());
		fprintf(stderr, msge, getpid());
		return 0;
	}

	ret = close(fd[1]);
	SYSCALL_ERROR(ret, "close");
	ret = close(fd[0]);
	SYSCALL_ERROR(ret, "close");

	pid = fork();
	SYSCALL_ERROR(pid, "fork");
	if(pid == 0){
		ret = close(fd[1]);
		SYSCALL_ERROR(ret, "close");
		ret = dup2(fd[0], 0);
		SYSCALL_ERROR(ret, "dup2");

		//char *argv[] = {"/bin/wc", "-l", NULL};
		//execve("/bin/wc", argv, NULL);
		pause();

		int ignore;
		scanf(msgo, &ignore);
		printf(msgo, ignore+1);
		return 0;
	}
	


	int count = 2;
	while(count){
		siginfo_t sig;
		waitid(P_ALL, -1, &sig, WEXITED);
		printf("The child %d exits.\n", sig.si_pid);
		count--;
	}

	return 0;
}
