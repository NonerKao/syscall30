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
	int sockfd = 0;
	struct sockaddr_in cli_addr; 
	if(argc < 2){
		printf("%s <ip> <port>\n", argv[0]);
		exit(0);
	}
	char sendBuff[1025];
	time_t ticks; 

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(!sockfd){printf("sockfd\n"); exit(0);}
	memset(&cli_addr, '0', sizeof(cli_addr));
	memset(sendBuff, '0', sizeof(sendBuff)); 

	cli_addr.sin_family = AF_INET;
	cli_addr.sin_port = htons(atoi(argv[2]));

	if(inet_pton(AF_INET, argv[1], &cli_addr.sin_addr)<0)
	{
		printf("\n inet_pton error occured\n");
		return 1;
	} 

	if( connect(sockfd, (struct sockaddr *)&cli_addr, sizeof(cli_addr)) < 0)
	{
		printf("\n Error : Connect Failed \n");
		printf("errno = %s\n", strerror(errno));
		return 1;
	} 

	int len = recv(sockfd, sendBuff, 1024, 0);
	sendBuff[len] = '\0';
	printf("message from server: [%s], length = %d\n", sendBuff, len);
	close(sockfd);

	return 0;
}

