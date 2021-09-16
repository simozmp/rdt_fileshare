#ifndef GBN_CORE
#define GBN_CORE

/*									INCLUDES								*/

#include <pthread.h>
#include "config.h"
#include "packet.h"
#include "rcv_buffer.h"
#include "snd_buf.h"

#include <sys/socket.h>



/*								PROTOTYPES									*/

int gbnc_connect(int socketfd, const struct sockaddr *servaddr,
		socklen_t addrlen);
int gbnc_accept(int socketfd, struct sockaddr *addr, socklen_t addrlen);
int gbn_verify_socket(int socket);
int wait_delivery();

ssize_t gbn_send(void *data, size_t len);
ssize_t gbn_rcv(void *data, size_t len);

#endif
