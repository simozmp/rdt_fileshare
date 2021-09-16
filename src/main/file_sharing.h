/*
 * file_sharing.h
 *
 *  Created on: 14 set 2021
 *      Author: simozmp
 */

#ifndef MAIN_FILE_SHARING_H_
#define MAIN_FILE_SHARING_H_

int receive_file(int socket_fd, const char* file_name, const char* destination_dir_path);
int send_file(int socket_fd, const char* file_path, const char* source_dir_path);
void sizetostr(off_t size, char* str);

#endif /* SRC_MAIN_FILE_SHARING_H_ */
