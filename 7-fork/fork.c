#include<unistd.h>
#include<stdio.h>

int main(){
	pid_t pid;

	asm("mov $57, %rax\n");
	asm("syscall\n");

	asm("movl %%eax, %0\n" 
		:"=r" (pid));

	printf("I am %d and get %d from fork()\n", getpid(), pid);
}
