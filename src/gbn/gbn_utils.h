/*
 * gbn_utils.h
 *
 *  Created on: 4 set 2021
 *      Author: simozmp
 */

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
int timestamp(char* buffer);


#endif
