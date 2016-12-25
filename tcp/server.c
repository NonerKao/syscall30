#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h> 

#define SYSCALL_ERROR(ret, call, ...)\
	ret = call(__VA_ARGS__);\
	if(ret < 0){\
		fprintf(stderr, "%s error at %d\n", #call, __LINE__);\
		exit(-1);\
	}

int main(int argc, char *argv[]){

	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr; 
	if(argc < 2){
		printf("./server <port>\n");
		exit(0);
	}
	char send_buf[1025];
	time_t ticks; 
	int ret;

	SYSCALL_ERROR(listenfd, socket, AF_INET, SOCK_STREAM, IPPROTO_IP);

	memset(&serv_addr, '0', sizeof(serv_addr));
	memset(send_buf, '0', sizeof(send_buf)); 

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1])); 

	SYSCALL_ERROR(ret, bind, listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

	SYSCALL_ERROR(ret, listen, listenfd, 0);

	while(1)
	{
		struct sockaddr_in cli;
		int clilen;
		char ipaddr[20];

		SYSCALL_ERROR(connfd, accept, listenfd, (struct sockaddr*)&cli, &clilen);

		inet_ntop(AF_INET, &cli.sin_addr, ipaddr, sizeof(struct sockaddr));
		printf("from IP: %s, port: %d\n", ipaddr, ntohs(cli.sin_port));

		ticks = time(NULL);
		sprintf(send_buf, "%.24s\n", ctime(&ticks));

		SYSCALL_ERROR(ret, sendto, connfd, send_buf, strlen(send_buf), 0, NULL, 0);

		sleep(1);
		close(connfd);
	}
}

