#include<stdio.h>
#include<unistd.h>
#include<signal.h>

int state = 0;

void siguser1(int signal){
	state = 1;
	return;
}

int main(){

	struct sigaction sa;
	sa.sa_handler = &siguser1;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);

	printf("before the pause: %d\n", state);
	alarm(3);
	pause();
	printf("after the pause: %d\n", state);
}
