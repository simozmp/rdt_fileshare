/*
 * file_sharing.c
 *
 *  Created on: 14 set 2021
 *      Author: simozmp
 */
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "gbn/gbn.h"
#include "main/config.h"
#include "main/file_sharing.h"


int receive_file(int socket_fd, const char* file_name, const char* destination_dir_path) {

	int return_value, n, rcvd = 0;
	size_t filesize;

	char* message;
	char* file_path;
	void* buffer;
	char* filesizestr;
	char* rcvdstr;
	FILE* output_file;

	buffer = malloc(FILE_CHUNK_SIZE);

	message = malloc(10*sizeof(char));
	file_path = malloc(80*sizeof(char));
	filesizestr = malloc(15*sizeof(char));
	rcvdstr = malloc(15*sizeof(char));

	memset(filesizestr, 0, 15*sizeof(char));
	memset(rcvdstr, 0, 15*sizeof(char));

	sprintf(message, "RDYRCV");
	sprintf(file_path, "%s%s", destination_dir_path, file_name);

	//	Opening file / error handling
	if((output_file = fopen(file_path, "w+")) == NULL) {
		perror("fopen()");
		return_value = -1;
	} else {
		//	Telling the peer we are ready to receive / error handling
		if(gbn_write(socket_fd, message, strlen(message)) != strlen(message)) {
			perror("gbn_write()");
			return_value = -1;
		} else {
			if(gbn_read(socket_fd, message, 10*sizeof(char)) < 0) {
				printf("Unexpected error!\n");
			} else {
				filesize = (size_t) strtol(message, (char**) NULL, 10);

				sizetostr(filesize, filesizestr);

				for(;1;) {
					n = 0;
					n = gbn_read(socket_fd, buffer, FILE_CHUNK_SIZE);

					if(n < 0)
						perror("gbn_read");
					else {
						memcpy(message, buffer, 9*sizeof(char));
						if(strcmp(message, "DONESND") != 0) {
							fwrite(buffer, n, 1, output_file);

							rcvd += n;
							sizetostr(rcvd, rcvdstr);
							printf("\r                                    ");
							printf("\rRcvd: %s/%s\t\t%.1f%%", rcvdstr, filesizestr, ((double) rcvd / (double) filesize)*100);
							rcvdstr[0] = '\0';
							fflush(stdout);
						} else {
							printf("\n");
							return_value = 0;
							break;
						}
					}
				}
			}
		}

		fclose(output_file);
	}

	free(buffer);
	free(file_path);
	free(message);

	return return_value;
}

int send_file(int socket_fd, const char* file_name, const char* source_dir_path) {
	int return_value, n, sent = 0;
	FILE* file;
	void* buffer;
	struct stat fstat;

	char* file_path = malloc((strlen(file_name)+strlen(source_dir_path)+1)*sizeof(char));
	char* msg = malloc(30);
	char* filesizestr = malloc(15);
	char* sentstr = malloc(15);

	buffer = malloc(FILE_CHUNK_SIZE);

	memset(msg, 0, 30);
	memset(filesizestr, 0, 15);
	memset(sentstr, 0, 15);
	memset(buffer, 0, FILE_CHUNK_SIZE);
	memset(&fstat, 0, sizeof(struct stat));

	sprintf(file_path, "%s%s", source_dir_path, file_name);

	//	File opening / error handling
	if((file = fopen(file_path, "rb")) == NULL) {
		perror("fopen()");
		return_value = -1;
	} else {
		stat(file_path, &fstat);

		sizetostr(fstat.st_size, filesizestr);

		//	Waiting for peer to be ready / error handling
		if((gbn_read(socket_fd, msg, 30)) < 0) {
			perror("read error");
			return_value = -1;
		} else {
			if(strncmp(msg, "RDYRCV", 6) != 0)
				return_value = -1;
			else {
				sprintf(msg, "%ld", fstat.st_size);

				if(gbn_write(socket_fd, msg, strlen(msg)+1) >= 0) {
					printf("0%%");

					for(long int i = 0; 1==1; i++)
						if((n = fread(buffer, 1, FILE_CHUNK_SIZE, file)) < 0) {
							perror("fread()");
							break;
						} else if(n == 0) {
							break;
						} else {
							n = gbn_write(socket_fd, buffer, n);

							if(n < 0)
								perror("write error");
							else {
								sent += n;
								sizetostr(sent, sentstr);
								printf("\r                                    ");
								printf("\rSent: %s/%s\t\t%.1f%%", sentstr, filesizestr, ((double) sent / (double) fstat.st_size)*100);
								fflush(stdout);
							}
						}

					sprintf(msg, "DONESND");
					if(gbn_write(socket_fd, msg, strlen(msg)+1) < 0)
						perror("gbn_write");

					printf("\n");

					return_value = 0;
				}
			}
		}
	}

	free(buffer);
	free(file_path);
	free(msg);
	free(sentstr);
	free(filesizestr);

	return return_value;
}


/*
 *	This function takes a file size (off_t) and parses it as a readable
 *	string in str
 *
 */
void sizetostr(off_t size, char* str) {
	//double u = 10;
	if(size < pow(10,3))
		sprintf(str, "%d B", (int)size);
	else if(size < pow(10,6))
		sprintf(str, "%.1f KB", ((double)size)/pow(10,3));
	else if(size < pow(10,9))
		sprintf(str, "%.1f MB", ((double)size)/pow(10,6));
	else
		sprintf(str, "%.1f GB", ((double)size)/pow(10,9));
}
