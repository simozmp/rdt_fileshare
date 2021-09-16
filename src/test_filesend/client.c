#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "gbn/gbn.h"
#include "test_filesend/config.h"

#define FILENAME "marktext-x86_64.AppImage"

int main() {

	int socksd, n;
	FILE* filedesc;
	struct sockaddr_in servaddr;
	void* buffer;

	char* msg = malloc(30);
	char* filepath = malloc(50);

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

	buffer = malloc(STEP);

	sprintf(filepath, "clientpool/%s", FILENAME);

	filedesc = fopen(filepath, "r");

	sprintf(msg, FILENAME);
	if(gbn_write(socksd, msg, strlen(msg)) < 0)
		perror("gbn_write");

	//	Waiting for server to be ready
	if((gbn_read(socksd, msg, 30)) < 0) {
		perror("read error");
	} else {
		printf("response: %s\n", msg);
	}

	for(int i = 0; 1==1; i++)
		if((n = fread(buffer,1,STEP,filedesc)) < 0) {
			perror("fread");
			break;
		} else if(n == 0) {
			printf("File over. All sent\n");
			break;
		} else {
			printf("%d-th chunck ready to send. size %d..\t", i, n);
			n = gbn_write(socksd, buffer, n);

			if(n < 0)
				perror("write error");
			else
				printf("Sent.\n");
		}

	printf("Telling the server i'm done.\n");

	sprintf(msg, "Done");
	if(gbn_write(socksd, msg, strlen(msg)+1) < 0)
		perror("gbn_write");
}
