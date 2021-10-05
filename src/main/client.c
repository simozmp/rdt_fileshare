
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gbn/gbn.h"
#include "main/config.h"
#include "main/file_sharing.h"


const char default_pool_path[80] = "./";
char* pool_path;
int socket_fd;
struct sockaddr_in servaddr;

int session();
int connect_to_server();

int main(int argc, char **argv) {

	//	A string to keep the internal command of client
	//	application (the only valid is 'connect')
	char* internal_command = malloc(80*sizeof(char));

	pool_path = malloc(80*sizeof(char));

	//	Changing pool directory if argument has been passed
	if(argc > 1) {
		sprintf(pool_path, "%s", argv[1]);
		if(pool_path[strlen(pool_path)-1] != '/') {
			printf("The specified directory doesn't exists. (did you forget / at the end?)\n");
			return -1;
		}

		//	Verifying directory validity
		DIR* dir = opendir(pool_path);
		if(!dir) {
			if(errno == ENOENT) {
				printf("The specified directory doesn't exists.\n");
				return -1;
			} else {
				perror("opendir()");
				printf("Couldn't verify pool directory.\n");
				return -1;
			}
		} else
			printf("Working directory: \"%s\"\n\n", pool_path);
	} else
		sprintf(pool_path, default_pool_path);

	//	Printing the default internal command as default action
	sprintf(internal_command, "connect");

	//	Main cycle (to manage multiple commands)
	while(strncmp(internal_command, "connect", 7) == 0) {

		//	Perform a communication session
		if(session() == -1)
			break;

		printf("If you want to reconnect, just type 'connect'.\n");

		//	Internal prompt to insert a new (internal) command
		printf("\n# ");
		fflush(stdout);
		fgets(internal_command, 80, stdin);
		sscanf(internal_command, "%[^\n]", internal_command);
	}

	printf("Goodbye!\n");

	free(internal_command);
	free(pool_path);

	return 0;
}

/*
 *	The following function manage a connection session with the server.
 *
 *	Returns -1 if the session is meant to be the last, 0 otherwise
 *
 */
int session() {
	char* promptline;
	char* response;
	char* filename;
	int n;

	promptline = malloc(MAXLINE*sizeof(char));
	response = malloc(MAXLINE*sizeof(char));
	filename = malloc(90*sizeof(char));

	//	Connecting to server
	if(connect_to_server() < 0) {
		perror("Couldn't connect to server. gbn_connect()");
		return -1;
	} else {
		printf("Connected to server.\n");
	}

	while(1) {

		//	Wait for RDY message from server
		do {
			n = gbn_read(socket_fd, response, MAXLINE);
			if(n < 0) {
				perror("gbn_read()");
				return 0;
			} else if(n == 0) {
				printf("Server disconnected.\n");
				return 0;
			}
			response[n] = 0;
		} while(strcmp(response, "RDY") != 0);

		//	Cleaning up promptline string
		promptline[0] = '\n';
		promptline[1] = '\0';

		//	Entering a command (while it is not an empty string)
		while(strcmp(promptline, "\n") == 0) {

			//	Server command prompt
			printf("\n#> ");
			fflush(stdout);
			fgets(promptline, 80, stdin);
			sscanf(promptline, "%[^\n]", promptline);

			//	Preventing to send a file not present in pool
			if(strncmp(promptline, "put ", 4) == 0) {

				//	Parsing specified file name
				sscanf(promptline, "put %[^\n]", filename);
				//	Using promptline to temporarily save the full file path
				sprintf(promptline, "%s%s", pool_path, filename);

				//	Checking if specified file exists in internal pool
				if(access(promptline, R_OK) != 0) {
					printf("Specified file doesn't exist, please try again.\n");

					//	Printing '\n' to promptline to make the command unvalid
					sprintf(promptline, "\n");
				} else
					//	Restoring original command
					sprintf(promptline, "put %s", filename);

			//	Managing 'close' command
			} else if(strncmp(promptline, "close", 5) == 0) {
				printf("\nShutting down connection..\n");

				//	Disconnecting from server without closing the socket
				if(gbn_shutdown(socket_fd) < 0) {
					perror("gbn_shutdown()");
				} else {
					printf("..successfully disconnected from server. Closing socket.\n");

					//	Closing socket
					if(gbn_close(socket_fd) < 0)
						perror("gbn_close()");
					else
						//	Finish session and exit program
						return -1;
				}

			//	Managing 'disconnect' command
			} else if(strncmp(promptline, "disconnect", 10) == 0) {
				printf("\nDisconnecting from server (shutting down connection)..\n");

				//	Disconnecting from server without closing the socket
				if(gbn_shutdown(socket_fd) < 0) {
					perror("gbn_shutdown()");
				} else {
					printf("Successfully disconnected from server.\n");
					return 0;
				}

			}
		}

		//	Writing command to server
		switch(gbn_write(socket_fd, promptline, strlen(promptline)+1)) {
		case -1:
			perror("gbn_write");
			break;
		case 0:
			printf("\nServer is not connected.\n");
			return 0;
			break;
		default:
			break;
		}

		printf("\n");

		//	Reading response from network
		n = gbn_read(socket_fd, response, MAXLINE);
		response[n] = 0;

		//	Checking gbn_read() return value
		switch(n) {
		case 0:
			printf("\nServer is not connected.\n");
			return 0;
			break;
		case -1:
			perror("gbn_read()");
			break;
		default:	//	Managing server known responses

			if(strncmp(response, "NOTAF", 4) == 0) {			//	NOT A File
				//	The requested file doesn't exists
				printf("The requested file doesn't exist.\n");

			} else if(strncmp(response, "FINC", 4) == 0) {		//	File INComing

				//	Parsing filename from response
				//	(expected response form is "FINC file.ext")
				strtok(response, " ");
				strcpy(filename, strtok(NULL, ""));

				printf("Downloading \"%s\" file from server..\n", filename);

				//	Receiving file from server
				if(receive_file(socket_fd, filename, pool_path) < 0)
					printf("There was an error.\n");
				else
					printf("..done!\n");

			} else if(strncmp(response, "WFFILE", 6) == 0) {	//	Waiting For FILE

				//	Parsing filename from response
				//	(expected response form is "WFFILE file.ext")
				strtok(response, " ");
				strcpy(filename, strtok(NULL, ""));

				if(strlen(filename) == 0) {
					printf("Something went wrong\n");
				} else {
					printf("Uploading \"%s\" file to server..\n", filename);

					//	Sending file to server
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

/*
 *	This function retrieves a new socket, and connects to the server
 *
 */
int connect_to_server() {

	//	Retrieving a gbn socket
	if ((socket_fd = gbn_socket()) < 0) {
		perror("gbn_socket()");
		return -1;
	}

	//	Configuring servaddr
	memset((void *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SERV_PORT);

	//	Writing server address to servaddr
	if(inet_pton(AF_INET, SERV_ADDR, &servaddr.sin_addr) <= 0) {
		perror("inet_pton()");
		return -1;
	}

	return gbn_connect(socket_fd, (struct sockaddr *) &servaddr, sizeof(servaddr));
}
