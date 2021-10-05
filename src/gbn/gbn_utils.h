#ifndef GBN_UTILS_H
#define GBN_UTILS_H

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <time.h>


int log_open(char* filename);
int log_dump();
int log_write(char* message);
int log_println();
int current_time_str(char* buffer);
struct timespec ts_abs_diff(struct timespec t1, struct timespec t2);
struct timespec ts_diff(struct timespec t1, struct timespec t2);
struct timespec ts_sum(struct timespec t1, struct timespec t2);
struct timespec ts_times(struct timespec ts, double factor);
int ts_max(struct timespec t1, struct timespec t2);


#endif
