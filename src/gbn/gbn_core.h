#ifndef GBN_CORE
#define GBN_CORE

/*									INCLUDES								*/

#include <pthread.h>
#include "config.h"
#include "packet.h"
#include "rcv_buffer.h"
#include <sys/socket.h>
#include "snd_buffer.h"



/*								PROTOTYPES									*/

int gbnc_connect(int socketfd, const struct sockaddr *servaddr,
		socklen_t addrlen);
int gbnc_accept(int socketfd, struct sockaddr *addr, socklen_t addrlen);
int gbn_verify_socket(int socket);
ssize_t gbnc_send(void *data, size_t len);
void wait_delivery();
int gbnc_shutdown(int socket);
int gbnc_close(int socket);

#endif
