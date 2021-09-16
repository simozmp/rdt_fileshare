/*
 * gbn_utils.c
 *
 *  Created on: 4 set 2021
 *      Author: simozmp
 */

#include "gbn/gbn_utils.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <time.h>

//	Log file descriptor
FILE* logfd;

int log_open(char* filename) {

	char* relative_path = "log/";
	char* full_path = malloc(90*sizeof(char));

	sprintf(full_path, "%s%s.log", relative_path, filename);
	logfd = fopen(full_path, "w");

	log_write("Log start");

	free(full_path);

	return logfd == NULL ? -1 : 1;
}

int log_dump() {
	return fclose(logfd);
}

int log_write(char* message) {

	int return_value = 1;
	char* timestring = malloc(30*sizeof(char));

	if(logfd == NULL)
		return_value = -1;
	else {
		timestamp(timestring);
		fprintf(logfd, "[%s]\t%s\n", timestring, message);
	}

	free(timestring);

    return return_value;
}

int log_println() {
	int return_value = 1;

	if(logfd == NULL)
		return_value = -1;
	else
		fprintf(logfd, "\n");

	return return_value;
}

int timestamp(char* buffer) {

	struct tm *long_time;

	struct timespec short_time;
	timespec_get(&short_time, TIME_UTC);

	const time_t rawtime = time(NULL);
	long_time = localtime(&rawtime);

	sprintf(buffer, "%d-%d-%d_%d:%d:%d.%ld", long_time->tm_year + 1900,
												long_time->tm_mon,
												long_time->tm_mday,
												long_time->tm_hour,
												long_time->tm_min,
												long_time->tm_sec,
												short_time.tv_nsec/10000);

	return 0;
}
