#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "../gbn/libgbn.h"

#define SERV_PORT 5193
#define MAXLINE 80

int main() {

	int socksd, n;
	struct sockaddr_in servaddr;
	char *message = NULL;

	// Retrieving a new socket
	if((socksd = gbn_socket()) < 0) {
		perror("socket error");
		exit(-1);
	}

	// Configuring servaddr and resetting cliaddr
	memset((void *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SERV_PORT);

	if(inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr) <= 0) {
		perror("pton");
		return -1;
	}

	if(gbn_connect(socksd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		perror("connection failed");
		return -1;
	}

	message = malloc(MAXLINE*sizeof(char));

	for(int i=0; i<10; i++) {
	
		memset(message, 0, MAXLINE*sizeof(char));
	
		printf("> ");
	
		fgets(message, MAXLINE, stdin);
	    sscanf(message, "%[^\n]", message);
	
		if((n = gbn_write(socksd, message, strlen((char*)message) + 1)) < 0) {
			perror("write error");
		} else {
			printf("Sent \"%s\" with n = %d\n", message, n);
		}
	}
	
	free(message);

	return 0;
}
