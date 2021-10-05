#include "gbn/gbn_utils.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <time.h>

//	Log file descriptor
FILE* logfd;

/*
 *	Opens the log file
 */
int log_open(char* filename) {

	char* relative_path = "log/";
	char* full_path = malloc(90*sizeof(char));

	struct stat dir_stat;

	if(stat(relative_path, &dir_stat) == -1)
		mkdir(relative_path, 0777);

	sprintf(full_path, "%s%s.log", relative_path, filename);
	logfd = fopen(full_path, "w");

	log_write("Log start");

	free(full_path);

	return logfd == NULL ? -1 : 1;
}

/*
 *	Closes the log file
 */
int log_dump() {
	return fclose(logfd);
}

/*
 *	Writes message into log file (with timestamp)
 */
int log_write(char* message) {

	int return_value = 0;
	char* timestring = malloc(30*sizeof(char));

	if(logfd == NULL)
		return_value = -1;
	else {
		current_time_str(timestring);
		fprintf(logfd, "[%s]\t%s\n", timestring, message);
		fflush(logfd);
	}

	free(timestring);

    return return_value;
}

/*
 *	prints a blank line in log
 */
int log_println() {
	int return_value = 0;

	if(logfd == NULL)
		return_value = -1;
	else
		fprintf(logfd, "\n");

	return return_value;
}

/*
 *	Writes in buffer current time formatted string
 */
int current_time_str(char* buffer) {

	struct tm *long_time = malloc(sizeof(struct tm));

	struct timespec short_time;
	timespec_get(&short_time, TIME_UTC);

	const time_t rawtime = time(NULL);

	localtime_r(&rawtime, long_time);

	sprintf(buffer, "%d-%d-%d_%d:%d:%d.%ld", long_time->tm_year + 1900,
												long_time->tm_mon,
												long_time->tm_mday,
												long_time->tm_hour,
												long_time->tm_min,
												long_time->tm_sec,
												short_time.tv_nsec/10000);

	free(long_time);

	return 0;
}

/*
 *	Returns the sum of t1 and t2
 */
struct timespec ts_sum(struct timespec t1, struct timespec t2) {
	struct timespec result;

	result.tv_sec = t1.tv_sec + t2.tv_sec;

	if(t1.tv_nsec + t2.tv_nsec > 999999999)
		result.tv_sec++;

	result.tv_nsec = (t1.tv_nsec + t2.tv_nsec) % 999999999;

	return result;
}

/*
 *	Returns the absolute difference of t1 and t2
 */
struct timespec ts_abs_diff(struct timespec t1, struct timespec t2) {
	struct timespec result;
	int max = ts_max(t1,t2);

	if(max == 1)
		result = ts_diff(t1, t2);
	else if(max == 2)
		result = ts_diff(t2, t1);
	else {
		result.tv_nsec = 0;
		result.tv_sec = 0;
	}

	return result;
}

/*
 *	Returns the difference of t1 and t2
 */
struct timespec ts_diff(struct timespec t1, struct timespec t2) {
	struct timespec result;

	if(t1.tv_nsec - t2.tv_nsec < 0) {
		result.tv_nsec = t1.tv_nsec - t2.tv_nsec + 1000000000;
		result.tv_sec = t1.tv_sec - t2.tv_sec - 1;
	} else {
		result.tv_nsec = t1.tv_nsec - t2.tv_nsec;
		result.tv_sec = t1.tv_sec - t2.tv_sec;
	}

	return result;
}

/*
 *	Returns factor times ts
 */
struct timespec ts_times(struct timespec ts, double factor) {
	struct timespec result;

	double sec = ts.tv_sec*factor;

	//	Truncating double to get the floor of sec
	result.tv_sec = (int) sec;

	result.tv_nsec = ((long int)(ts.tv_nsec*factor)) % 999999999 + 1000000000*(sec - ((int) sec));

	result.tv_sec += (int) ts.tv_nsec*factor /1000000000;

	return result;
}

/*
 * Compares t1 and t2 and returns:
 * · 1 if t1 is greater than t2
 * · 2 if t1 is smaller than t2
 * · 0 if t1 is equal t2
 *
 */
int ts_max(struct timespec t1, struct timespec t2) {

	if(t1.tv_sec > t2.tv_sec)
		return 1;
	else if(t1.tv_sec < t2.tv_sec)
		return 2;
	else if(t1.tv_nsec > t2.tv_nsec)
		return 1;
	else if(t1.tv_nsec < t2.tv_nsec)
		return 2;
	else
		return 0;
}
