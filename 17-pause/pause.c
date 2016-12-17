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

	sigaction(SIGUSR1, &sa, NULL);

	printf("before the pause: %d\n", state);
	pause();
	printf("after the pause: %d\n", state);
}
