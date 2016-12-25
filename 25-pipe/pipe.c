#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define SYSCALL_ERROR(ret, call, ...)\
	ret = call(__VA_ARGS__);\
	if(ret == -1){\
		fprintf(stderr, "%s error at %d\n", #call, __LINE__);\
		exit(-1);\
	}

const char *msgo = "Hi, this is from %d through stdout.\n";
const char *msge = "Hi, this is from %d through stderr.\n";

int wchild(int fd[]){
	int ret;
	SYSCALL_ERROR(ret, close, fd[0]);
	SYSCALL_ERROR(ret, dup2, fd[1], 1);

	printf(msgo, getpid());
	fprintf(stderr, msge, getpid());
	return 0;
}

int rchild(int fd[]){
	int ret;
	SYSCALL_ERROR(ret, close, fd[1]);
	SYSCALL_ERROR(ret, dup2, fd[0], 0);

	int wpid;
	scanf(msgo, &wpid);
	printf(msgo, wpid+1);
	return 0;
}

int main(){
	int wstatus;
	int fd[2];

	FILE *target; 

	int ret;
	SYSCALL_ERROR(ret, pipe, fd);

	pid_t pid;
	SYSCALL_ERROR(pid, fork);
	if(pid == 0)
		return wchild(fd);

	SYSCALL_ERROR(pid, fork);
	if(pid == 0)
		return rchild(fd);

	SYSCALL_ERROR(ret, close, fd[1]);
	SYSCALL_ERROR(ret, close, fd[0]);

	int count = 2;
	while(count){
		siginfo_t sig;
		waitid(P_ALL, -1, &sig, WEXITED);
		printf("The child %d exits.\n", sig.si_pid);
		count--;
	}

	return 0;
}
