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

//	Heap memory pointer by functions
//		list_files_in_pool()
char*	lfp_linebuffer;	// Presentation string for files in pool
char*	pool_presentation;			// Output lfp_buffer
char*	lfp_filepath;		// String for file path
char*	lfp_sizestr;		// String for the formatted file size

void	serve(int connection_socket, struct sockaddr_in addr);
void	read_pool();
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
				perror("gbn_close(connection_socket)");
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

	//	pool strings memory allocation
	lfp_filepath = malloc(sizeof(char)*MAXLINE);	//	TODO: FREE MEMORY
	lfp_sizestr = malloc(sizeof(char)*MAXLINE);
	pool_presentation = malloc(sizeof(char)*MAXLINE);
	lfp_linebuffer = malloc(sizeof(char)*160);

	sprintf(readymsg, "RDY");

	while(1) {

		//	Sending RDY message
		n = gbn_write(connection_socket, readymsg, strlen(readymsg));
		if(n == strlen(readymsg))
			printf("\nReady message sent to client.\n");
		else if(n == 0) {
			printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
				ntohs(addr.sin_port));
			gbn_close(connection_socket);
			return;
		} else
			perror("write error");

		printf("\nWaiting for request..\n");

		//	Resetting line string
		line[0] = '\0';

		//	Reading command message from client
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
		
		//	Split string in command (1 word) and arguments (>= 0 words)
		command = strtok(line, " \n\t");
		tok = strtok(NULL, "\n");
		if(tok != NULL)
			argument = strcpy(argument, tok);

		if(strcmp(command, "list") == 0) {

			//	Updating lfp_linebuffer
			read_pool();

			//	Sending response
			n = gbn_write(connection_socket, pool_presentation,
					strlen(pool_presentation));

			if(n == strlen(pool_presentation))
				printf("sent:\n'%s'\n", pool_presentation);
			else if(n == 0) {
				printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
					ntohs(addr.sin_port));
				gbn_close(connection_socket);
				return;
			} else if(n < 0) {
				perror("write error");
			} else {
				printf("There was an error. Shutting down connection");
				gbn_shutdown(connection_socket);
				gbn_close(connection_socket);
				return;
			}

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

			//	Sending response
			n = gbn_write(connection_socket, line, strlen(line));

			if(n == strlen(line))
				printf("sent:\n'%s'\n", line);
			else if(n == 0) {
				printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
					ntohs(addr.sin_port));
				gbn_close(connection_socket);
				return;
			} else if(n < 0) {
				perror("write error");
			} else {
				printf("There was an error. Shutting down connection");
				gbn_shutdown(connection_socket);
				gbn_close(connection_socket);
				return;
			}

		} else if(strcmp(command, "get") == 0) {

			if(strlen(argument) == 0) {
				sprintf(line, "Please specify a file to get.\n");

				//	Write response
				n = gbn_write(connection_socket, line, strlen(line));
				if(n == strlen(line))
					printf("sent: '%s'\n", line);
				else if(n == 0) {
					printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
						ntohs(addr.sin_port));
					gbn_close(connection_socket);
					return;
				} else if(n < 0) {
					perror("write error");
				} else {
					printf("There was an error. Shutting down connection");
					gbn_shutdown(connection_socket);
					gbn_close(connection_socket);
					return;
				}

			} else {	//	Sending file

				//	Printing full file name on string
				sprintf(filename, "%s%s", pool_path, argument);

				//	Checking file
				if(access(filename, R_OK) != 0) {

					//	Printing response to string
					sprintf(line, "NOTAF");		//	(NOT A File)

					//	Sending response
					n = gbn_write(connection_socket, line, strlen(line));
					if(n == strlen(line))
						printf("Requested file doesn't exist.\n\"Not a file\" message sent.\n");
					else if(n == 0) {
						printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
							ntohs(addr.sin_port));
						gbn_close(connection_socket);
						return;
					} else if(n < 0) {
						perror("write error");
					} else {
						printf("There was an error. Shutting down connection");
						gbn_shutdown(connection_socket);
						gbn_close(connection_socket);
						return;
					}

				} else {
					sprintf(line, "FINC %s", argument);

					//	Sending response
					n = gbn_write(connection_socket, line, strlen(line));
					if(n == strlen(line))
						printf("sent: '%s'\n", line);
					else if(n == 0) {
						printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
							ntohs(addr.sin_port));
						gbn_close(connection_socket);
						return;
					} else if(n < 0) {
						perror("write error");
					} else {
						printf("There was an error. Shutting down connection");
						gbn_shutdown(connection_socket);
						gbn_close(connection_socket);
						return;
					}

					printf("\nSending file:\n");

					send_file(connection_socket, argument, pool_path);
				}
			}

		} else if(strcmp(command, "put") == 0) {
			if(strlen(argument) == 0) {

				printf(line, "I need a filename to receive the file.\n");

				//	Sending response
				n = gbn_write(connection_socket, line, strlen(line));
				if(n == strlen(line))
					printf("sent: '%s'\n", line);
				else if(n == 0) {
					printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
						ntohs(addr.sin_port));
					gbn_close(connection_socket);
					return;
				} else if(n < 0) {
					perror("write error");
				} else {
					printf("There was an error. Shutting down connection");
					gbn_shutdown(connection_socket);
					gbn_close(connection_socket);
					return;
				}

			} else {
				sprintf(line, "WFFILE %s", argument);

				//	Sending response
				n = gbn_write(connection_socket, line, strlen(line));
				if(n == strlen(line))
					printf("sent: '%s'\n", line);
				else if(n == 0) {
					printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
						ntohs(addr.sin_port));
					gbn_close(connection_socket);
					return;
				} else if(n < 0) {
					perror("write error");
				} else {
					printf("There was an error. Shutting down connection");
					gbn_shutdown(connection_socket);
					gbn_close(connection_socket);
					return;
				}

				printf("\nReceiving file:\n");

				receive_file(connection_socket, argument, pool_path);
			}
		} else {
			strcpy(line, "Invalid command, try typing help");

			//	Sending response
			n = gbn_write(connection_socket, line, strlen(line));
			if(n == strlen(line))
				printf("sent: '%s'\n", line);
			else if(n == 0) {
				printf("%s:%d disconnected\n\n", inet_ntoa(addr.sin_addr),
					ntohs(addr.sin_port));
				gbn_close(connection_socket);
				return;
			} else if(n < 0) {
				perror("write error");
			} else {
				printf("There was an error. Shutting down connection");
				gbn_shutdown(connection_socket);
				gbn_close(connection_socket);
				return;
			}
		}
	}
}

/*
 *	This function returns a readable table formatted file list of the pool
 *
 */
void read_pool() {
	DIR*	pool_dir;			//	DIR variable for pool
	struct	dirent* dir_entry;	//	Directory entry for files
	struct	stat finfo;			//	Stat variable for file information

	//	Pool directory opening
	pool_dir = opendir(pool_path);

	if(pool_dir) {
		//	Writing columns headers line to pool_presentation
		sprintf(pool_presentation, "%50s\t%9s\n\n", "Name", "Size");

		//	Scans each dirent (files/directories) in pool dir...
		while((dir_entry = readdir(pool_dir)) != NULL)
			//	...which is visible and not a directory
			if(dir_entry->d_name[0] != '.' &&
					dir_entry->d_type != DT_DIR) {

				//	Setting up complete lfp_filepath
				strcpy(lfp_filepath, pool_path);
				lfp_filepath[strlen(lfp_filepath)] = 0;
				strcat(lfp_filepath, dir_entry->d_name);

				//	Get file info
				if(stat(lfp_filepath, &finfo) != 0)
					perror("stat");

				//	Get formatted size
				sizetostr(finfo.st_size, lfp_sizestr);
				
				//	Print formatted row with file info in linebuffer
				sprintf(lfp_linebuffer, "%50s\t%9s\n", dir_entry->d_name, lfp_sizestr);

				//	Append line to pool_presentation
				strcat(pool_presentation, lfp_linebuffer);
			}

		//	Pool folder closure
		closedir(pool_dir);

		//	To delete the last '\n' from pool_presentation
		pool_presentation[strlen(pool_presentation)-1] = 0;
	} else {
		sprintf(pool_presentation, "Server pool currently unavailable.");
	}
}

