#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "gbn/gbn.h"

#define SERV_PORT 5193
#define MAXLINE 80

int main() {
	int socksd, n, reuse = 1;
	struct sockaddr_in servaddr, cliaddr;
	socklen_t cliaddrlen;
	unsigned char *line = NULL;

	// Retrieving a new socket
	if((socksd = gbn_socket()) < 0) {
		perror("socket error");
		exit(-1);
	}

	// Allowing address reusage on socket
	if(setsockopt(socksd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
		perror("errore setsockopt");
		exit(-1);
	}

	// Configuring servaddr and resetting cliaddr
	memset((void *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SERV_PORT);
	memset((void *)&cliaddr, 0, sizeof(cliaddr));
	cliaddrlen = sizeof(cliaddr);

	// Address binding
	if(bind(socksd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		perror("bind error");
		exit(-1);
	}

	printf("Waiting for connections..\n");

	if(gbn_accept(socksd, (struct sockaddr *) &cliaddr, cliaddrlen) < 0) {
		perror("accept error");
		exit(-1);
	} else {
		printf("%s:%d connected\n", inet_ntoa(cliaddr.sin_addr),
			ntohs(cliaddr.sin_port));
	}

	line = malloc(MAXLINE*sizeof(char));

	while(strcmp((const char *) line, "close") != 0) {

		memset(line, 0, MAXLINE*sizeof(char));

		printf("Reading..\n");
		if((n = gbn_read(socksd, line, MAXLINE)) < 0) {
			perror("read error");
		} else {
			printf("n: %d, message: %s\n", n, line);
		}
	}

	printf("Shutting down\n");

	/*
	printf("About to close connection...");

	printf("Connection succesfully closed!");
	 */

	return 0;
}
