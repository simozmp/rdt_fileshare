/*
 *	Implementation at level 5 of go back n protocol
 *
 *	Author: simozmp
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

//	Client side functions
int gbn_connect(int socketfd, const struct sockaddr *servaddr,
		socklen_t addrlen);

//	Server side functions
int gbn_accept(int socketfd, struct sockaddr *addr, socklen_t addrlen);

#endif
