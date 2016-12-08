#include<stdio.h>

int main(){
	FILE *fp[6];

	fp[0] = fopen("/tmp/r.txt", "r");
	fp[1] = fopen("/tmp/r+.txt", "r+");
	fp[2] = fopen("/tmp/w.txt", "w");
	fp[3] = fopen("/tmp/w+.txt", "w+");
	fp[4] = fopen("/tmp/a.txt", "a");
	fp[5] = fopen("/tmp/a+.txt", "a+");

	fclose(fp[0]);
	fclose(fp[1]);
	fclose(fp[2]);
	fclose(fp[3]);
	fclose(fp[4]);
	fclose(fp[5]);
	return 0;
}
