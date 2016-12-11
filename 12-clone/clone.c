#include <stdio.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/wait.h>
#define _GNU_SOURCE       
#include <sys/syscall.h>  
#include <unistd.h>

void *thread_fn(void* arg){
	int tid = syscall(SYS_gettid);
	printf("I am the thread %d in child %d\n", tid, getpid());
	return NULL;
}

int main(){

	pthread_t pt;
	pid_t pid;
	int wstatus;

	if( (pid = fork()) != 0 ) {
		waitpid(pid, &wstatus, 0);
	} else {
		printf("I am the child %d\n", getpid());
		pthread_create(&pt, NULL, &thread_fn, NULL);
		pthread_join(pt, NULL);
	}

}
