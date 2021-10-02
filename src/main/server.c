#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "gbn/gbn.h"
#include "main/config.h"
#include "main/file_sharing.h"

const char default_pool_path[80] = "./";
char* pool_path;

//	Heap memory by functions
//		list_files_in_pool()
char*	lfp_linebuffer;		// Single line lfp_buffer
char*	lfp_buffer;			// Output lfp_buffer
char*	lfp_filepath;		// String for file path
char*	lfp_sizestr;		// String for the formatted file size

void	serve(int connection_socket, struct sockaddr_in addr);
char*	list_files_in_pool();
void	sizetostr(off_t size, char* str);

int main(int argc, char **argv) {

	int socket_fd, reuse = 1;
	struct sockaddr_in servaddr, cliaddr;

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
	} else {
		sprintf(pool_path, default_pool_path);
	}

	while(1) {
		//	Retrieving a new socket
		if((socket_fd = gbn_socket()) < 0) {
			perror("socket error");
			exit(-1);
		}

		//	Configuring servaddr and resetting cliaddr
		memset((void *)&servaddr, 0, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		servaddr.sin_port = htons(SERV_PORT);
		memset((void *)&cliaddr, 0, sizeof(cliaddr));

		// Allowing address reusage on socket
		if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
			perror("errore setsockopt");
			exit(-1);
		}

		//	Address binding
		if(bind(socket_fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
			perror("bind error");
			exit(-1);
		}

		printf("Waiting for connections..\n");

		//	Accepting connection
		if(gbn_accept(socket_fd, (struct sockaddr *) &cliaddr, sizeof(cliaddr)) < 0) {
			perror("accept error");
			exit(-1);
		} else {
			printf("%s:%d connected\n", inet_ntoa(cliaddr.sin_addr),
				ntohs(cliaddr.sin_port));
		}

		//	Fork
		if(fork() == 0) {	//	Son
			printf("Server forked successfully.\n");
			if(gbn_close(socket_fd) < 0) {
				perror("gbn_close()");
			} else
				printf("\n");
		} else {			//	Father
			serve(socket_fd, cliaddr);
			break;
		}
	}

	free(pool_path);

	exit(0);
}

void serve(int connection_socket, struct sockaddr_in addr) {
	int		n;
	char*	line;
	char*	partial_line;
	char*	command;
	char*	argument;
	char*	tok;
	char*	readymsg;
	char*	filename;

	readymsg = malloc(4*sizeof(char));
	line = malloc(MAXLINE*sizeof(char));
	partial_line = malloc(MAXLINE*sizeof(char));
	filename = malloc(100*sizeof(char));
	command = malloc(MAXCOMMAND*sizeof(char));
	argument = malloc(MAXARGUMENT*sizeof(char));
	tok = malloc(MAXARGUMENT*sizeof(char));

	// lfp_  memory allocation
	lfp_filepath = malloc(sizeof(char)*MAXLINE);	//	TODO: FREE MEMORY
	lfp_sizestr = malloc(sizeof(char)*MAXLINE);
	lfp_buffer = malloc(sizeof(char)*MAXLINE);
	lfp_linebuffer = malloc(sizeof(char)*160);

	sprintf(readymsg, "RDY");

	while(1) {
		
		while((n = gbn_write(connection_socket, readymsg, strlen(readymsg))) != strlen(readymsg)) {
			perror("error writing ready message.\n");
		}

		printf("\nWaiting for request..\n");
		memset(line, 0, MAXLINE*sizeof(char));
		n = gbn_read(connection_socket, line, MAXLINE);
		switch(n) {
		case 0:
			printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
				ntohs(addr.sin_port));
			gbn_close(connection_socket);
			return;
		case -1:
			perror("error in read");
			break;
		default:
			printf("request: '%s'\n", line);
			break;
		}
		
		// Split string in command (1 word) and arguments (>= 0 words)
		command = strtok(line, " \n\t");
		tok = strtok(NULL, "\n");
		if(tok != NULL)
			argument = strcpy(argument, tok);

		if(strcmp(command, "list") == 0) {
			strcpy(line, list_files_in_pool());

			// Write response
			if(gbn_write(connection_socket, line, strlen(line)) != strlen(line)) {
				perror("write error");
			}
			else
				printf("sent: '%s'\n", line);

		} else if(strcmp(command, "help") == 0) {
			sprintf(line, "usage: command [argument]\n\n");

			sprintf(partial_line, "available commands:\n");
			strcat(line, partial_line);
			sprintf(partial_line, "list\tlist available files\n");
			strcat(line, partial_line);
			sprintf(partial_line, "get\tdownload file specified by argument\n");
			strcat(line, partial_line);
			sprintf(partial_line, "put\tupload file specified by argument");
			strcat(line, partial_line);

			// Write response
			if(gbn_write(connection_socket, line, strlen(line)) != strlen(line))
				perror("write error");
			else
				printf("sent: '%s'\n", line);

		} else if(strcmp(command, "get") == 0) {

			if(strlen(argument) == 0) {
				sprintf(line, "Please specify a file to get.\n");
				// Write response
				if(gbn_write(connection_socket, line, strlen(line)) != strlen(line))
					perror("write error");
				else
					printf("sent: '%s'\n", line);
			} else {
				sprintf(filename, "%s%s", pool_path, argument);

				//	Checking file
				if(access(filename, R_OK) != 0) {

					sprintf(line, "NOTAF");
					// Write response
					if(gbn_write(connection_socket, line, strlen(line)) != strlen(line))
						perror("write error");
					else
						printf("sent: '%s'\n", line);

				} else {
					sprintf(line, "FINC %s", argument);
					// Write response
					if(gbn_write(connection_socket, line, strlen(line)) != strlen(line))
						perror("write error");
					else
						printf("sent: '%s'\n", line);

					printf("\nSending file:\n");

					send_file(connection_socket, argument, pool_path);
				}
			}

		} else if(strcmp(command, "put") == 0) {
			if(strlen(argument) == 0) {
				printf(line, "I need a filename to receive the file.\n");
				// Write response
				if(gbn_write(connection_socket, line, strlen(line)) != strlen(line))
					perror("write error");
				else
					printf("sent: '%s'\n", line);
			} else {
				sprintf(line, "WFFILE %s", argument);
				// Write response
				if(gbn_write(connection_socket, line, strlen(line)) != strlen(line))
					perror("write error");
				else
					printf("sent: '%s'\n", line);

				printf("\nReceiving file:\n");

				receive_file(connection_socket, argument, pool_path);
			}
		} else {
			strcpy(line, "Invalid command, try typing help");

			// Write response
			if(gbn_write(connection_socket, line, strlen(line)) != strlen(line))
				perror("write error");
			else
				printf("sent: '%s'\n", line);
		}
	}
}

/*
 *	This function returns a readable table formatted file list of the pool
 *
 */
char* list_files_in_pool() {
	DIR*	d;				// Directory variable for pool
	struct	dirent* dir;	// Directory entry for files
	struct	stat finfo;		// Stat variable for file information

	lfp_buffer[0] = 0;

	// Pool folder opening
	d = opendir(pool_path);

	if(d) {
		sprintf(lfp_linebuffer, "%50s\t%9s\n\n", "Name", "Size");

		// Append line to lfp_buffer
		strcat(lfp_buffer, lfp_linebuffer);

		// Scans each file in pool...
		while((dir = readdir(d)) != NULL)
			// ...which is visible (or not a directory)
			if(dir->d_name[0] != '.') {

				// Setting up complete lfp_filepath
				strcpy(lfp_filepath, pool_path);
				lfp_filepath[strlen(lfp_filepath)] = 0;
				strcat(lfp_filepath, dir->d_name);

				// Get file info
				if(stat(lfp_filepath, &finfo) != 0)
					perror("stat");

				// Get formatted size
				sizetostr(finfo.st_size, lfp_sizestr);
				
				// Print in linebuffer the table line
				sprintf(lfp_linebuffer, "%50s\t%9s\n", dir->d_name, lfp_sizestr);

				// Append line to lfp_buffer
				strcat(lfp_buffer, lfp_linebuffer);
			}

		// Pool folder closure
		closedir(d);
	}

	// To delete the last '\n'
	lfp_buffer[strlen(lfp_buffer)-1] = 0;

	/*/ String memory deallocation
	free(lfp_filepath);
	free(lfp_sizestr);
	free(lfp_linebuffer);*/

	return lfp_buffer;
}

