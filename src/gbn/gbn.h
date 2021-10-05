/*
 *	Implementation of a Go Back N reliable socket
 *
 *	Author: simozmp
 *
 */

#ifndef GBN_H
#define GBN_H

/*									INCLUDES								*/

#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>

/*								PROTOTYPES									*/

int gbn_socket();
ssize_t gbn_write(int socket, void *buf, size_t count);
ssize_t gbn_read(int socket, void *buf, size_t count);
int gbn_shutdown(int socketfd);
int gbn_close(int socketfd);

//	Client side functions
int gbn_connect(int socketfd, const struct sockaddr *servaddr,
		socklen_t addrlen);

//	Server side functions
int gbn_accept(int socketfd, struct sockaddr *addr, socklen_t addrlen);

#endif
