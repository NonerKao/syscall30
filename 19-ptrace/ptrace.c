#include<unistd.h>
#include<sys/ptrace.h>
#include<sys/types.h>
#include<sys/time.h>
#include<sys/resource.h>
#include<sys/wait.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

int main(){
	
	pid_t pid = fork();
	
	if(pid > 0){
		int wstatus;
		int count = 0;
		void *addr;
		int data;

		ptrace(PTRACE_ATTACH, pid, NULL, NULL);
		while(waitpid(pid, &wstatus, 0)){
			if(WIFEXITED(wstatus)){
				printf("The child exits\n");
				return 0;
			}
			else if(WIFSTOPPED(wstatus) && WSTOPSIG(wstatus) == SIGSTOP){
				if(count == 4){
					printf("Enter the address of count:\n");
					scanf("%p", &addr);
					printf("Enter a number:\n");
					scanf("%d", &data);
					ptrace(PTRACE_POKEDATA, pid, addr, data);
				}
				else if(count == 8){
					printf("Enter the address of fmt[8]:\n");
					scanf("%p", &addr);
					ptrace(PTRACE_POKEDATA, pid, addr, 0x0000000a78257830);
				}
				else if(count == 12){
					printf("Enter the address of is_end:\n");
					scanf("%p", &addr);
					ptrace(PTRACE_POKEDATA, pid, addr, 1);
				}
				ptrace(PTRACE_CONT, pid, NULL, NULL);
			}

			count++;
		}
	}
	else{
		int is_end = 0;
		int count = 0;
		char fmt[40] = "count = %d\n";

		printf("Addresses to be poked:\n");
		printf("\tis_end = %p\n", &is_end);
		printf("\tcount = %p\n", &count);
		printf("\tfmt[8] = %p\n", &fmt[8]);
		printf("===\n");

		ptrace(PTRACE_TRACEME, 0, NULL, NULL);

		while(!is_end){
			kill(getpid(), SIGSTOP);
			count++;
			fprintf(stderr, fmt, count);
			sleep(1);
		}
		return 0;
	}
}
