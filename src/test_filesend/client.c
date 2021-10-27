#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>

#include "../gbn/libgbn.h"
#include "test_filesend/config.h"

#define ITERATIONS 3

/*
 *	This application connects to a server, and ciclically sends the file passed as argument,
 *	computing the average sending time.
 *
 */
int main(int argc, char** argv) {

	int socksd, n;
	FILE* filedesc;
	struct sockaddr_in servaddr;
	void* buffer;
	struct timeval start_time, end_time, elapsed_time, temp_time, sum_time, avg_time;
	long long int avg_time_int, sum_time_int;

	char* msg = malloc(30);
	char* filename = malloc(50);

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

	sprintf(filename, "%s", argv[1]);

	sum_time.tv_sec = 0;
	sum_time.tv_usec = 0;

	for(int i = 0; i < ITERATIONS; i++) {

		printf("Iteration %d: ", i+1);
		fflush(stdout);

		filedesc = fopen(filename, "r");

		sprintf(msg, "%s", argv[1]);
		if(gbn_write(socksd, msg, strlen(msg)) < 0)
			perror("gbn_write");

		//	Waiting for server to be ready
		if((gbn_read(socksd, msg, 30)) < 0) {
			perror("read error");
		} else {
			printf("server ready\n");
		}

		printf("Sending file..\n");
		gettimeofday(&start_time, NULL);

		for(int i = 0; 1==1; i++)
			if((n = fread(buffer,1,STEP,filedesc)) < 0) {
				perror("fread");
				break;
			} else if(n == 0) {
				printf("..sent.\n");
				break;
			} else {
				n = gbn_write(socksd, buffer, n);

				if(n < 0)
					perror("write error");
			}

		gettimeofday(&end_time, NULL);

		timersub(&end_time, &start_time, &elapsed_time);

		printf("Sending took %ld.%ld seconds.\n",
				elapsed_time.tv_sec, elapsed_time.tv_usec);

		timeradd(&elapsed_time, &sum_time, &temp_time);
		memcpy(&sum_time, &temp_time, sizeof(struct timeval));

		printf("Telling the server i'm done.\n");

		sprintf(msg, "Done");
		if(gbn_write(socksd, msg, strlen(msg)+1) < 0)
			perror("gbn_write");

		sum_time_int = sum_time.tv_sec*1000000 + sum_time.tv_usec;
		avg_time_int = sum_time_int/(i+1);
		avg_time.tv_sec = avg_time_int/1000000;
		avg_time.tv_usec = avg_time_int - 1000000*avg_time.tv_sec;

		printf("Average sending time is %ld.%.6ld seconds.\n\n",
				avg_time.tv_sec, avg_time.tv_usec);
	}

	gbn_shutdown(socksd);
	gbn_close(socksd);
}
