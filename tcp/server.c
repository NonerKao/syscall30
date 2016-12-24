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

int main(int argc, char *argv[])
{
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr; 
	if(argc < 2){
		printf("./server <port>\n");
		exit(0);
	}
	char sendBuff[1025];
	time_t ticks; 

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if(!listenfd){printf("fd\n"); exit(0);}
	memset(&serv_addr, '0', sizeof(serv_addr));
	memset(sendBuff, '0', sizeof(sendBuff)); 

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1])); 

	int ret = bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 
	if(ret){printf("bind\n"); exit(0);}

	ret = listen(listenfd, 0); 
	if(ret){printf("listen\n"); exit(0);}

	while(1)
	{
		struct sockaddr_in cli;
		int clilen;
		char ipaddr[20];
		connfd = accept(listenfd, (struct sockaddr*)&cli, &clilen);
		printf("%d\n", connfd);
		inet_ntop(AF_INET, &cli.sin_addr, ipaddr, sizeof(struct sockaddr));
		printf("from IP: %s, port: %d\n", ipaddr, ntohs(cli.sin_port));

		ticks = time(NULL);
		sprintf(sendBuff, "%.24s\r\n", ctime(&ticks));
		send(connfd, sendBuff, strlen(sendBuff), 0); 

		sleep(1);
		close(connfd);
	}
}

