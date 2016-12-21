#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>

int main(){
	char *ptr1 = sbrk(0);
	char *ptr2;
	sleep(1);

	ptr1 = (char*)malloc(16384);
	sleep(1);

	ptr2 = (char*)malloc(16384);
	sleep(1);

	free(ptr1);
	sleep(1);

	free(ptr2);
	sleep(1);

	ptr1[100] = ptr1[200] = 'a';
	ptr2[100] = ptr2[200] = 'b';
}
