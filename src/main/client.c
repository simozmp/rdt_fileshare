
#include <sys/types.h>
#include <sys/socket.h>
//#include<Winsock2.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gbn/gbn.h"
#include "main/config.h"
#include "main/file_sharing.h"


const char pool_path[80] = "clientpool/";
int socket_fd;
struct sockaddr_in servaddr;

int connect_to_server();

int main(int argc, char **argv) {

	char* promptline;
	char* response;
	char* filename;
	int n;

	promptline = malloc(80*sizeof(char));
	response = malloc(MAXLINE*sizeof(char));
	filename = malloc(90*sizeof(char));

	if(connect_to_server() < 0) {
		perror("Couldn't connect to server.");
		return -1;
	} else {
		printf("Connected to server.\n");
	}

	while(1) {

		do {
			n = gbn_read(socket_fd, response, MAXLINE);
			response[n] = 0;
		} while(strcmp(response, "RDY") != 0);

		promptline[0] = '\n';
		promptline[1] = '\0';
		while(strcmp(promptline, "\n") == 0) {
			printf("\n> ");
			fflush(stdout);

			fgets(promptline, 80, stdin);
			sscanf(promptline, "%[^\n]", promptline);

			//	Command check
			if(strncmp(promptline, "put ", 4) == 0) {

				sscanf(promptline, "put %[^\n]", filename);
				sprintf(promptline, "%s%s", pool_path, filename);

				if(access(promptline, R_OK) != 0) {
					printf("Specified file doesn't exist, please try again.\n");
					sprintf(promptline, "\n");
				} else
					sprintf(promptline, "put %s", filename);
			}
		}

		if(gbn_write(socket_fd, promptline, strlen(promptline)+1) < 0) {
			perror("write");
			exit(-1);
		}
		
		n = gbn_read(socket_fd, response, MAXLINE);
		response[n] = 0;
		switch(n) {
		case 0:
			perror("server shut");
			exit(0);
			break;
		case -1:
			perror("read error");
			exit(-1);
			break;
		default:
			if(strncmp(response, "NOTAF", 4) == 0) {	//	NOT A File
				//	The requested file doesn't exists
				printf("The requested file doesn't exist.\n");

			} else if(strncmp(response, "FINC", 4) == 0) {		//	File INComing

				strtok(response, " ");
				strcpy(filename, strtok(NULL, ""));

				printf("Downloading \"%s\" file from server..\n", filename);

				if(receive_file(socket_fd, filename, pool_path) < 0)
					printf("There was an error.\n");
				else
					printf("..done!\n");

			} else if(strncmp(response, "WFFILE", 6) == 0) {	//	The server is Waiting For File

				strtok(response, " ");
				strcpy(filename, strtok(NULL, ""));

				if(strlen(filename) == 0) {
					printf("Something went wrong\n");
				} else {
					printf("Uploading \"%s\" file to server..\n", filename);

					if(send_file(socket_fd, filename, pool_path) < 0)
						printf("There was an error.\n");
					else
						printf("..done!\n");
				}

			} else
				printf("%s\n", response);
			break;
		}
	}
}

int connect_to_server() {
	if ((socket_fd = gbn_socket()) < 0) {
		perror("socketfd error");
		return -1;
	}

	// Configuring servaddr
	memset((void *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SERV_PORT);


	if(inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr) <= 0) {
		perror("pton");
		return -1;
	}

	return gbn_connect(socket_fd, (struct sockaddr *) &servaddr, sizeof(servaddr));
}
