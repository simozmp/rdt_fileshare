#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "../gbn/libgbn.h"
#include "test_filesend/config.h"

int main() {
	int socksd, n, reuse = 1;
	struct sockaddr_in servaddr, cliaddr;
	socklen_t cliaddrlen;
	char *message = NULL,
			*filepath = NULL;
	FILE* file;
	void* buffer;

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

	message = malloc(50);
	filepath = malloc(50);


	printf("Waiting for incoming files..\n");
	if((n = gbn_read(socksd, message, 50)) < 0) {
		perror("read error");
	} else {
		printf("filename: %s\n", message);
		printf("preparing to receive\n");
	}

	sprintf(filepath, "serverpool/%s", message);
	printf("Dumping received file to %s\n", filepath);

	file = fopen(filepath, "w");
	free(filepath);

	buffer = malloc(STEP);

	if((n = gbn_write(socksd, "Ready", 6)) < 0) {
		perror("write error");
	} else {
		printf("Ready to receive\n");
	}

	while(1 == 1) {

		printf("\nReading chunck.\n");

		n=0;
		n = gbn_read(socksd, buffer, STEP);

		if(n < 0)
			perror("gbn_read");
		else {
			memcpy(message, buffer, 50*sizeof(char));

			if(strcmp(message, "Done") != 0) {
				printf("Writing on file.\n");
				fwrite(buffer, n, 1, file);
			} else
				break;
		}
	}

	printf("File successfully written on disk, closing..\n");
	fclose(file);

	printf("Shutting down\n");


	/*
	printf("About to close connection...");

	printf("Connection succesfully closed!");
	 */

	return 0;
}
