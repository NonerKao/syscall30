#include<stdio.h>

extern char end;

int main(){
	printf("%p\n", &end);
}
